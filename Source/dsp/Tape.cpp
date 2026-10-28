#include "Tape.h"

namespace dendrite::dsp
{
namespace
{
float coefficientFor (float seconds, float sampleRate) noexcept
{
    return 1.0f - std::exp (-1.0f / (seconds * sampleRate));
}

int wrap (int i, int size) noexcept
{
    return ((i % size) + size) % size;
}

constexpr float silentDb = -100.0f;
} // namespace

void Tape::prepare (float sampleRate, uint32_t seed)
{
    sr = sampleRate;
    for (auto& c : channels)
    {
        c.split1.setFrequency (250.0f, sr);
        c.split2.setFrequency (1500.0f, sr);
        c.split3.setFrequency (6000.0f, sr);
        c.lowAp2.setFrequency (1500.0f, sr);
        c.lowAp3.setFrequency (6000.0f, sr);
        c.mid1Ap3.setFrequency (6000.0f, sr);
        c.delay.prepare ((int) (0.03f * sr));
        c.emphasis.setCoefficients (2500.0f, 0.6f, sr);
        c.deemphasis.setCoefficients (2500.0f, 0.6f, sr);
        c.ageFilter.setCoefficients (20000.0f, 0.7071f, sr);
        c.dcBlock.prepare (sr);
        c.dustHighpass.setCutoff (900.0f, sr);
        c.dustLowpass.setCutoff (7000.0f, sr);
    }

    // A whole number of samples: at rest the tape is bit-transparent, with no
    // interpolation dulling the top.
    tapeDelay = std::round (0.004f * sr);

    wow.prepare (sr / (float) controlInterval, 1.2f, seed * 17u + 3u);
    flutter.prepare (sr / (float) controlInterval, 14.0f, seed * 23u + 5u);
    noise.seed (seed * 2654435761u + 11u);

    gainCoef = coefficientFor (0.0057f, sr);
    fastAttack = coefficientFor (0.002f, sr);
    fastRelease = coefficientFor (0.03f, sr);
    lostAttack = coefficientFor (0.0011f, sr);
    lostRelease = coefficientFor (0.028f, sr);
    envAttack = coefficientFor (0.0005f, sr);
    envRelease = coefficientFor (0.011f, sr);
    engageCoef = coefficientFor (0.05f, sr);

    buildTexture (seed);
    reset();
}

void Tape::reset()
{
    for (auto& row : damage)
        row.fill (0.0f);
    historyValid.fill (false);
    for (auto& c : channels)
    {
        c.split1.reset();
        c.split2.reset();
        c.split3.reset();
        c.lowAp2.reset();
        c.lowAp3.reset();
        c.mid1Ap3.reset();
        c.bandGain.fill (1.0f);
        c.delay.reset();
        c.emphasis.reset();
        c.deemphasis.reset();
        c.ageFilter.reset();
        c.dcBlock.reset();
        c.dustHighpass.reset();
        c.dustLowpass.reset();
        c.lostEnvelope = c.envelope = 0.0f;
    }
    wow.reset();
    flutter.reset();
    fastEnvelope.fill (0.0f);
    playCell = songCell = 0;
    sub = -1;
    wasPlaying = false;
    startCell (0);
    age = ageMix = 0.0f;
    engaged = 1.0f;
    modulation = 0.0f;
    tapePosition = 0.0f;
    sampleCounter = 0;
}

void Tape::setWear (float amount) noexcept
{
    // How many passes of the 4-bar tape until its weak spots are badly worn:
    // 24 passes (96 bars) near 0, 6 at 50%, 1.5 at 100%. At 0 it never wears.
    const float passes = 24.0f * std::pow (1.5f / 24.0f, amount);
    wearRate = amount <= 0.001f ? 0.0f : 1.61f / passes;
}

float Tape::getCellWear (int cell) const noexcept
{
    const auto& d = damage[(size_t) wrap (cell, cellsPerTape)];
    return 0.25f * (d[0] + d[1] + d[2] + d[3]);
}

void Tape::process (float* left, float* right, float* lostL, float* lostR, int numSamples, const Transport& transport) noexcept
{
    const double quartersPerSample = transport.bpm / (60.0 * (double) sr);
    float* io[2] = { left, right };
    float* lost[2] = { lostL, lostR };

    for (int i = 0; i < numSamples; ++i)
    {
        const double cellPos = (transport.ppq + quartersPerSample * i) * 4.0;
        const int cellInSong = (int) std::floor (cellPos);
        const float frac = (float) (cellPos - cellInSong);
        const int subNow = std::min (subsPerCell - 1, (int) (frac * subsPerCell));

        if (transport.playing)
        {
            if (! wasPlaying)
            {
                // Playback (re)starts: line the play order up with the song again.
                playCell = cellInSong;
                startCell (cellInSong);
                sub = subNow;
                updateExpected();
                wasPlaying = true;
            }
            else if (cellInSong != songCell)
            {
                // Any change of cell counts as the next one in play order, so a loop
                // brace that jumps back keeps wearing the tape like a real loop would.
                closeSub();
                closeCell();
                ++playCell;
                startCell (cellInSong);
                sub = subNow;
                updateExpected();
            }
            else if (subNow != sub)
            {
                closeSub();
                sub = subNow;
                updateExpected();
            }
            ++energyCount;
        }
        else
        {
            wasPlaying = false;
        }

        // When the transport stops the tape stops: the damage fades out of the way.
        engaged += engageCoef * ((transport.playing ? 1.0f : 0.0f) - engaged);

        if (sampleCounter++ % controlInterval == 0)
            controlTick();

        const double tapePos = (double) wrap (playCell, cellsPerTape) + frac;
        tapePosition = (float) tapePos;
        const double u = tapePos - 0.5;
        const int c0 = (int) std::floor (u);
        const float t = (float) (u - c0);
        const auto& dA = damage[(size_t) wrap (c0, cellsPerTape)];
        const auto& dB = damage[(size_t) wrap (c0 + 1, cellsPerTape)];
        const double texturePos = tapePos / cellsPerTape * textureSize;

        std::array<float, numBands> gainTarget {};
        for (size_t b = 0; b < numBands; ++b)
        {
            const float d = std::clamp ((dA[b] + t * (dB[b] - dA[b])) * textureAt (texturePos + textureOffset[b]), 0.0f, 1.0f);
            const float g = fresh[b] ? 1.0f : 1.0f - 0.985f * d * std::sqrt (d);
            gainTarget[b] = 1.0f - engaged * (1.0f - g);
        }

        const float emphasisGain = 1.0f + 1.5f * friction;
        const float satMix = std::min (1.0f, 10.0f * friction + 10.0f * age);
        const float rough = friction * friction * friction;
        const float drive = 1.0f + (4.0f * friction + 6.0f * rough) * (0.5f + 0.5f * age) + 1.5f * age;
        const float dustDensity = 400.0f + 2600.0f * friction;
        const float dustScale = 0.25f * std::sqrt (sr / dustDensity);

        for (size_t c = 0; c < 2; ++c)
        {
            auto& ch = channels[c];
            const float x = io[c][i];

            // wear: four phase-aligned bands, each with its own damage
            float b0, rest1, b1, rest2, b2, b3;
            ch.split1.split (x, b0, rest1);
            ch.split2.split (rest1, b1, rest2);
            ch.split3.split (rest2, b2, b3);
            b0 = ch.lowAp3.process (ch.lowAp2.process (b0));
            b1 = ch.mid1Ap3.process (b1);
            const float bands[numBands] = { b0, b1, b2, b3 };

            float y = 0.0f, gone = 0.0f;
            for (size_t b = 0; b < numBands; ++b)
            {
                const float e = bands[b] * bands[b];
                energy[c][b] += e;
                fastEnergy[b] += 0.5f * e;

                ch.bandGain[b] += gainCoef * (gainTarget[b] - ch.bandGain[b]);
                y += ch.bandGain[b] * bands[b];
                gone += (1.0f - ch.bandGain[b]) * bands[b];
            }
            lost[c][i] = gone;

            // friction: wow and flutter move the whole tape, never a copy beside it
            ch.delay.push (y);
            y = ch.delay.read (tapeDelay + 1.0f + modulation); // read (1) is the newest sample

            // saturation between pre- and de-emphasis: the highs give first
            float lp, bp, hp;
            ch.emphasis.process (y, lp, bp, hp);
            float e = lp + ch.emphasis.damping() * bp + emphasisGain * hp;
            e += satMix * (saturate (e, drive) - e); // fully linear at zero friction
            ch.deemphasis.process (e, lp, bp, hp);
            y = lp + ch.deemphasis.damping() * bp + hp / emphasisGain;

            // friction softens the top, age darkens it
            ch.ageFilter.process (y, lp, bp, hp);
            y += ageMix * (lp - y);

            // dust: where the sound wore away, oxide grains remain in its shape,
            // sparse and crackly at low friction, a soft hiss at high
            const float g = std::abs (gone);
            ch.lostEnvelope += (g > ch.lostEnvelope ? lostAttack : lostRelease) * (g - ch.lostEnvelope);
            const float n = noise.nextBipolar();
            const float grain = noise.next01() < dustDensity / sr ? n * dustScale : 0.0f;
            const float dust = ch.dustLowpass.process (ch.dustHighpass.process (grain)) * ch.lostEnvelope * 0.6f * friction;

            // modulation noise: the tape's own grain riding the signal
            const float a = std::abs (y);
            ch.envelope += (a > ch.envelope ? envAttack : envRelease) * (a - ch.envelope);
            y += dust + n * ch.envelope * (0.01f * friction + 0.05f * rough + 0.05f * friction * age);

            io[c][i] = ch.dcBlock.process (y);
        }

        // An onset well above anything heard here 4 or 8 bars ago is new material:
        // it comes through at once, so a new section never loses its first hit.
        for (size_t b = 0; b < numBands; ++b)
        {
            const float e = fastEnergy[b];
            fastEnergy[b] = 0.0f;
            fastEnvelope[b] += (e > fastEnvelope[b] ? fastAttack : fastRelease) * (e - fastEnvelope[b]);
            subPeak[b] = std::max (subPeak[b], fastEnvelope[b]);
            if (transport.playing && ! fresh[b] && expectedPeakDb[b] > -200.0f)
            {
                const float db = 10.0f * std::log10 (fastEnvelope[b] + 1.0e-12f);
                if (db > -55.0f && db > expectedPeakDb[b] + 8.0f)
                    fresh[b] = true;
            }
        }
    }
}

void Tape::controlTick() noexcept
{
    // wow and flutter: nothing on a new tape at zero friction, a few cents of drift at
    // the middle, more as it ages and drags
    const float rough = friction * friction * friction;
    const float wowMs = 2.0f * age + 0.6f * friction + 1.4f * rough;
    const float flutterMs = (0.015f * friction + 0.035f * rough) * (1.0f + age);
    modulation = sr * 0.001f * (wowMs * wow.tick() + flutterMs * flutter.tick());

    // age follows the average damage, over a few seconds
    float mean = 0.0f;
    for (const auto& row : damage)
        for (auto v : row)
            mean += v;
    mean /= (float) (cellsPerTape * numBands);
    age += (float) controlInterval / (3.0f * sr) * (std::min (1.0f, 1.6f * mean) - age);

    // the top softens with friction and darkens with age
    const float darkness = std::min (1.0f, std::pow (age, 0.8f) + 0.15f * friction + 0.35f * friction * friction * friction);
    const float cutoff = expMap (20000.0f, 3800.0f, darkness);
    for (auto& c : channels)
        c.ageFilter.setCoefficients (cutoff, 0.7071f, sr);
    ageMix = std::min (1.0f, darkness * 25.0f); // a new tape at zero friction is fully transparent
}

void Tape::startCell (int cellInSong)
{
    songCell = cellInSong;
    subsHeard = 0;
    fresh.fill (false);
    for (auto& band : nowMean)
        band.fill (silentDb);
    for (auto& band : nowPeak)
        band.fill (silentDb);
    for (auto& c : energy)
        c.fill (0.0f);
    subPeak.fill (0.0f);
    energyCount = 0;
}

// Bank the 64th note that just ended. If it is far from both 4 and 8 bars ago in a
// band, that band is fresh tape for the rest of the cell.
void Tape::closeSub()
{
    if (sub >= 0 && energyCount > 0)
    {
        const int h = wrap (playCell, historyCells);
        const int h4 = wrap (playCell - cellsPerTape, historyCells);

        for (size_t b = 0; b < numBands; ++b)
        {
            const float mean = 10.0f * std::log10 ((energy[0][b] + energy[1][b]) / (2.0f * (float) energyCount) + 1.0e-12f);
            nowMean[b][(size_t) sub] = mean;
            nowPeak[b][(size_t) sub] = 10.0f * std::log10 (subPeak[b] + 1.0e-12f);

            const float now = std::max (mean, -70.0f);
            float nearest = 1.0e9f;
            for (int slot : { h4, h })
                if (historyValid[(size_t) slot])
                    nearest = std::min (nearest, std::abs (now - std::max (historyMean[(size_t) slot][b][(size_t) sub], -70.0f)));
            if (nearest < 1.0e8f && nearest > 6.0f && now > -60.0f)
                fresh[b] = true;
        }
        ++subsHeard;
    }

    for (auto& c : energy)
        c.fill (0.0f);
    subPeak.fill (0.0f);
    energyCount = 0;
}

// The cell that just ended: compare it with 4 and 8 bars ago, then wear or heal it.
void Tape::closeCell()
{
    const int h = wrap (playCell, historyCells);

    if (subsHeard < subsPerCell)
    {
        historyValid[(size_t) h] = false; // cut short by a jump: don't trust it
        return;
    }

    const int h4 = wrap (playCell - cellsPerTape, historyCells);
    const int h8 = h; // 8 bars ago lives in the slot this cell is about to take
    auto& cell = damage[(size_t) wrap (playCell, cellsPerTape)];

    for (size_t b = 0; b < numBands; ++b)
    {
        const float s4 = historyValid[(size_t) h4] ? similarity (nowMean[b], historyMean[(size_t) h4][b]) : -1.0f;
        const float s8 = historyValid[(size_t) h8] ? similarity (nowMean[b], historyMean[(size_t) h8][b]) : -1.0f;
        const float same = std::max (s4, s8);
        if (same < 0.0f)
            continue; // nothing to compare with, or nothing there to wear

        float& d = cell[b];
        if (same > 0.5f)
            d = std::min (1.0f, d + wearRate * fragility[(size_t) wrap (playCell, cellsPerTape)][b] * (same - 0.5f) * 2.0f * (0.25f + d));
        else
            d *= 1.0f - 1.5f * (0.5f - same);
    }

    historyMean[(size_t) h] = nowMean;
    historyPeak[(size_t) h] = nowPeak;
    historyValid[(size_t) h] = true;
}

// The loudest each band was around this moment 4 and 8 bars ago.
void Tape::updateExpected()
{
    const int h = wrap (playCell, historyCells);
    const int h4 = wrap (playCell - cellsPerTape, historyCells);
    for (size_t b = 0; b < numBands; ++b)
    {
        float m = -1000.0f;
        for (int slot : { h4, h })
            if (historyValid[(size_t) slot])
                for (int k = std::max (0, sub - 1); k <= std::min (subsPerCell - 1, sub + 1); ++k)
                    m = std::max (m, historyPeak[(size_t) slot][b][(size_t) k]);
        expectedPeakDb[b] = m;
    }
}

// 1 = the same (within about 1 dB per 64th), 0 = clearly different, -1 = silence.
float Tape::similarity (const std::array<float, subsPerCell>& a, const std::array<float, subsPerCell>& b) noexcept
{
    float distance = 0.0f, loudest = silentDb;
    for (size_t k = 0; k < subsPerCell; ++k)
    {
        distance += std::abs (std::max (a[k], -70.0f) - std::max (b[k], -70.0f));
        loudest = std::max ({ loudest, a[k], b[k] });
    }
    if (loudest < -60.0f)
        return -1.0f;
    distance /= (float) subsPerCell;
    return std::clamp (1.0f - (distance - 1.0f) / 5.0f, 0.0f, 1.0f);
}

// tanh with a small bias for even harmonics, scaled so quiet signals pass at unity.
float Tape::saturate (float x, float drive) noexcept
{
    constexpr float bias = 0.12f;
    const float tb = std::tanh (drive * bias);
    const float slope = drive * (1.0f - tb * tb);
    return (std::tanh (drive * (x + bias)) - tb) / slope;
}

float Tape::textureAt (double position) const noexcept
{
    position = std::fmod (position, (double) textureSize);
    const int i = (int) position;
    const float f = (float) (position - i);
    const float a = texture[(size_t) i];
    return a + f * (texture[(size_t) ((i + 1) % textureSize)] - a);
}

void Tape::buildTexture (uint32_t seed)
{
    // The oxide: 1/f noise along the tape (octaves of smoothed random), skewed so most
    // of it is sound and a few spots are very weak, scaled to a mean of 1.
    Rng r;
    r.seed (seed * 4099u + 7u);
    std::array<float, textureSize> sum {};
    float amp = 1.0f;
    for (int span = textureSize / 8; span >= 2; span /= 2, amp *= 0.7f)
    {
        std::vector<float> knots ((size_t) (textureSize / span + 1));
        for (auto& k : knots)
            k = r.nextBipolar();
        knots.back() = knots.front();
        for (int i = 0; i < textureSize; ++i)
        {
            const float s = smooth01 ((float) (i % span) / (float) span);
            const float k0 = knots[(size_t) (i / span)], k1 = knots[(size_t) (i / span + 1)];
            sum[(size_t) i] += amp * (k0 + s * (k1 - k0));
        }
    }
    const auto [lo, hi] = std::minmax_element (sum.begin(), sum.end());
    const float low = *lo, range = std::max (1.0e-6f, *hi - *lo);
    float mean = 0.0f;
    for (size_t i = 0; i < textureSize; ++i)
    {
        texture[i] = 2.2f * std::pow ((sum[i] - low) / range, 1.6f);
        mean += texture[i];
    }
    mean /= (float) textureSize;
    for (auto& v : texture)
        v /= mean;
    for (size_t b = 1; b < numBands; ++b)
        textureOffset[b] = (double) (r.next01() * 0.15f * textureSize);

    // Fragility per cell: weak stretches of tape rather than single cells, and the
    // highs shed first.
    Rng f;
    f.seed (seed * 977u + 1u);
    constexpr float bandScale[numBands] = { 0.55f, 0.75f, 0.9f, 1.0f };
    float stretch = f.next01();
    for (size_t c = 0; c < cellsPerTape; ++c)
    {
        stretch += 0.45f * (f.next01() - stretch);
        for (size_t b = 0; b < numBands; ++b)
        {
            const float u = std::clamp (0.6f * stretch + 0.4f * f.next01(), 0.0f, 1.0f);
            fragility[c][b] = bandScale[b] * (0.12f + 1.4f * std::pow (u, 2.2f));
        }
    }
}
} // namespace dendrite::dsp
