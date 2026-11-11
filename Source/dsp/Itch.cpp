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

    // How loud each head plays against the accent it was given, matched by ear-weighted
    // level on drums, chords and a dub loop (tools/render/heads.cpp): no head is the
    // quiet one any more.
    constexpr float headGain[Heads::count] = { 0.85f, 2.7f, 1.4f, 2.6f, 5.5f };

    // How much of each head goes onto the echo's tape as well.
    constexpr float toTape[Heads::count] = { 0.25f, 0.3f, 0.55f, 0.3f, 0.2f };

    // the spherical head
    constexpr float headRadius = 0.0875f, soundSpeed = 343.0f;
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
    stepPeak = 0.0f;
    loudestLong = -120.0f;
    written = 0;
    quiet = 1 << 20;
    score = 0.0f;
}

void Listener::push (float mid) noexcept
{
    for (size_t b = 0; b < filters.size(); ++b)
    {
        const float y = filters[b].process (mid);
        energy[b] = y * y + envCoef * (energy[b] - y * y);
        stepPeak = std::max (stepPeak, energy[b]);
    }
}

void Listener::step (int stepsPerBar) noexcept
{
    auto& current = ring[(size_t) (written % ringFrames)];
    for (size_t b = 0; b < bands; ++b)
        current[b] = std::max (-80.0f, 10.0f * std::log10 (energy[b] + 1e-12f));
    ++written;

    // Whether anything sounded during this sixteenth (not only at its edge, which a short
    // stab can fall between).
    const float loudest = 10.0f * std::log10 (stepPeak + 1e-12f);
    stepPeak = 0.0f;

    // Silence is judged against the track's own recent peaks (40 dB under them, never
    // under -90 dB), so a quiet track is heard as well as a loud one.
    loudestLong = std::max (loudest, loudestLong - 40.0f / (float) (8 * stepsPerBar)); // falls 40 dB over 8 bars
    if (loudest < std::max (-90.0f, loudestLong - 40.0f))
    {
        // A rest inside the loop is not news; a silence longer than two bars is the end.
        quiet = std::min (quiet + 1, 1 << 20);
        if (quiet > 2 * stepsPerBar)
            score += 0.3f * (0.0f - score);
        return;
    }
    quiet = 0;

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
    score += 0.3f * (best - score);
}

//==============================================================================
void Binaural::Shadow::set (float alpha, float b) noexcept
{
    const float n = 1.0f / (1.0f + b);
    b0 = (1.0f + alpha * b) * n;
    b1 = (1.0f - alpha * b) * n;
    a1 = (1.0f - b) * n;
}

void Binaural::prepare (float sampleRate)
{
    sr = sampleRate;
    line.prepare ((int) (0.0015f * sr) + 8);
    beta = sr * headRadius / soundSpeed; // the bilinear transform of Brown and Duda's head shadow
    backL.setCutoff (3200.0f, sr);
    backR.setCutoff (3200.0f, sr);
    clear();
}

void Binaural::clear() noexcept
{
    line.clear();
    shadowL = {};
    shadowR = {};
    backL.clear();
    backR.clear();
    delayL = delayR = targetL = targetR = 0.0f;
    gainL = gainR = 1.0f;
    behind = 0.0f;
    tick = 0;
    primed = false;
}

