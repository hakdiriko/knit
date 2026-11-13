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

    // How much of the echo fits next to the dry under the ceiling (0..1). A dry that is
    // already over the ceiling is the host's to handle: the echo then steps aside.
    float wetRoom (float dry, float wet) noexcept
    {
        const float sum = std::abs (dry + wet);
        if (sum <= ceilingGain)
            return 1.0f;
        const float allowed = std::max (0.0f, ceilingGain - std::abs (dry));
        return std::clamp (allowed / (std::abs (wet) + 1.0e-12f), 0.0f, 1.0f);
    }
} // namespace

void Engine::prepare (float sampleRate)
{
    sr = sampleRate;
    echo.prepare (sr);
    listener.prepare (sr);
    heads.prepare (sr);
    mind.rest();
    hand.prepare (sampleRate);
    fly.prepare (sampleRate);

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
    mind.rest(); // the creature keeps its life
    hand.rest();
    fly.rest();
    stepOnset = 0.0f;
    throwStarted = false;
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
    dryNow = dryTarget;
    limiterEnv = 1.0f;
    corrLR = corrLL = corrRR = inPower = outPower = 0.0f;
    state = {};
}

void Engine::setControls (float feed, float space, bool throwHeld, int choice, bool bypassed) noexcept
{
    echo.setControls (feed, space, throwHeld && ! bypassed);
    feedNow = feed;
    spaceNow = space;
    const bool held = throwHeld && ! bypassed;
    if (held && ! throwWasHeld)
        throwStarted = true;
    throwWasHeld = held;
    const int next = std::clamp (choice, 0, frameChoices - 1);
    if (next == 0 && itchChoice != 0)
    {
        heads.stop(); // off means off, even in the middle of an itch
        boredFrames = 0;
    }
    itchChoice = next;
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

        // the creature breathes once per sixteenth
        Creature::Senses s;
        s.bandsDb = listener.lastBands();
        s.sounding = listener.silentSteps() == 0;
        s.onset = stepOnset;
        s.repetition = listener.repetition();
        s.feed = feedNow;
        s.space = spaceNow;
        s.throwStarted = throwStarted;
        s.stepsPerBar = stepsPerBar;
        s.stepInBar = (int) (((step % stepsPerBar) + stepsPerBar) % stepsPerBar);
        s.stepSeconds = (float) (0.25 * samplesPerBeat / sr);
        mind.step (s);
        stepOnset = 0.0f;
        throwStarted = false;
    }

    const auto frame = (int64_t) std::floor (beat / frameBeats);
    if (frame != lastFrame)
    {
        // A frame counts only if it was heard whole and the heads were quiet when it
        // began: after an itch there is always a full frame of relief.
        // It also needs something to have sounded in the last bar, so that the heads have a
        // hit to grab: a short frame that falls in a loop's rest still counts.
        const bool counted = lastFrame != INT64_MIN && frameStartedIdle && frameSteps >= (int) (0.6 * frameBeats * 4.0)
                             && listener.silentSteps() < stepsPerBar;
        if (counted)
        {
            const float mean = frameSum / (float) frameSteps;
            // the creature decides how much is too much (1.2: always 0.72)
            const float enough = diceItch ? 0.72f : mind.threshold();
            if (mean > enough)
            {
                ++boredFrames;
                if (itchChoice > 0 && ! flyItch)
                {
                    // The longer the loop goes on, the harder it itches.
                    const auto gesture = diceItch ? Gesture::dice (mixSeed (frame, itchChoice), heads.lastSetMask())
                                                  : mind.itch (beat, itchChoice);
                    const float intensity = 0.45f + 0.18f * (float) (boredFrames - 1) + 1.2f * (mean - enough) + gesture.lift;
                    const double gapBeats = std::clamp (frameBeats / 4.0, 0.5, 2.0);
                    if (handItch)
                    {
                        // the motif: one gesture on the desk, a bar to four bars long
                        if (handGenome != mind.life().genome)
                        {
                            handGenome = mind.life().genome;
                            hand.grow (handGenome);
                        }
                        const double motifBeats = std::clamp (frameBeats, barBeats, 4.0 * barBeats);
                        const float reach = clamp01 (0.3f + 0.45f * mind.arousal() + 0.25f * mind.familiarity()
                                                     + 0.3f * (intensity - 0.45f));
                        hand.start (gesture, motifBeats * samplesPerBeat, barBeats * samplesPerBeat, emptyBandHz(), reach,
                                    mind.brightness());
                    }
                    else
                    {
                        heads.fire (gesture, intensity, gapBeats * samplesPerBeat, samplesPerBeat, barBeats * samplesPerBeat);
                    }
                    echo.setTurnHeal (gesture.heal);
                    record.count += 1;
                    record.gesture = gesture;
                    record.ppq = beat;
                    record.intensity = intensity;
                    record.boredom = mean;
                    record.threshold = enough;
                    record.familiarity = mind.familiarity();
                    record.arousal = mind.arousal();
                    record.brightness = mind.brightness();
                    record.density = mind.density();
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
        // shorter than two bars) at least two bars after they fell quiet, so a short
        // length stays an event and doesn't turn into wallpaper.
        frameStartedIdle = ! heads.busy() && ! hand.busy()
                           && (double) quietFor >= std::max (0.0, 2.0 * barBeats - frameBeats) * samplesPerBeat;
    }
    beat += beatsPerSample;
    quietFor = heads.busy() || hand.busy() ? 0 : quietFor + 1;
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

    fly.setAwake (flyItch ? awakeFor (itchChoice) : 0.0f);
    fly.setEmptyHz (emptyBandHz());
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
        stepOnset = std::max (stepOnset, attack);
        heads.listen (mid, side, attack);

        float hm = 0.0f, hs = 0.0f, tm = 0.0f, ts = 0.0f, turn = 0.0f;
        heads.process (hm, hs, tm, ts, turn);
        if (std::abs (turn) > 0.0f)
            echo.turnSpace (turn);

        if (outsideOn)
            echo.setTouch (outside);
        else if (flyItch)
            echo.setTouch (fly.process (l, r));
        else if (handItch)
            echo.setTouch (hand.process (attack));

        float wm = 0.0f, ws = 0.0f;
        echo.process (mid, side, tm, ts, wm, ws);

        // The dry passes untouched; bypass fades the echo and the heads out.
        wetNow += std::clamp (wetTarget - wetNow, -wetStep, wetStep);
        const float em = wetNow * (wm + hm), es = wetNow * (ws + hs);
        if (tapLeft != nullptr)
        {
            tapLeft[i] = wetNow * (hm + hs);
            tapRight[i] = wetNow * (hm - hs);
        }
        float wetL = em + es, wetR = em - es;

        // Safety limiter on the echo only: the dry passes untouched, always. When the dry and
        // the echo together would pass -0.3 dBFS, the echo dips under the hit (instant, then
        // back over 120 ms), the way an engineer rides the return down. A seatbelt, not an effect.
        // dry off (a return track) fades over 30 ms; bypass always brings the dry back
        const float dryWant = wetTarget > 0.5f ? dryTarget : 1.0f;
        dryNow += std::clamp (dryWant - dryNow, -wetStep, wetStep);
        const float dl = dryNow * l, dr = dryNow * r;
        const float room = std::min (wetRoom (dl, wetL), wetRoom (dr, wetR));
        limiterEnv = room < limiterEnv ? room : room + limiterRelease * (limiterEnv - room);
        wetL *= limiterEnv;
        wetR *= limiterEnv;
        const float outL = dl + wetL;
        const float outR = dr + wetR;
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
    state.hit = echo.takeHit();
    state.body = state.echo.body;
    state.echoBody = state.echo.echoBody;
    state.runaway = state.echo.runaway;
    state.repetition = listener.repetition();
    state.frameProgress = (float) (std::fmod (std::max (0.0, beat), frameBeats) / frameBeats);
    state.boredom = std::min (1.0f, (float) boredFrames / 3.0f);
    state.itching = heads.busy() || hand.busy() || fly.busy();
    state.fly = fly.doing();
    state.flyAwake = flyItch && itchChoice > 0;
    for (int h = 0; h < Heads::count; ++h)
        state.lamps[(size_t) h] = heads.lamp (h);
    state.bpm = bpm;
    state.hostPlaying = time.playing;
    for (int i = 0; i < Creature::neurons; ++i)
        state.neurons[(size_t) i] = mind.firing (i);
    state.arousal = mind.arousal();
    state.familiarity = mind.familiarity();
}
} // namespace dendrite

namespace dendrite
{
// The band the music leaves emptiest (of the Listener's 250 Hz .. 4.5 kHz bands): the
// quietest against its neighbours over the creature's long memory of the bands.
float Engine::emptyBandHz() const noexcept
{
    static constexpr float centres[] = { 90.0f, 250.0f, 700.0f, 1800.0f, 4500.0f, 10000.0f };
    const auto& life = mind.life();
    if (life.bandMeanReady == 0)
        return 1800.0f;
    int best = 3;
    float lowest = 1.0e9f;
    for (int b = 1; b <= 4; ++b)
    {
        const float dip = life.bandMean[b] - 0.5f * (life.bandMean[b - 1] + life.bandMean[b + 1]);
        if (dip < lowest)
        {
            lowest = dip;
            best = b;
        }
    }
    return centres[best];
}
} // namespace dendrite
