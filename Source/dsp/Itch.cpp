#include "Itch.h"

namespace dendrite::dsp
{
namespace
{
    constexpr float golden = 1.6180339f;

    // Breathing depth from the Itch knob: still -> breathing -> restless -> feverish.
    constexpr std::array<Breakpoint, 4> breathPath {{ { 0.0f, 0.0f }, { 0.35f, 1.0f }, { 0.7f, 1.8f }, { 1.0f, 2.4f } }};

    // Kuramoto coupling (rad/s) from Ecology. The natural rates below sit about
    // 0.3 rad/s apart, so the wobbles lock somewhere past 60% Ecology: the "ringing"
    // and "life" regimes breathe as one body.
    constexpr std::array<Breakpoint, 4> kuramotoPath {{ { 0.0f, 0.0f }, { 0.4f, 0.08f }, { 0.65f, 0.5f }, { 1.0f, 1.3f } }};
    constexpr std::array<float, 4> naturalHz { 0.09f, 0.13f, 0.17f, 0.11f };

    // Move weights for hits (drums) and for sustain (pads), blended by how transient
    // the input is. Order: open, close, step, lean, tilt.
    constexpr std::array<float, 5> hitWeights     { 0.7f, 1.6f, 2.0f, 1.5f, 0.8f };
    constexpr std::array<float, 5> sustainWeights { 2.0f, 0.8f, 0.8f, 1.5f, 1.6f };

    // Scar size per move and the most a scar can grow to.
    constexpr std::array<float, 3> scarUnit  { 0.8f, 0.025f, 0.12f };
    constexpr std::array<float, 3> scarLimit { 3.0f, 0.06f, 0.4f };

    // Moves land on a 4-bar line.
    constexpr double phraseBars = 4.0;