void Binaural::aim (float azimuth, float near) noexcept
{
    // Woodworth: the far ear hears it later by (a / c) (theta + sin theta)
    const float lateral = std::sin (azimuth);
    const float theta = std::asin (std::clamp (lateral, -1.0f, 1.0f));
    const float itd = headRadius / soundSpeed * (theta + lateral) * sr; // > 0: the right ear first
    targetL = std::max (0.0f, itd);
    targetR = std::max (0.0f, -itd);

    // Brown and Duda: the high-frequency gain at an ear, from the angle between the source
    // and that ear (2 facing it, 0.1 at 150 degrees, a bright spot straight behind)
    const auto alpha = [] (float angle) noexcept {
        constexpr float least = 0.1f, darkest = 150.0f * pi / 180.0f;
        return (1.0f + 0.5f * least) + (1.0f - 0.5f * least) * std::cos (angle / darkest * pi);
    };
    shadowL.set (alpha (std::abs (wrapAngle (azimuth + 0.5f * pi))), beta);
    shadowR.set (alpha (std::abs (wrapAngle (azimuth - 0.5f * pi))), beta);

    // from behind, the pinna takes the edge off
    behind = 0.65f * std::max (0.0f, -std::cos (azimuth));

    // close to an ear, the other ear hears much less
    const float shade = clamp01 (near) * 0.7f * std::abs (lateral);
    const float body = 1.0f - 0.15f * behind;
    gainL = body * (lateral > 0.0f ? 1.0f - shade : 1.0f);
    gainR = body * (lateral < 0.0f ? 1.0f - shade : 1.0f);

    if (! primed)
    {
        delayL = targetL;
        delayR = targetR;
        primed = true;
    }
}

void Binaural::process (float x, float azimuth, float near, float& left, float& right) noexcept
{
    if (tick-- <= 0)
    {
        tick = 15;
        aim (azimuth, near);
    }
    delayL += 0.02f * (targetL - delayL);
    delayR += 0.02f * (targetR - delayR);
    line.push (x);
    float l = shadowL.process (line.read (1.0f + delayL));
    float r = shadowR.process (line.read (1.0f + delayR));
    l = lerp (l, backL.lowpass (l), behind);
    r = lerp (r, backR.lowpass (r), behind);
    left = gainL * l;
    right = gainR * r;
}

//==============================================================================
void Heads::prepare (float sampleRate)
{
    fadeStep = 1.0f / (0.1f * sampleRate);
    sr = sampleRate;
    historyM.prepare ((int) (historySeconds * sr));
    capture.assign ((size_t) (captureSeconds * sr) + 8, 0.0f);
    air.assign (capture.size(), 0.0f);
    airFilter1.highpass (1700.0f, 0.7071f, sr);
    airFilter2.highpass (1700.0f, 0.7071f, sr);
    flutter.prepare (sr, 0.32f, 0x81);
    rmsCoef = approach (0.03f, sr);
    vowel.bandpass (820.0f, 1.1f, sr);
    for (auto* b : { &screamPlace, &laughPlace, &manipPlace, &ghostPlace })
        b->prepare (sr);
    for (auto& g : grains)
        g.place.prepare (sr);
    reset();
}

