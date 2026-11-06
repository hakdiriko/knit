#include "Engine.h"

namespace dendrite
{
namespace
{
    uint64_t mixSeed (int64_t frame, int choice) noexcept
    {
        uint64_t z = (uint64_t) (frame + 0x100000) * 0x9E3779B97F4A7C15ull + 0xBF58476D1CE4E5B9ull * (unsigned) choice;
        z ^= z >> 31;
        z *= 0x94D049BB133111EBull;
        z ^= z >> 29;
        return z;
    }

    float coefFor (float seconds, float sr) noexcept { return std::exp (-1.0f / (seconds * sr)); }

    constexpr float ceilingGain = 0.966f; // -0.3 dBFS
} // namespace

void Engine::prepare (float sampleRate)
{
    sr = sampleRate;
    echo.prepare (sr);
    listener.prepare (sr);
    heads.prepare (sr);

    attackFilter.highpass (150.0f, 0.7071f, sr);
    fastUp = coefFor (0.0003f, sr);
    fastDown = coefFor (0.008f, sr);
    slowUp = coefFor (0.012f, sr);
    slowDown = coefFor (0.06f, sr);
    attackUp = coefFor (0.0005f, sr);
    attackDown = coefFor (0.03f, sr);
    limiterRelease = coefFor (0.12f, sr);
    meterCoef = 1.0f - coefFor (0.3f, sr);
    wetStep = 1.0f / (0.03f * sr); // a 30 ms crossfade for bypass
    reset();
}

void Engine::reset()
{
    echo.reset();
    listener.reset();
    heads.reset();
    attackFilter.clear();
    attackFast = attackSlow = attack = 0.0f;
    beat = 0.0;
    lastStep = -1;
    lastFrame = INT64_MIN;
    frameSum = 0.0f;
    frameSteps = boredFrames = 0;
    frameStartedIdle = true;
    quietFor = 1 << 30;
    wetNow = wetTarget;
    limiterEnv = 0.0f;
    corrLR = corrLL = corrRR = inPower = outPower = 0.0f;
    state = {};
}

void Engine::setControls (float feed, float space, bool throwHeld, int choice, bool bypassed) noexcept
{
    echo.setControls (feed, space, throwHeld && ! bypassed);
    itchChoice = std::clamp (choice, 0, frameChoices - 1);
    wetTarget = bypassed ? 0.0f : 1.0f;
}

void Engine::tickClock (double beatsPerSample, double frameBeats, double barBeats, int stepsPerBar,
                        double samplesPerBeat) noexcept
{
    const auto step = (int64_t) std::floor (beat * 4.0);
    if (step != lastStep)
    {
        lastStep = step;
        listener.step (stepsPerBar);
        frameSum += listener.repetition();
        ++frameSteps;
    }

    const auto frame = (int64_t) std::floor (beat / frameBeats);
    if (frame != lastFrame)
    {
        // A frame counts only if it was heard whole and the heads were quiet when it
        // began: after an itch there is always a full frame of relief.
        const bool counted = lastFrame != INT64_MIN && frameStartedIdle && frameSteps >= (int) (0.6 * frameBeats * 4.0);
        if (counted)
        {
            const float mean = frameSum / (float) frameSteps;
            if (mean > 0.72f)
            {
                ++boredFrames;
                if (itchChoice > 0)
                {
                    // The longer the loop goes on, the harder it itches.
                    const float intensity = 0.45f + 0.18f * (float) (boredFrames - 1) + 1.2f * (mean - 0.72f);
                    const double gapBeats = std::clamp (frameBeats / 4.0, 0.5, 2.0);
                    heads.fire (intensity, mixSeed (frame, itchChoice), gapBeats * samplesPerBeat, samplesPerBeat,
                                barBeats * samplesPerBeat);
                }
            }
            else if (mean < 0.5f)
            {
                boredFrames = 0;
            }
        }
        lastFrame = frame;
        frameSum = 0.0f;
        frameSteps = 0;
        // Relief: a frame counts only if it began with the heads quiet, and (for frames
        // shorter than a bar) at least a bar after they fell quiet.
        frameStartedIdle = ! heads.busy()
                           && (double) quietFor >= std::max (0.0, barBeats - frameBeats) * samplesPerBeat;
    }
    beat += beatsPerSample;
    quietFor = heads.busy() ? 0 : quietFor + 1;
}

void Engine::process (float* left, float* right, int numSamples, const HostTime& time) noexcept
{
    const double bpm = std::clamp (time.bpm, 30.0, 300.0);
    const double barBeats = time.barBeats > 0.5 ? time.barBeats : 4.0;
    if (time.playing)
        beat = time.ppq;
    echo.setTempo (bpm);

    const double beatsPerSample = bpm / 60.0 / sr;
    const double samplesPerBeat = sr * 60.0 / bpm;
    const int stepsPerBar = std::clamp ((int) std::lround (barBeats * 4.0), 4, 64);
    const double frameBeats = (itchChoice > 0 ? (double) frameBars[itchChoice] : 4.0) * barBeats;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i], r = right[i];
        const float mid = 0.5f * (l + r), side = 0.5f * (l - r);

