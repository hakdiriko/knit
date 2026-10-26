#include "Itch.h"

namespace dendrite::dsp
{
namespace
{
    constexpr float golden = 1.6180339f;

    // Breathing depth from the Itch knob: still -> breathing -> restless (and beyond).
    constexpr std::array<Breakpoint, 4> breathPath {{ { 0.0f, 0.0f }, { 0.35f, 1.0f }, { 0.6f, 1.7f }, { 1.0f, 2.4f } }};

    // Kuramoto coupling (rad/s) from Ecology. The natural rates below sit about
    // 0.3 rad/s apart, so the wobbles lock somewhere past 60% Ecology: the "ringing"
    // and "life" regimes breathe as one body.
    constexpr std::array<Breakpoint, 4> kuramotoPath {{ { 0.0f, 0.0f }, { 0.4f, 0.08f }, { 0.65f, 0.5f }, { 1.0f, 1.3f } }};
    constexpr std::array<float, 4> naturalHz { 0.09f, 0.13f, 0.17f, 0.11f };

    // Gesture weights for hits (drums) and for sustain (pads), blended by how
    // transient the input is. Order: throw, runaway, dub drop, lurch, brake, grab, retune.
    constexpr std::array<float, 7> hitWeights     { 3.0f, 0.7f, 2.5f, 2.0f, 1.0f, 2.5f, 0.5f };
    constexpr std::array<float, 7> sustainWeights { 1.0f, 3.0f, 0.7f, 1.5f, 2.5f, 0.7f, 2.5f };

    // Scar size per scratch and the most a scar can grow to.
    constexpr std::array<float, 3> scarUnit  { 0.8f, 0.025f, 0.12f };
    constexpr std::array<float, 3> scarLimit { 3.0f, 0.06f, 0.4f };