void Heads::reset()
{
    historyM.clear();
    onsets.fill ({});
    onsetHead = 0;
    lastOnset = 0.0f;
    now = 0;
    std::fill (capture.begin(), capture.end(), 0.0f);
    std::fill (air.begin(), air.end(), 0.0f);
    captureLength = copied = 0;
    captureFrom = 0;
    clock = -1.0;
    end = 0.0;
    fade = 1.0f;
    stopping = false;
    lamps.fill (0.0f);
    probeL.fill (0.0f);
    probeR.fill (0.0f);
    for (auto& w : windows)
        w = {};
    lastSet = 0;
    throat1.clear();
    throat2.clear();
    vowel.clear();
    airFilter1.clear();
    airFilter2.clear();
    screamY1 = screamY2 = screamRms = 0.0f;
    haStart = -1.0;
    flutter.reset();
    for (auto* b : { &screamPlace, &laughPlace, &manipPlace, &ghostPlace })
        b->clear();
    for (auto& g : grains)
    {
        g.sounding = false;
        g.place.clear();
    }
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
    fade = 1.0f;
    stopping = false;

    // Grab one of the strongest accents of the last bar: any within 3 dB of the loudest, so
    // a drum loop doesn't hand the heads the same kick every time.
    const auto inBar = [&] (const Onset& o)
    { return o.at >= 0 && o.at >= now - (int64_t) barSamples && o.at <= now - (int64_t) (0.01f * sr); };
    float best = 0.0f;
    for (const auto& o : onsets)
        if (inBar (o))
            best = std::max (best, o.strength);
    int64_t from = now - (int64_t) beatSamples;
    int candidates = 0;
    for (const auto& o : onsets)
        if (inBar (o) && o.strength >= 0.7f * best && best > 0.0f)
            ++candidates;
    if (candidates > 0)
    {
        int pick = std::min (candidates - 1, (int) (dice.unit() * (float) candidates));
        for (const auto& o : onsets)
            if (inBar (o) && o.strength >= 0.7f * best && pick-- == 0)
            {
                from = o.at;
                break;
            }
    }
    captureFrom = std::max (now - (int64_t) (historySeconds * sr) + 16, from - (int64_t) (0.004f * sr));
    captureLength = (int) std::clamp (beatSamples * (double) dice.range (0.75f, 1.0f), 0.12 * sr,
                                      (double) capture.size() - 8.0);
    copied = 0;
    capturePeak = 1.0e-3f;
    airFilter1.clear();
    airFilter2.clear();

    const double second = sr;
    const double gap = std::clamp (gapSamples, 0.2 * second, 2.5 * second);

    // Draw three of the five heads, in an order of their own; never the same three twice
    // running.
    std::array<int, count> order { 0, 1, 2, 3, 4 };
    int set = 0;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        for (int i = count - 1; i > 0; --i)
            std::swap (order[(size_t) i], order[(size_t) std::min (i, (int) (dice.unit() * (float) (i + 1)))]);
        set = 0;
        for (int k = 0; k < perItch; ++k)
            set |= 1 << order[(size_t) k];
        if (set != lastSet)
            break;
    }
    lastSet = set;
    for (auto& w : windows)
        w = {};

    end = 0.0;
    for (int slot = 0; slot < perItch; ++slot)
    {
        const auto head = (Head) order[(size_t) slot];
        // A tape's heads are fixed; these drift a little, so no two itches line up the same.
        const double start = (double) (slot + 1) * gap * dice.jitter (0.05f);
        double length = second;

        switch (head)
        {
            case scream:
                length = std::clamp (1.5 * gap * dice.jitter (0.15f), 0.6 * second, 3.5 * second);
                screamFrom = 480.0f * std::exp2 (dice.range (0.0f, 0.75f));
                screamRise = dice.range (0.4f, 0.9f) * (0.6f + 0.4f * strength);
                screamFeedback = 0.955f + 0.025f * strength;
                screamAz = dice.range (-pi, pi);
                screamTurns = dice.sign() * dice.range (1.0f, 1.8f) * (0.8f + 0.4f * strength);
                throat1.clear();
                throat2.clear();
                screamPlace.clear();
                screamY1 = screamY2 = screamRms = 0.0f;
                screamTick = 0;
                break;

            case laugh:
            {
                haRate = dice.range (4.8f, 6.0f);
                haSlow = dice.range (0.88f, 0.94f);
                haDecay = dice.range (0.74f, 0.86f);
                haSide = dice.sign();
                haMax = 4 + (int) std::lround (3.0f * strength);
                haCount = 0;
                length = 0.12 * second;
                double rate = haRate;
                for (int k = 0; k < haMax; ++k, rate *= haSlow)
                    length += second / rate;
                nextHa = start;
                haStart = -1.0;
                vowel.clear();
                laughPlace.clear();
                break;
            }

            case manipulate:
                manipFloor = 0.62f - 0.25f * strength * dice.range (0.7f, 1.0f);
                manipTurn = dice.sign() * dice.range (0.6f, 1.2f) * pi;
                manipAz = dice.range (-0.5f, 0.5f);
                length = std::clamp ((double) captureLength / 0.72, 0.8 * second, 4.0 * second);
                manipPos = 0.0f;
                manipPlace.clear();
                break;

            case swarm:
            {
                swarmBase = std::clamp (2.0f * (float) (second / beatSamples), 2.0f, 8.0f); // eighth notes
                swarmSpread = dice.range (0.12f, 0.2f);
                swarmGrainLength = std::clamp (0.22f / swarmBase, 0.03f, 0.07f);
                swarmSync = 0.0f;
                swarmTick = 0;
                length = std::clamp (2.0 * gap * dice.jitter (0.1f), 1.2 * second, 4.0 * second);
                for (auto& g : grains)
                {
                    g.phase = dice.unit();
                    g.rate = swarmBase * dice.jitter (swarmSpread);
                    g.azimuth = dice.range (-pi, pi);
                    g.sounding = false;
                    g.place.clear();
                }
                break;
            }

            case ghost:
            {
                ghostSpeed = dice.range (0.82f, 0.95f);
                ghostFrom = dice.sign() * dice.range (0.4f, 0.6f) * pi;
                ghostPos = 0.0f;
                length = std::min (3.0 * second, (double) captureLength / ghostSpeed + 0.24 * second);
                // a velvet-noise sequence: one sparse tap per slot, random sign, decaying
                const float span = 0.22f * sr, pitch = span / (float) velvetTaps, tauSamples = 0.07f * sr;
                float energy = 0.0f;
                for (int k = 0; k < velvetTaps; ++k)
                {
                    velvetAt[(size_t) k] = (int) ((float) k * pitch + dice.unit() * pitch);
                    velvetWeight[(size_t) k] = dice.sign() * std::exp (-(float) velvetAt[(size_t) k] / tauSamples);
                    energy += velvetWeight[(size_t) k] * velvetWeight[(size_t) k];
                }
                const float norm = 1.0f / std::sqrt (std::max (energy, 1.0e-9f));
                for (auto& w : velvetWeight)
                    w *= norm;
                ghostPlace.clear();
                break;
            }
        }

        windows[(size_t) head] = { start, length, true };
        end = std::max (end, start + length);
    }
    end += 0.02 * second; // the places' last samples
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
        air[(size_t) copied] = airFilter2.process (airFilter1.process (v));
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

