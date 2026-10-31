#include "Itch.h"

namespace dendrite
{
namespace
{
    constexpr float bandCentres[] = { 90.0f, 250.0f, 700.0f, 1800.0f, 4500.0f, 10000.0f };

    // A swell that rises over the first `rise` of its life and falls over the rest.
    float swell (float p, float rise) noexcept
    {
        if (p <= 0.0f || p >= 1.0f)
            return 0.0f;
        const float s = p < rise ? std::sin (0.5f * pi * p / rise) : std::cos (0.5f * pi * (p - rise) / (1.0f - rise));
        return s * s;
    }

    constexpr float historySeconds = 10.0f, captureSeconds = 1.6f;
} // namespace

//==============================================================================
void Listener::prepare (float sampleRate)
{
    for (size_t b = 0; b < filters.size(); ++b)
        filters[b].bandpass (std::min (bandCentres[b], 0.42f * sampleRate), 1.0f, sampleRate);
    envCoef = std::exp (-1.0f / (0.025f * sampleRate));
    reset();
}

void Listener::reset()
{
    for (auto& f : filters)
        f.clear();
    energy.fill (0.0f);
    written = 0;
    score = 0.0f;
}

void Listener::push (float mid) noexcept
{
    for (size_t b = 0; b < filters.size(); ++b)
    {
        const float y = filters[b].process (mid);
        energy[b] = y * y + envCoef * (energy[b] - y * y);
    }
}

void Listener::step (int stepsPerBar) noexcept
{
    auto& current = ring[(size_t) (written % ringFrames)];
    float loudest = -120.0f;
    for (size_t b = 0; b < bands; ++b)
    {
        current[b] = std::max (-80.0f, 10.0f * std::log10 (energy[b] + 1e-12f));
        loudest = std::max (loudest, current[b]);
    }
    ++written;

    float best = 0.0f;
    for (int bars : { 1, 2, 4 })
    {
        const int lag = bars * stepsPerBar;
        if (lag >= ringFrames || written <= lag)
            continue;
        const auto& then = ring[(size_t) ((written - 1 - lag) % ringFrames)];
        float diff = 0.0f;
        for (size_t b = 0; b < bands; ++b)
            diff += std::abs (current[b] - then[b]);
        best = std::max (best, std::exp (-(diff / (float) bands) / 3.5f));
    }
    if (loudest < -62.0f)
        best = 0.0f; // silence is not repetition

    score += 0.3f * (best - score);
}

//==============================================================================
void Heads::prepare (float sampleRate)
{
    sr = sampleRate;
    historyM.prepare ((int) (historySeconds * sr));
    historyS.prepare (16);
    capture.assign ((size_t) (captureSeconds * sr) + 8, 0.0f);
    flutter.prepare (sr, 0.32f, 0x81);
    rmsCoef = approach (0.03f, sr);
    vowel.bandpass (820.0f, 1.1f, sr);
    reset();
}

void Heads::reset()
{
    historyM.clear();
    onsets.fill ({});
    onsetHead = 0;
    lastOnset = levelEnv = 0.0f;
    now = 0;
    std::fill (capture.begin(), capture.end(), 0.0f);
    captureLength = copied = 0;
    captureFrom = 0;
    clock = -1.0;
    end = 0.0;
    lamps.fill (0.0f);
    throat1.clear();
    throat2.clear();
    vowel.clear();
    screamPair.clear();
    manipPair.clear();
    screamY1 = screamY2 = screamRms = 0.0f;
    haStart = -1.0;
    flutter.reset();
}

void Heads::listen (float m, float, float onset) noexcept
{
    historyM.push (m);
    ++now;
    if (onset > 0.5f && lastOnset <= 0.5f)
    {
        onsetHead = (onsetHead + 1) % onsets.size();
        onsets[onsetHead] = { now, 0.0f };
    }
    lastOnset = onset;
    // the strength of an onset is its peak over the next 30 ms
    auto& recent = onsets[onsetHead];
    if (recent.at >= 0 && now - recent.at < (int64_t) (0.03f * sr))
        recent.strength = std::max (recent.strength, std::abs (m));
}

void Heads::fire (float intensity, uint64_t seed, double gapSamples, double beatSamples, double barSamples) noexcept
{
    dice.reseed (seed);
    strength = clamp01 (intensity);

    // Grab the loudest accent of the last bar (the most recent one wins a tie).
    float best = 0.0f;
    int64_t from = now - (int64_t) beatSamples;
    for (const auto& o : onsets)
        if (o.at >= 0 && o.at >= now - (int64_t) barSamples && o.at <= now - (int64_t) (0.01f * sr)
            && o.strength >= best * 0.98f)
        {
            best = o.strength;
            from = o.at;
        }
    captureFrom = std::max (now - (int64_t) (historySeconds * sr) + 16, from - (int64_t) (0.004f * sr));
    captureLength = (int) std::clamp (beatSamples * (double) dice.range (0.75f, 1.0f), 0.12 * sr,
                                      (double) capture.size() - 8.0);
    copied = 0;
    capturePeak = 1.0e-3f;

    const double second = sr;
    const double gap = std::clamp (gapSamples, 0.25 * second, 2.5 * second);

    // A tape's heads are fixed; these drift a little, so no two itches line up the same.
    screamWin = { gap * dice.jitter (0.06f), std::clamp (1.5 * gap * dice.jitter (0.15f), 0.6 * second, 3.5 * second) };
    screamFrom = 480.0f * std::exp2 (dice.range (0.0f, 0.75f));
    screamRise = dice.range (0.4f, 0.9f) * (0.6f + 0.4f * strength);
    screamSide = dice.sign();
    screamFeedback = 0.955f + 0.025f * strength;
    throat1.clear();
    throat2.clear();
    screamPair.clear();
    screamY1 = screamY2 = screamRms = 0.0f;
    screamTick = 0;

    haRate = dice.range (4.8f, 6.0f);
    haSlow = dice.range (0.88f, 0.94f);
    haDecay = dice.range (0.74f, 0.86f);
    haSign = dice.sign();
    haMax = 4 + (int) std::lround (3.0f * strength);
    haCount = 0;
    double laughLength = 0.12 * second, rate = haRate;
    for (int k = 0; k < haMax; ++k, rate *= haSlow)
        laughLength += second / rate;
    laughWin = { 2.0 * gap * dice.jitter (0.05f), laughLength };
    nextHa = laughWin.start;
    haStart = -1.0;
    vowel.clear();

    manipFloor = 0.62f - 0.25f * strength * dice.range (0.7f, 1.0f);
    manipTurn = dice.sign() * dice.range (0.6f, 1.2f) * pi;
    manipWin = { 3.0 * gap * dice.jitter (0.04f), std::clamp ((double) captureLength / 0.72, 0.8 * second, 4.0 * second) };
    manipPos = 0.0f;
    manipPair.clear();

    end = std::max (manipWin.start + manipWin.length, laughWin.start + laughWin.length);
    clock = 0.0;
}

void Heads::copyCapture() noexcept
{
    // a little at a time, never more than has been heard
    for (int step = 0; step < 64 && copied < captureLength; ++step)
    {
        const int64_t source = captureFrom + copied;
        const int64_t back = historyM.written() - source;
        if (back < 1)
            break;
        const float fade = std::min (1.0f, (float) copied / (0.003f * sr))
                           * std::min (1.0f, (float) (captureLength - copied) / (0.02f * sr));
        const float v = historyM.at (back) * fade;
        capture[(size_t) copied] = v;
        capturePeak = std::max (capturePeak, std::abs (v));
        ++copied;
    }
}

float Heads::readCapture (float position) const noexcept
{
    if (position < 0.0f || position >= (float) (copied - 3))
        return 0.0f;
    const auto i = (size_t) position;
    const float f = position - (float) i;
    const float xm1 = i > 0 ? capture[i - 1] : 0.0f, x0 = capture[i], x1 = capture[i + 1], x2 = capture[i + 2];
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

void Heads::process (float& outM, float& outS, float& tapeM, float& tapeS, float& turn) noexcept
{
    outM = outS = tapeM = tapeS = turn = 0.0f;
    if (copied < captureLength)
        copyCapture();
    if (! busy())
    {
        lamps.fill (0.0f);
        return;
    }

    const double t = clock;
    clock += 1.0;
    const float level = 0.75f + 0.25f * strength;

    // ---- head 1: scream. The accent, rocked back and forth, excites a climbing throat.
    const float ps = screamWin.phase (t);
    const float e = swell (ps, 0.35f);
    lamps[0] = e;
    if (e > 0.0f)
    {
        if (screamTick-- <= 0)
        {
            screamTick = 15;
            const float f = std::min (screamFrom * std::exp2 (screamRise * smooth01 (ps)), 0.2f * sr);
            throat1.bandpass (f, 14.0f, sr);
            throat2.bandpass (2.01f * f, 16.0f, sr);
        }
        const float span = (float) std::max (32, captureLength - 4);
        float position = (float) std::fmod (t - screamWin.start, 2.0 * span);
        if (position > span)
            position = 2.0f * span - position; // forward, then back: an endless excitation
        const float x = readCapture (position);
        const float y1 = throat1.process (x + screamFeedback * softClip (screamY1, 1.5f));
        const float y2 = throat2.process (x + screamFeedback * softClip (screamY2, 1.5f));
        screamY1 = y1;
        screamY2 = y2;
        const float y = y1 + 0.5f * y2;
        screamRms += rmsCoef * (y * y - screamRms);
        const float agc = std::min (40.0f, 0.42f * capturePeak / (std::sqrt (screamRms) * 1.41f + 1.0e-5f));
        const float voice = softClip (agc * y, 1.2f) * e * level;

        float re = 0.0f, im = 0.0f;
        screamPair.process (voice, re, im);
        const float a = screamSide * 1.25f + 0.55f * flutter.next();
        const float m = 0.35f * re;
        const float s = 0.95f * (re * std::sin (a) + im * std::cos (a));
        outM += m;
        outS += s;
        tapeM += 0.25f * m;
        tapeS += 0.25f * s;
    }

    // ---- head 2: laugh. The accent's first syllable, again and again, lower and softer.
    if (t >= laughWin.start && t < laughWin.start + laughWin.length && t >= nextHa && haCount < haMax)
    {
        haStart = t;
        haLength = 0.075 * sr * dice.jitter (0.15f);
        haPos = (float) (0.002 * sr);
        haSpeed = std::exp2 (-(0.3f + 0.55f * (float) haCount) / 12.0f) * dice.jitter (0.01f);
        haAmp = std::pow (haDecay, (float) haCount) * dice.range (0.75f, 1.0f);
        if (dice.unit() < 0.8f)
            haSign = -haSign; // mostly alternating, never a pattern
        nextHa = t + sr / haRate * dice.jitter (0.08f);
        haRate *= haSlow;
        ++haCount;
    }
    lamps[1] = 0.0f;
    if (haStart >= 0.0 && t >= haStart && t < haStart + haLength)
    {
        const float w = swell ((float) ((t - haStart) / haLength), 0.15f) * haAmp;
        const float g = readCapture (haPos) * w;
        haPos += haSpeed;
        const float grain = (0.6f * g + 0.9f * vowel.process (g)) * level;
        const float m = 0.6f * grain, s = haSign * 0.75f * grain;
        outM += m;
        outS += s;
        tapeM += 0.3f * m;
        tapeS += 0.3f * s;
        lamps[1] = std::min (1.0f, 1.4f * w);
    }

    // ---- head 3: manipulate. A hand on the reel: pitch and time fall together, and the
    // sound swings around the head, taking the echo's space with it.
    const float pm = manipWin.phase (t);
    lamps[2] = 0.0f;
    if (pm > 0.0f && pm < 1.0f)
    {
        const float speed = manipFloor + (1.0f - manipFloor) * std::exp (-3.0f * pm);
        const float tail = std::min (1.0f, ((float) captureLength - manipPos) / (0.03f * sr));
        const float env = std::min (1.0f, pm / 0.02f) * std::min (1.0f, (1.0f - pm) / 0.15f) * std::max (0.0f, tail);
        const float y = readCapture (manipPos) * env * 0.9f * level;
        manipPos += speed;

        const float before = smooth01 ((float) ((t - 1.0 - manipWin.start) / manipWin.length));
        const float alpha = manipTurn * smooth01 (pm);
        float re = 0.0f, im = 0.0f;
        manipPair.process (y, re, im);
        const float m = re * (0.75f + 0.25f * std::cos (alpha));
        const float s = 0.9f * re * std::sin (alpha) + 0.35f * im * (1.0f - std::cos (alpha));
        outM += m;
        outS += s;
        tapeM += 0.55f * m;
        tapeS += 0.55f * s;
        turn = 0.5f * manipTurn * (smooth01 (pm) - before);
        lamps[2] = std::sin (pi * pm) * (0.5f + 0.5f * strength);
    }
}
} // namespace dendrite