    float smooth01 (float x) noexcept
    {
        x = std::clamp (x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    // Attack, hold, release envelope (seconds), smoothstepped edges.
    float envelope (float t, float attack, float hold, float release) noexcept
    {
        if (t < 0.0f)
            return 0.0f;
        if (t < attack)
            return smooth01 (t / attack);
        t -= attack;
        if (t < hold)
            return 1.0f;
        t -= hold;
        if (t < release)
            return 1.0f - smooth01 (t / release);
        return 0.0f;
    }
} // namespace

const char* Itch::gestureName (int gesture) noexcept
{
    static constexpr std::array<const char*, 7> names { "throw", "runaway", "dub drop", "lurch", "brake", "grab", "retune" };
    return gesture >= 0 && gesture < numGestures ? names[(size_t) gesture] : "";
}

void Itch::prepare (float sampleRate, float rate)
{
    sr = sampleRate;
    controlRate = rate;
    dt = 1.0f / controlRate;

    hopSamples = std::max (1, (int) std::round (0.05f * sr));
    hopSeconds = (float) hopSamples / sr;

    for (int b = 0; b < numBands; ++b)
        bands[(size_t) b].setCoefficients (80.0f * std::pow (2.0f, (float) b), 1.2f, sr);

    for (int k = 0; k < 4; ++k)
        pink[(size_t) k].prepare (controlRate, 3.0f * std::pow (golden, (float) k * 0.5f), 0x7f4a7c15u + 104729u * (uint32_t) k);

    healCoef = std::exp (-1.0f / (180.0f * controlRate));
    fastHealCoef = std::exp (-1.0f / (3.0f * controlRate));

    reset();
}

void Itch::reset()
{
    restartDynamics (0x6d2b79f5u);

    for (auto& p : pink)
        p.reset();
    for (int k = 0; k < 4; ++k)
        phase[(size_t) k] = 1.7f * (float) k;

    mods = {};
    tick (0.0f); // so the first block already carries scars and breathing
}

void Itch::restartDynamics (uint32_t seed) noexcept
{
    for (auto& b : bands)
        b.reset();

    energy.fill (0.0f);
    hopCounter = 0;
    historyPos = 0;
    framesSeen = 0;
    soundFrames = 0;
    specSim = barSim = sameness = 0.0f;
    prevBroadband = -100.0f;
    onsetRate = transient = 0.0f;
    idleSeconds = 0.0f;

    level = 0.0f;
    armedSeconds = 0.0f;
    relief = holdSeconds = 0.0f;
    active = lastGesture = none;
    manualPending = false;

    rng.seed (seed);
    threshold = 1.0f + 0.12f * rng.nextBipolar();
}

void Itch::setTransport (const Transport& t) noexcept
{
    const bool tempo = t.playing && t.bpm > 0.0;

    // Play pressed: start listening from scratch, with a seed taken from the bar the
    // transport starts on. Playing from the same bar gives the same scratches.
    if (tempo && ! wasPlaying)
    {
        const auto barIndex = (uint32_t) (long long) std::floor (t.ppq / std::max (1.0, t.barQuarters));
        restartDynamics (0x6d2b79f5u ^ (barIndex * 2654435761u));
        lastPhrase = (long long) std::floor (t.ppq / (4.0 * std::max (1.0, t.barQuarters)));
    }

    wasPlaying = tempo;
    transport = t;
}

void Itch::analyse (float mono) noexcept
{
    for (int b = 0; b < numBands; ++b)
    {
        const float v = bands[(size_t) b].process (mono);
        energy[(size_t) b] += v * v;
    }

    if (++hopCounter >= hopSamples)
    {
        hopCounter = 0;
        analyseFrame();
        energy.fill (0.0f);
    }
}

void Itch::analyseFrame() noexcept
{
    std::array<float, numBands> db {};
    float total = 0.0f;

    for (int b = 0; b < numBands; ++b)
    {
        const float e = energy[(size_t) b] / (float) hopSamples;
        total += e;
        db[(size_t) b] = std::max (-80.0f, 10.0f * std::log10 (e + 1.0e-12f));
    }

    const float broadband = 10.0f * std::log10 (total + 1.0e-12f);
    const bool silent = broadband < -60.0f;
    const float presence = std::clamp ((broadband + 60.0f) / 20.0f, 0.0f, 1.0f);

    // ---- Sameness: the last two seconds against the last ten. Both start as plain running
    // means over the sound heard so far, so a fresh start (or a quiet intro) never
    // leaves a stale average behind.
    if (! silent)
    {
        ++soundFrames;
        const float warm = 1.0f / (float) soundFrames;
        const float aShort = std::max (hopSeconds / 2.0f, warm), aLong = std::max (hopSeconds / 10.0f, warm);
        for (int b = 0; b < numBands; ++b)
        {
            shortDb[(size_t) b] += aShort * (db[(size_t) b] - shortDb[(size_t) b]);
            longDb[(size_t) b] += aLong * (db[(size_t) b] - longDb[(size_t) b]);
        }
    }

    float spread = 0.0f;
    for (int b = 0; b < numBands; ++b)
        spread += std::abs (shortDb[(size_t) b] - longDb[(size_t) b]);
    specSim = 1.0f - std::clamp (spread / (float) numBands / 6.0f, 0.0f, 1.0f);

    // ---- With a tempo: this bar against the last one (a loop scores near 1).
    const bool tempo = transport.playing && transport.bpm > 0.0;
    if (tempo && ! silent)
    {
        const float barSeconds = (float) (transport.barQuarters * 60.0 / transport.bpm);
        const int lag = (int) std::round (barSeconds / hopSeconds);

        if (lag >= 2 && lag < historyFrames && framesSeen > lag)
        {
            const auto& then = history[(size_t) ((historyPos - lag + historyFrames) % historyFrames)];
            float d = 0.0f;
            for (int b = 0; b < numBands; ++b)
                d += std::abs (db[(size_t) b] - then[(size_t) b]);
            const float sim = 1.0f - std::clamp (d / (float) numBands / 9.0f, 0.0f, 1.0f);
            barSim += (hopSeconds / 2.0f) * (sim - barSim);
        }
    }

    history[(size_t) historyPos] = db;
    historyPos = (historyPos + 1) % historyFrames;
    ++framesSeen;

    if (! silent)
        sameness = tempo ? 0.5f * (specSim + barSim) : specSim;

    // ---- Hits or sustain: onsets per second (a 6 dB jump from one frame to the next).
    const bool onset = ! silent && broadband - prevBroadband > 6.0f;
    onsetRate += (hopSeconds / 3.0f) * ((onset ? 1.0f / hopSeconds : 0.0f) - onsetRate);
    transient = std::clamp (onsetRate / 3.0f, 0.0f, 1.0f);
    prevBroadband = broadband;

    // ---- Build. Below 60% Itch nothing builds: breathing only.
    idleSeconds = knobsMoving ? 0.0f : idleSeconds + hopSeconds;

    if (amount >= 0.6f)
    {
        const float fillSeconds = expMap (40.0f, 3.0f, (amount - 0.6f) / 0.4f);
        const float same = std::clamp ((sameness - 0.45f) / 0.5f, 0.0f, 1.0f);
        const float hands = 0.5f + 0.5f * std::min (1.0f, idleSeconds / 20.0f);

        if (active == none && relief < 0.3f)
            level += hopSeconds * same * hands * presence / fillSeconds;

        level -= hopSeconds * 0.12f * (1.0f - sameness);
    }
    else
    {
        level -= hopSeconds * 0.5f;
    }

    if (knobsMoving)
        level -= hopSeconds * 0.4f;

    level = std::clamp (level, 0.0f, 1.5f);
}

void Itch::tick (float ecology) noexcept
{
    // ---- Transport between host updates.
    const bool tempo = transport.playing && transport.bpm > 0.0;
    if (tempo)
        transport.ppq += transport.bpm / 60.0 * (double) dt;

    // ---- Breathing: a 1/f walk plus a phase oscillator per node, the oscillators
    // pulled together by Ecology.
    const float coupling = evalPath (kuramotoPath, ecology);
    std::array<float, 4> pull {};
    for (int k = 0; k < 4; ++k)
        for (int j = 0; j < 4; ++j)
            if (j != k)
                pull[(size_t) k] += std::sin (phase[(size_t) j] - phase[(size_t) k]);

    const float breathDepth = evalPath (breathPath, amount) * (1.0f - 0.6f * relief);
    for (int k = 0; k < 4; ++k)
    {
        auto& p = phase[(size_t) k];
        p += dt * (2.0f * pi * naturalHz[(size_t) k] + 0.25f * coupling * pull[(size_t) k]);
        if (p > 2.0f * pi)
            p -= 2.0f * pi;

        mods.breath[(size_t) k] = breathDepth * (0.55f * std::sin (p) + 0.45f * pink[(size_t) k].tick());
    }

    // ---- Fire.
    if (manualPending)
    {
        manualPending = false;
        start (chooseGesture(), true);
    }
    else if (active == none && level >= threshold)
    {
        armedSeconds += dt;
        bool fire = true;

        if (tempo)
        {
            const double phraseQuarters = transport.barQuarters * (amount > 0.85f ? 2.0 : 4.0);
            const auto phrase = (long long) std::floor (transport.ppq / phraseQuarters);
            fire = phrase != lastPhrase || armedSeconds > 40.0f; // safety: never stuck armed
        }

        if (fire)
            start (chooseGesture(), false);
    }

    if (tempo)
        lastPhrase = (long long) std::floor (transport.ppq / (transport.barQuarters * (amount > 0.85f ? 2.0 : 4.0)));

    updateGesture();

    // ---- Scars heal; at 0% Itch everything heals within seconds (fully predictable).
    const bool still = amount < 0.02f;
    for (size_t s = 0; s < 3; ++s)
    {
        scarNow[s] *= still ? fastHealCoef : healCoef;
        if (still)
            scarKept[s] *= fastHealCoef;
    }

    mods.scarDetune = scarNow[0] + scarKept[0];
    mods.scarEcho = scarNow[1] + scarKept[1];
    mods.scarTone = scarNow[2] + scarKept[2];
}

int Itch::chooseGesture() noexcept
{
    std::array<float, numGestures> w {};
    float total = 0.0f;

    for (int g = 0; g < numGestures; ++g)
    {
        w[(size_t) g] = transient * hitWeights[(size_t) g] + (1.0f - transient) * sustainWeights[(size_t) g];
        if (g == lastGesture)
            w[(size_t) g] *= 0.3f; // rarely the same scratch twice in a row
        total += w[(size_t) g];
    }

    float r = rng.next01() * total;
    for (int g = 0; g < numGestures; ++g)
    {
        r -= w[(size_t) g];
        if (r <= 0.0f)
            return g;
    }

    return numGestures - 1;
}

void Itch::start (int gesture, bool manual) noexcept
{
    active = gesture;
    lastGesture = gesture;
    elapsed = 0.0f;
    level = 0.0f;
    armedSeconds = 0.0f;
    threshold = 1.0f + 0.12f * rng.nextBipolar();
    ++scratchCount;

    // Gestures are timed in beats and bars when there's a tempo, else as if at 120 BPM.
    beat = transport.bpm > 0.0 ? std::clamp ((float) (60.0 / transport.bpm), 0.25f, 1.5f) : 0.5f;
    bar = beat * (float) std::clamp (transport.barQuarters, 1.0, 8.0);
    depth = manual ? 0.85f : 0.55f + 0.45f * smooth01 ((amount - 0.6f) / 0.4f);

    const float r = rng.next01();

    switch (gesture)
    {
        case throwEcho: length = 2.0f * bar + 0.1f; break;
        case runaway:   length = 2.25f * bar + 0.1f; break;
        case dubDrop:
            dropBeats = transient > 0.4f ? 1.0f : 2.0f;
            length = dropBeats * beat + bar + 0.1f;
            break;
        case lurch:
            ratio = r < 0.3f ? 2.0f : r < 0.6f ? 0.5f : r < 0.8f ? 1.5f : 0.75f;
            length = 2.0f * bar + 0.1f;
            break;
        case brake:     length = 2.0f * beat + 1.2f; break;
        case grab:
            grabLoop = std::clamp ((transient > 0.4f ? 0.25f : 0.5f) * beat, 0.06f, 0.6f);
            grabHold = std::min (bar, 2.6f - grabLoop);
            length = grabHold + 0.06f;
            break;
        case retune:
            ratio = r < 0.35f ? 1.5f : r < 0.6f ? 0.75f : r < 0.8f ? 2.0f : 0.5f;
            length = 2.0f * bar + 0.2f;
            break;
        default: active = none; return;
    }

    if (amount >= 0.02f || manual)
        addScar();
}

void Itch::addScar() noexcept
{
    const auto s = (size_t) std::min (2, (int) (rng.next01() * 3.0f));
    const float m = (rng.next01() < 0.5f ? -1.0f : 1.0f) * (0.4f + 0.6f * rng.next01()) * scarUnit[s];

    scarNow[s] = std::clamp (scarNow[s] + m, -scarLimit[s], scarLimit[s]);
    scarKept[s] = std::clamp (scarKept[s] + 0.15f * m, -scarLimit[s], scarLimit[s]);
}

void Itch::updateGesture() noexcept
{
    auto& m = mods;
    m.echoSend = m.echoLoopAdd = m.loopAdd = m.couplingAdd = m.grabMix = 0.0f;
    m.loopScale = m.dryGain = m.lurchRatio = m.retuneRatio = m.brakeSpeed = 1.0f;
    m.braking = false;

    // Relief lasts 8 s when itchy, down to 3 s when compulsive.
    relief = std::max (0.0f, relief - dt / (8.0f - 5.0f * smooth01 ((amount - 0.6f) / 0.4f)));
    holdSeconds = std::max (0.0f, holdSeconds - dt);

    if (active != none)
    {
        const float t = elapsed;

        switch (active)
        {
            case throwEcho:
                m.echoSend = depth * 5.0f * envelope (t, 0.004f, 0.5f * beat, 0.03f);
                m.echoLoopAdd = depth * 0.4f * envelope (t, 0.01f, bar, bar);
                break;

            case runaway:
            {
                const float build = envelope (t, 0.75f * bar, 0.75f * bar, 0.01f);
                m.loopAdd = depth * 0.3f * build;
                m.couplingAdd = 0.35f * build;
                m.loopScale = 1.0f - 0.65f * envelope (t - 1.5f * bar, 0.01f, 0.25f * bar, 0.5f * bar);
                break;
            }

            case dubDrop:
                m.dryGain = 1.0f - 0.97f * envelope (t, 0.006f, dropBeats * beat, 0.015f);
                m.echoSend = depth * 4.0f * envelope (t, 0.004f, 0.35f * beat, 0.03f);
                m.echoLoopAdd = depth * 0.25f * envelope (t, 0.01f, dropBeats * beat, bar);
                break;

            case lurch:
                m.lurchRatio = t < 2.0f * bar ? ratio : 1.0f;
                break;

            case brake:
                m.braking = t < 2.0f * beat;
                m.brakeSpeed = 1.0f - 0.6f * depth * smooth01 (t / (1.5f * beat));
                break;

            case grab:
                m.grabMix = envelope (t, 0.008f, grabHold, 0.04f);
                m.grabLoopSeconds = grabLoop;
                break;

            case retune:
                m.retuneRatio = t < 2.0f * bar ? ratio : 1.0f;
                break;

            default: break;
        }

        elapsed += dt;
        if (elapsed >= length)
        {
            active = none;
            relief = 1.0f;
            holdSeconds = 0.5f;
        }
    }

    m.holdGain = active != none || holdSeconds > 0.0f;
}

std::array<float, 6> Itch::getScars() const noexcept
{
    return { scarNow[0], scarNow[1], scarNow[2], scarKept[0], scarKept[1], scarKept[2] };
}

void Itch::setScars (const std::array<float, 6>& s) noexcept
{
    for (size_t i = 0; i < 3; ++i)
    {
        scarNow[i] = std::isfinite (s[i]) ? std::clamp (s[i], -scarLimit[i], scarLimit[i]) : 0.0f;
        scarKept[i] = std::isfinite (s[i + 3]) ? std::clamp (s[i + 3], -scarLimit[i], scarLimit[i]) : 0.0f;
    }
}
} // namespace dendrite::dsp