float Heads::readAir (float position) const noexcept
{
    if (position < 0.0f || position >= (float) (copied - 2))
        return 0.0f;
    const auto i = (size_t) position;
    const float f = position - (float) i;
    return air[i] + f * (air[i + 1] - air[i]);
}

void Heads::process (float& outM, float& outS, float& tapeM, float& tapeS, float& turn) noexcept
{
    outM = outS = tapeM = tapeS = turn = 0.0f;
    probeL.fill (0.0f);
    probeR.fill (0.0f);
    if (copied < captureLength)
        copyCapture();
    if (! busy())
    {
        lamps.fill (0.0f);
        return;
    }

    if (stopping)
    {
        // itch turned off: the heads fade out within 100 ms
        fade -= fadeStep;
        if (fade <= 0.0f)
        {
            fade = 0.0f;
            clock = end;
            lamps.fill (0.0f);
            return;
        }
    }
    const double t = clock;
    clock += 1.0;
    const float level = 0.75f + 0.25f * strength;

    for (int k = 0; k < count; ++k)
    {
        if (! windows[(size_t) k].on)
        {
            lamps[(size_t) k] = 0.0f;
            continue;
        }
        float l = 0.0f, r = 0.0f;
        switch ((Head) k)
        {
            case scream: playScream (t, level, l, r); break;
            case laugh: playLaugh (t, level, l, r); break;
            case manipulate: playManipulate (t, level, l, r, turn); break;
            case swarm: playSwarm (t, level, l, r); break;
            case ghost: playGhost (t, level, l, r); break;
        }
        // levelled, and never louder at its peaks than the accent it came from
        const float ceiling = 0.85f * capturePeak, drive = 1.0f / ceiling;
        l = softClip (l * headGain[k], drive);
        r = softClip (r * headGain[k], drive);
        probeL[(size_t) k] = l;
        probeR[(size_t) k] = r;
        const float m = 0.5f * (l + r), s = 0.5f * (l - r);
        outM += m;
        outS += s;
        tapeM += toTape[k] * m;
        tapeS += toTape[k] * s;
    }
    if (stopping)
    {
        outM *= fade;
        outS *= fade;
        tapeM *= fade;
        tapeS *= fade;
        turn *= fade;
        for (auto& l : lamps)
            l *= fade;
    }
}