    float smooth01 (float x) noexcept
    {
        x = std::clamp (x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }
} // namespace

const char* Itch::moveName (int move) noexcept
{
    static constexpr std::array<const char*, 5> names { "open", "close", "step", "lean", "tilt" };
    return move >= 0 && move < numMoves ? names[(size_t) move] : "";
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
    appliedCoef = 1.0f - std::exp (-1.0f / (0.08f * controlRate));

    reset();
}

void Itch::reset()
{
    restartDynamics (0x6d2b79f5u);

    for (auto& p : pink)
        p.reset();
    for (int k = 0; k < 4; ++k)
        phase[(size_t) k] = 1.7f * (float) k;

    applied = {};
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
    specSim = barSim = fastSim = sameness = 0.0f;
    prevBroadband = -100.0f;
    onsetRate = transient = 0.0f;

    level = 0.0f;
    armedSeconds = 0.0f;
    relief = 0.0f;
    manualPending = false;

    // A move in progress lets go; `applied` eases back, so nothing jumps.
    stage = idle;
    current = pending = lastMove = none;
    moveAmount = 0.0f;

    rng.seed (seed);
    threshold = 1.0f + 0.08f * rng.nextBipolar();
}

void Itch::setTransport (const Transport& t) noexcept
{
    const bool tempo = t.playing && t.bpm > 0.0;

    // Play pressed: start listening from scratch, with a seed taken from the bar the
    // transport starts on. Playing from the same bar gives the same moves.
    if (tempo && ! wasPlaying)
    {
        const auto barIndex = (uint32_t) (long long) std::floor (t.ppq / std::max (1.0, t.barQuarters));
        restartDynamics (0x6d2b79f5u ^ (barIndex * 2654435761u));
        lastPhrase = (long long) std::floor (t.ppq / (phraseBars * std::max (1.0, t.barQuarters)));
    }

    wasPlaying = tempo;
    transport = t;
}

float Itch::barSeconds() const noexcept
{
    // Without a tempo, a bar counts as 2 s (4/4 at 120 BPM).
    if (! hasTempo())
        return 2.0f;
    return std::clamp ((float) (transport.barQuarters * 60.0 / transport.bpm), 0.25f, 16.0f);
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

float Itch::repeatScore (const std::array<float, numBands>& db) const noexcept
{
    // The same moment 1, 2, 4 and 8 bars ago, read between frames (bars rarely last a
    // whole number of 50 ms frames). The best match wins: a loop repeats at its own
    // length, and also at every multiple of it.
    const float barFrames = barSeconds() / hopSeconds;
    float best = -1.0f;

    for (const float bars : { 1.0f, 2.0f, 4.0f, 8.0f })
    {
        const float lag = bars * barFrames;
        if (lag < 2.0f || lag > (float) (historyFrames - 2) || (float) framesSeen < lag + 1.0f)
            continue;

        const int whole = (int) lag;
        const float frac = lag - (float) whole;
        const auto& a = history[(size_t) ((historyPos - whole + historyFrames) % historyFrames)];
        const auto& b = history[(size_t) ((historyPos - whole - 1 + historyFrames) % historyFrames)];

        float d = 0.0f;
        for (int k = 0; k < numBands; ++k)
            d += std::abs (db[(size_t) k] - ((1.0f - frac) * a[(size_t) k] + frac * b[(size_t) k]));

        best = std::max (best, 1.0f - std::clamp (d / (float) numBands / 9.0f, 0.0f, 1.0f));
    }

    return best;
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
    const float presence = std::clamp ((broadband + 60.0f) / 6.0f, 0.0f, 1.0f); // gaps between hits still count

    // ---- Without a tempo: the last two seconds against the last ten. Both start as
    // plain running means over the sound heard so far, so a fresh start (or a quiet
    // intro) never leaves a stale average behind.
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

    // ---- With a tempo: the repeat score, smoothed over about a bar.
    const bool tempo = hasTempo();
    if (tempo && ! silent)
    {
        const float score = repeatScore (db);
        if (score >= 0.0f)
        {
            barSim += std::min (1.0f, hopSeconds / std::max (1.0f, barSeconds())) * (score - barSim);
            fastSim += (hopSeconds / 0.4f) * (score - fastSim);
        }
    }

    history[(size_t) historyPos] = db;
    historyPos = (historyPos + 1) % historyFrames;
    ++framesSeen;

    if (! silent)
        sameness = tempo ? barSim : specSim;

    // ---- Hits or sustain: onsets per second (a 6 dB jump from one frame to the next).
    const bool onset = ! silent && broadband - prevBroadband > 6.0f;
    onsetRate += (hopSeconds / 3.0f) * ((onset ? 1.0f / hopSeconds : 0.0f) - onsetRate);
    transient = std::clamp (onsetRate / 3.0f, 0.0f, 1.0f);
    prevBroadband = broadband;

    // ---- Count, in bars. Exact repetition fills the counter in `patience` bars;
    // a bar of new material (a fill, a new section) empties it.
    const float barsThisFrame = hopSeconds / barSeconds();

    if (autoOn && stage == idle && relief < 0.3f)
    {
        const float same = std::clamp ((sameness - 0.45f) / 0.5f, 0.0f, 1.0f);
        const float novelty = std::clamp ((0.8f - (tempo ? fastSim : sameness)) / 0.3f, 0.0f, 1.0f);
        level += barsThisFrame * same * presence / (float) patience;
        level -= barsThisFrame * novelty;
    }

    if (! autoOn)
        level -= hopSeconds * 2.0f;
    if (knobsMoving)
        level -= hopSeconds * 0.25f;

    level = std::clamp (level, 0.0f, 1.2f);
}

void Itch::tick (float ecology) noexcept
{
    // ---- Transport between host updates.
    const bool tempo = hasTempo();
    if (tempo)
        transport.ppq += transport.bpm / 60.0 * (double) dt;

    const float progress = level / threshold;
    const float tension = buildOn && autoOn && stage == idle ? smooth01 (progress) : 0.0f;

    // ---- Breathing: a 1/f walk plus a phase oscillator per node, the oscillators
    // pulled together by Ecology. While the counter fills, the breathing deepens.
    const float coupling = evalPath (kuramotoPath, ecology);
    std::array<float, 4> pull {};
    for (int k = 0; k < 4; ++k)
        for (int j = 0; j < 4; ++j)
            if (j != k)
                pull[(size_t) k] += std::sin (phase[(size_t) j] - phase[(size_t) k]);

    const float breathDepth = evalPath (breathPath, amount) * (1.0f - 0.6f * relief) * (1.0f + 0.6f * tension);
    for (int k = 0; k < 4; ++k)
    {
        auto& p = phase[(size_t) k];
        p += dt * (2.0f * pi * naturalHz[(size_t) k] + 0.25f * coupling * pull[(size_t) k]);
        if (p > 2.0f * pi)
            p -= 2.0f * pi;

        mods.breath[(size_t) k] = breathDepth * (0.55f * std::sin (p) + 0.45f * pink[(size_t) k].tick());
    }

    // ---- Choose the next move a quarter of the way in, so the build can lean toward it.
    if (stage == idle)
    {
        if (autoOn && pending == none && progress > 0.25f)
        {
            pending = chooseMove();
            prepareMove (pending, 0.45f + 0.55f * amount);
        }
        else if (pending != none && (! autoOn || progress < 0.1f))
        {
            pending = none; // lost interest
        }

        moveAmount = buildOn && pending != none ? 0.3f * smooth01 ((progress - 0.25f) / 0.75f) : 0.0f;
    }

    // ---- Fire.
    if (manualPending)
    {
        manualPending = false;
        fire (true);
    }
    else if (autoOn && stage == idle && level >= threshold)
    {
        armedSeconds += dt;
        bool now = true;

        if (tempo)
        {
            const auto phrase = (long long) std::floor (transport.ppq / (phraseBars * transport.barQuarters));
            now = phrase != lastPhrase || armedSeconds > 60.0f; // safety: never stuck armed
        }

        if (now)
            fire (false);
    }

    if (tempo)
        lastPhrase = (long long) std::floor (transport.ppq / (phraseBars * transport.barQuarters));

    // Auto switched off: an automatic move lets go at once.
    if (! autoOn && ! manualMove && (stage == arriving || stage == holding))
    {
        stage = returning;
        stageTime = 0.0f;
        returnSeconds = 1.0f;
        arriveFrom = moveAmount;
    }

    updateMove();

    // ---- Scars heal; at 0% Itch everything heals within seconds (fully predictable).
    const bool still = amount < 0.02f;
    for (size_t s = 0; s < 3; ++s)
    {
        scarNow[s] *= still ? fastHealCoef : healCoef;
        if (still)
            scarKept[s] *= fastHealCoef;
    }

    mods.ecologyAdd = applied.ecology;
    mods.sizeRatio = applied.ratio;
    mods.toneAdd = applied.tone;
    mods.nodeGain = applied.gain;
    mods.scarDetune = scarNow[0] + scarKept[0];
    mods.scarEcho = scarNow[1] + scarKept[1];
    mods.scarTone = scarNow[2] + scarKept[2];
}

int Itch::chooseMove() noexcept
{
    std::array<float, numMoves> w {};
    float total = 0.0f;

    for (int m = 0; m < numMoves; ++m)
    {
        w[(size_t) m] = transient * hitWeights[(size_t) m] + (1.0f - transient) * sustainWeights[(size_t) m];
        if (m == lastMove)
            w[(size_t) m] *= 0.3f; // rarely the same move twice in a row
        total += w[(size_t) m];
    }

    float r = rng.next01() * total;
    for (int m = 0; m < numMoves; ++m)
    {
        r -= w[(size_t) m];
        if (r <= 0.0f)
            return m;
    }

    return numMoves - 1;
}

void Itch::prepareMove (int move, float depth) noexcept
{
    target = {};
    const float r = rng.next01();

    switch (move)
    {
        case open: // the space blooms: the parts talk more, the top opens
            target.ecology = 0.22f * depth;
            target.tone = 0.35f * depth;
            break;

        case close: // the dub fader comes down: drier, darker
            target.ecology = -0.25f * depth;
            target.tone = -0.5f * depth;
            break;

        case step: // the delay time steps to a neighbouring value (a note value under Sync)
            target.ratio = r < 0.35f ? 2.0f : r < 0.7f ? 0.5f : r < 0.85f ? 1.5f : 0.75f;
            target.gain[0] = 1.0f + 0.4f * depth; // the echo leans forward while it builds
            break;

        case lean: // one node comes forward: echoes or the past on hits, shimmer or ring on sustain
        {
            const int node = transient > 0.4f ? (r < 0.5f ? 0 : 3) : (r < 0.5f ? 2 : 1);
            for (int k = 0; k < 4; ++k)
                target.gain[(size_t) k] = k == node ? 1.0f + 1.0f * depth : 1.0f - 0.2f * depth;
            break;
        }

        case tilt: // the tone tips one way, and the Shift node's octaves follow it
            target.tone = (r < 0.5f ? -0.6f : 0.6f) * depth;
            break;

        default: break;
    }
}

void Itch::fire (bool manual) noexcept
{
    int move = pending;
    if (manual || move == none)
    {
        move = chooseMove();
        prepareMove (move, manual ? std::max (0.8f, 0.45f + 0.55f * amount) : 0.45f + 0.55f * amount);
    }

    current = lastMove = move;
    pending = none;
    manualMove = manual;
    stage = arriving;
    stageTime = 0.0f;
    arriveFrom = moveAmount;

    // Timed in beats and bars at the host tempo, or as if at 120 BPM without one.
    const float beat = transport.bpm > 0.0 ? std::clamp ((float) (60.0 / transport.bpm), 0.25f, 1.5f) : 0.5f;
    const float bar = beat * (float) std::clamp (transport.barQuarters, 1.0, 8.0);
    arriveSeconds = manual ? 0.12f : 0.25f * beat;
    holdSeconds = (patience <= 4 ? 2.0f : 4.0f) * bar;
    returnSeconds = 2.0f * bar;

    level = 0.0f;
    armedSeconds = 0.0f;
    threshold = 1.0f + 0.08f * rng.nextBipolar();
    ++scratchCount;

    if (amount >= 0.02f || manual)
        addScar();
}

void Itch::updateMove() noexcept
{
    relief = std::max (0.0f, relief - dt / 6.0f);

    switch (stage)
    {
        case arriving:
            stageTime += dt;
            moveAmount = arriveFrom + (1.0f - arriveFrom) * smooth01 (stageTime / arriveSeconds);
            if (stageTime >= arriveSeconds)
            {
                stage = holding;
                stageTime = 0.0f;
            }
            break;

        case holding:
            stageTime += dt;
            moveAmount = 1.0f;
            if (stageTime >= holdSeconds)
            {
                stage = returning;
                stageTime = 0.0f;
                arriveFrom = 1.0f;
            }
            break;

        case returning:
            stageTime += dt;
            moveAmount = arriveFrom * (1.0f - smooth01 (stageTime / returnSeconds));
            if (stageTime >= returnSeconds)
            {
                stage = idle;
                current = none;
                manualMove = false;
                moveAmount = 0.0f;
                relief = 1.0f;
            }
            break;

        case idle:
        default: break;
    }

    // Continuous offsets ease toward the move (no zipper, no jump); a step is a switch
    // halfway through, and the network's glide or crossfade carries it.
    const float a = moveAmount;
    applied.ecology += appliedCoef * (target.ecology * a - applied.ecology);
    applied.tone += appliedCoef * (target.tone * a - applied.tone);
    for (size_t k = 0; k < 4; ++k)
        applied.gain[k] += appliedCoef * ((1.0f + (target.gain[k] - 1.0f) * a) - applied.gain[k]);
    applied.ratio = a > 0.5f ? target.ratio : 1.0f;
}

void Itch::addScar() noexcept
{
    const auto s = (size_t) std::min (2, (int) (rng.next01() * 3.0f));
    const float m = (rng.next01() < 0.5f ? -1.0f : 1.0f) * (0.4f + 0.6f * rng.next01()) * scarUnit[s];

    scarNow[s] = std::clamp (scarNow[s] + m, -scarLimit[s], scarLimit[s]);
    scarKept[s] = std::clamp (scarKept[s] + 0.15f * m, -scarLimit[s], scarLimit[s]);
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