        listener.push (mid);
        tickClock (beatsPerSample, frameBeats, barBeats, stepsPerBar, samplesPerBeat);

        // onsets, for the heads' grab
        const float level = std::abs (attackFilter.process (mid));
        attackFast = level + (level > attackFast ? fastUp : fastDown) * (attackFast - level);
        attackSlow = level + (level > attackSlow ? slowUp : slowDown) * (attackSlow - level);
        const float onset = clamp01 ((attackFast / (attackSlow + 1e-6f) - 1.3f) * 1.25f);
        attack = onset + (onset > attack ? attackUp : attackDown) * (attack - onset);
        heads.listen (mid, side, attack);

        float hm = 0.0f, hs = 0.0f, tm = 0.0f, ts = 0.0f, turn = 0.0f;
        heads.process (hm, hs, tm, ts, turn);
        if (std::abs (turn) > 0.0f)
            echo.turnSpace (turn);

        float wm = 0.0f, ws = 0.0f;
        echo.process (mid, side, tm, ts, wm, ws);

        // The dry passes untouched; bypass fades the echo and the heads out.
        wetNow += std::clamp (wetTarget - wetNow, -wetStep, wetStep);
        const float em = wetNow * (wm + hm), es = wetNow * (ws + hs);
        float outL = l + em + es;
        float outR = r + em - es;

        // Safety limiter: instant attack, 120 ms release, -0.3 dBFS. A seatbelt, not an effect.
        const float peak = std::max (std::abs (outL), std::abs (outR));
        limiterEnv = peak > limiterEnv ? peak : peak + limiterRelease * (limiterEnv - peak);
        if (limiterEnv > ceilingGain)
        {
            const float g = ceilingGain / limiterEnv;
            outL *= g;
            outR *= g;
        }
        left[i] = outL;
        right[i] = outR;

        corrLR += meterCoef * (outL * outR - corrLR);
        corrLL += meterCoef * (outL * outL - corrLL);
        corrRR += meterCoef * (outR * outR - corrRR);
        inPower += meterCoef * (mid * mid + side * side - inPower);
        outPower += meterCoef * (0.5f * (outL * outL + outR * outR) - outPower);
    }

    const float denom = std::sqrt (corrLL * corrRR);
    state.echo = echo.view();
    state.correlation = denom > 1e-9f ? corrLR / denom : 1.0f;
    state.inLevel = std::sqrt (std::max (0.0f, inPower));
    state.outLevel = std::sqrt (std::max (0.0f, outPower));
    state.repetition = listener.repetition();
    state.frameProgress = (float) (std::fmod (std::max (0.0, beat), frameBeats) / frameBeats);
    state.boredom = std::min (1.0f, (float) boredFrames / 3.0f);
    state.itching = heads.busy();
    for (int h = 0; h < Heads::count; ++h)
        state.lamps[(size_t) h] = heads.lamp (h);
    state.bpm = bpm;
    state.hostPlaying = time.playing;
}
} // namespace dendrite