void Heads::stop() noexcept
{
    if (busy())
        stopping = true;
}

// ---- scream: the accent, rocked back and forth, excites a climbing throat that circles
// your head, faster as it climbs.
void Heads::playScream (double t, float level, float& l, float& r) noexcept
{
    const auto& w = windows[scream];
    const float ps = w.phase (t);
    const float e = swell (ps, 0.35f);
    lamps[scream] = e;
    if (e <= 0.0f)
        return;
    if (screamTick-- <= 0)
    {
        screamTick = 15;
        const float f = std::min (screamFrom * std::exp2 (screamRise * smooth01 (ps)), 0.2f * sr);
        throat1.bandpass (f, 14.0f, sr);
        throat2.bandpass (2.01f * f, 16.0f, sr);
    }
    const float span = (float) std::max (32, captureLength - 4);
    float position = (float) std::fmod (t - w.start, 2.0 * span);
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

    const float azimuth = screamAz + tau * screamTurns * std::pow (clamp01 (ps), 1.4f) + 0.25f * flutter.next();
    screamPlace.process (voice, azimuth, 0.6f * e, l, r);
}

// ---- laugh: the accent's first syllable, again and again, lower and softer, hopping
// from one side of your head to the other.
void Heads::playLaugh (double t, float level, float& l, float& r) noexcept
{
    const auto& w = windows[laugh];
    if (t >= w.start && t < w.start + w.length && t >= nextHa && haCount < haMax)
    {
        haStart = t;
        haLength = 0.09 * sr * dice.jitter (0.15f);
        haPos = (float) (0.002 * sr);
        haSpeed = std::exp2 (-(0.3f + 0.55f * (float) haCount) / 12.0f) * dice.jitter (0.01f);
        haAmp = std::pow (haDecay, (float) haCount) * dice.range (0.75f, 1.0f);
        if (dice.unit() < 0.8f)
            haSide = -haSide; // mostly alternating, never a pattern
        haAz = haSide * dice.range (0.9f, 2.1f); // beside you, sometimes a little behind
        nextHa = t + sr / haRate * dice.jitter (0.08f);
        haRate *= haSlow;
        ++haCount;
    }
    lamps[laugh] = 0.0f;
    if (haStart >= 0.0 && t >= haStart && t < haStart + haLength + 0.002 * sr)
    {
        const float wv = swell ((float) ((t - haStart) / haLength), 0.15f) * haAmp;
        const float g = readCapture (haPos) * wv;
        haPos += haSpeed;
        const float grain = (0.6f * g + 0.9f * vowel.process (g)) * level;
        laughPlace.process (grain, haAz, 0.5f, l, r);
        lamps[laugh] = std::min (1.0f, 1.4f * wv);
    }
}

// ---- manipulate: a hand on the reel. Pitch and time fall together, and the sound
// swings around your head, taking the echo's space with it.
void Heads::playManipulate (double t, float level, float& l, float& r, float& turn) noexcept
{
    const auto& w = windows[manipulate];
    const float pm = w.phase (t);
    lamps[manipulate] = 0.0f;
    if (pm <= 0.0f || pm >= 1.0f)
        return;
    const float speed = manipFloor + (1.0f - manipFloor) * std::exp (-3.0f * pm);
    const float tail = std::min (1.0f, ((float) captureLength - manipPos) / (0.03f * sr));
    const float env = std::min (1.0f, pm / 0.02f) * std::min (1.0f, (1.0f - pm) / 0.15f) * std::max (0.0f, tail);
    const float y = readCapture (manipPos) * env * 0.9f * level;
    manipPos += speed;

    const float before = smooth01 ((float) ((t - 1.0 - w.start) / w.length));
    const float swing = manipTurn * smooth01 (pm);
    manipPlace.process (y, manipAz + swing, 0.2f, l, r);
    turn = 0.5f * manipTurn * (smooth01 (pm) - before);
    lamps[manipulate] = std::sin (pi * pm) * (0.5f + 0.5f * strength);
}

// ---- swarm: a dozen pulses, each at its own rate and place around you, fire grains of
// the attack. Coupled (Kuramoto), they pull each other into step, a beat grid draws them
// onto the beat, and the cloud closes into one hit in front of you.
void Heads::playSwarm (double t, float level, float& l, float& r) noexcept
{
    const auto& w = windows[swarm];
    const float p = w.phase (t);
    lamps[swarm] = 0.0f;
    const bool alive = p >= 0.0f && p < 1.0f;

    if (alive && swarmTick-- <= 0)
    {
        constexpr int every = 8;
        swarmTick = every - 1;
        float cx = 0.0f, cy = 0.0f;
        for (const auto& g : grains)
        {
            cx += std::cos (tau * g.phase);
            cy += std::sin (tau * g.phase);
        }
        cx /= (float) swarmGrains;
        cy /= (float) swarmGrains;
        swarmSync = std::sqrt (cx * cx + cy * cy);
        const float mean = std::atan2 (cy, cx) / tau;

        const float spreadHz = swarmBase * swarmSpread;
        const float coupling = 4.0f * spreadHz * smooth01 (p / 0.6f);
        const float pace = 2.0f * spreadHz * smooth01 ((p - 0.25f) / 0.45f);
        const float grid = (float) ((t - w.start) / sr) * swarmBase;
        const float dt = (float) every / sr;
        for (auto& g : grains)
        {
            const float pull = coupling * swarmSync * std::sin (tau * (mean - g.phase)) + pace * std::sin (tau * (grid - g.phase));
            g.phase += (g.rate + pull) * dt;
            if (g.phase < 0.0f)
                g.phase += 1.0f;
            if (g.phase >= 1.0f)
            {
                g.phase -= std::floor (g.phase);
                if (p < 0.88f)
                {
                    g.sounding = true;
                    g.age = 0;
                    g.length = std::max (64, (int) (swarmGrainLength * sr * dice.jitter (0.2f)));
                    g.position = 0.002f * sr;
                    g.speed = dice.jitter (0.03f);
                    // scattered grains are few at a time, so each is louder; in step they add up
                    g.amp = level / (float) swarmGrains * (1.0f + 3.0f * (1.0f - swarmSync));
                }
            }
        }
    }

    // the cloud closes in front of you as it locks
    const float close = smooth01 ((swarmSync - 0.55f) / 0.4f);
    bool any = false;
    for (auto& g : grains)
    {
        if (! g.sounding)
            continue;
        any = true;
        const float s = std::sin (pi * (float) g.age / (float) g.length);
        const float x = readCapture (g.position) * s * s * g.amp;
        g.position += g.speed;
        if (++g.age >= g.length)
            g.sounding = false;
        float gl = 0.0f, gr = 0.0f;
        g.place.process (x, g.azimuth * (1.0f - close), 0.25f, gl, gr);
        l += gl;
        r += gr;
    }
    if (alive || any)
        lamps[swarm] = alive ? 0.25f + 0.75f * swarmSync : 0.25f;
}

// ---- ghost: the accent's air, smeared by velvet noise into a whisper at one ear, that
// passes behind your head to the other.
void Heads::playGhost (double t, float level, float& l, float& r) noexcept
{
    const auto& w = windows[ghost];
    const float p = w.phase (t);
    lamps[ghost] = 0.0f;
    if (p <= 0.0f || p >= 1.0f)
        return;
    float y = 0.0f;
    for (int k = 0; k < velvetTaps; ++k)
        y += velvetWeight[(size_t) k] * readAir (ghostPos - (float) velvetAt[(size_t) k]);
    ghostPos += ghostSpeed;
    const float env = std::min (1.0f, p / 0.06f) * std::min (1.0f, (1.0f - p) / 0.25f);
    y *= env * level;
    const float azimuth = ghostFrom + (ghostFrom > 0.0f ? pi : -pi) * smooth01 (p);
    ghostPlace.process (y, azimuth, 0.9f, l, r);
    lamps[ghost] = env;
}
} // namespace dendrite
