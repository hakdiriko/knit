#include "Network.h"

namespace dendrite::dsp
{
namespace
{
    constexpr float golden = 1.6180339f;
    constexpr float inputGain = 0.5f;
    constexpr float wetGain = 2.0f; // make-up after the loops; auto-gain does the fine matching
    constexpr float starveGain = 0.8f;

    // Node order: Echo, Resonator, Shift, Memory.
    constexpr std::array<float, 4> nodeLevel     { 1.0f, 0.55f, 0.7f, 0.8f };
    constexpr std::array<float, 4> panBase       { -0.6f, 0.3f, -0.3f, 0.6f };
    constexpr std::array<float, 4> lowpassScale  { 1.0f, 1.4f, 0.8f, 0.9f };
    constexpr std::array<float, 4> lowpassQ      { 0.6f, 0.707f, 0.6f, 0.6f };
    constexpr std::array<float, 4> diffuserMs    { 13.7f, 1.3f, 9.1f, 17.3f };
    constexpr std::array<float, 4> diffuserScale { 1.0f, 0.3f, 1.0f, 1.0f };
    constexpr std::array<float, 4> driftDepth    { 0.004f, 0.0025f, 0.003f, 0.0f };
    constexpr std::array<float, 4> glideSeconds  { 0.3f, 0.06f, 0.25f, 0.4f };

    // ---- Ecology: colour (0-30%) -> space (30-60%) -> ringing (60-80%) -> life (80-100%).
    // Each node crosses loop gain 1.0 at a slightly different point (70-76%), so the
    // tipping point into self-oscillation is a blur, never a step.
    constexpr std::array<Breakpoint, 5> couplingPath {{ { 0.0f, 0.05f }, { 0.3f, 0.2f }, { 0.6f, 0.85f }, { 0.8f, 1.0f }, { 1.0f, 1.0f } }};
    constexpr std::array<Breakpoint, 4> diffusionPath {{ { 0.0f, 0.0f }, { 0.3f, 0.25f }, { 0.6f, 0.6f }, { 1.0f, 0.65f } }};
    constexpr std::array<Breakpoint, 3> ecologyDrivePath {{ { 0.0f, 1.0f }, { 0.6f, 1.0f }, { 1.0f, 1.8f } }};

    constexpr std::array<std::array<Breakpoint, 5>, 4> loopGainPath {{
        {{ { 0.0f, 0.30f }, { 0.3f, 0.50f }, { 0.6f, 0.82f }, { 0.73f, 1.0f }, { 1.0f, 1.18f } }}, // Echo
        {{ { 0.0f, 0.55f }, { 0.3f, 0.75f }, { 0.6f, 0.93f }, { 0.70f, 1.0f }, { 1.0f, 1.12f } }}, // Resonator
        {{ { 0.0f, 0.20f }, { 0.3f, 0.40f }, { 0.6f, 0.75f }, { 0.75f, 1.0f }, { 1.0f, 1.15f } }}, // Shift
        {{ { 0.0f, 0.25f }, { 0.3f, 0.45f }, { 0.6f, 0.78f }, { 0.76f, 1.0f }, { 1.0f, 1.16f } }}, // Memory
    }};

    float smoothstep (float x) noexcept { return x * x * (3.0f - 2.0f * x); }

    // ---- Sync: note values in quarter-note beats, shortest first (straight, triplet, dotted).
    struct NoteValue
    {
        float beats;
        const char* name;
    };

    constexpr std::array<NoteValue, 18> noteValues {{
        { 0.0625f, "1/64" },       { 1.0f / 12.0f, "1/32 triplet" }, { 0.125f, "1/32" },
        { 1.0f / 6.0f, "1/16 triplet" }, { 0.1875f, "1/32 dotted" }, { 0.25f, "1/16" },
        { 1.0f / 3.0f, "1/8 triplet" },  { 0.375f, "1/16 dotted" },  { 0.5f, "1/8" },
        { 2.0f / 3.0f, "1/4 triplet" },  { 0.75f, "1/8 dotted" },    { 1.0f, "1/4" },
        { 4.0f / 3.0f, "1/2 triplet" },  { 1.5f, "1/4 dotted" },     { 2.0f, "1/2" },
        { 8.0f / 3.0f, "1/1 triplet" },  { 3.0f, "1/2 dotted" },     { 4.0f, "1/1" },
    }};

    // Longest synced time per node, inside its buffer (the Shift node also needs room
    // for its 0.12 s pitch-shift window).
    constexpr float syncMinSeconds = 0.02f;
    constexpr float echoSyncMaxSeconds = 1.15f;
    constexpr float shiftSyncMaxSeconds = 0.5f;
    constexpr float memorySyncMaxSeconds = 3.2f;

    // The note value closest to `seconds` by ear (log distance) that fits [lo, hi],
    // or -1 when none does (an extreme tempo): the node then stays free.
    int nearestNote (float seconds, float beatSeconds, float lo, float hi) noexcept
    {
        int best = -1;
        float bestDistance = 0.0f;

        for (size_t i = 0; i < noteValues.size(); ++i)
        {
            const float t = noteValues[i].beats * beatSeconds;
            if (t < lo || t > hi)
                continue;

            const float d = std::abs (std::log (t / seconds));
            if (best < 0 || d < bestDistance)
            {
                best = (int) i;
                bestDistance = d;
            }
        }

        return best;
    }
} // namespace

void Network::prepare (double sampleRate)
{
    sr = (float) sampleRate;

    lines[echo].prepare ((int) (1.2f * sr));
    lines[resonator].prepare ((int) (0.03f * sr));
    lines[shift].prepare ((int) (0.65f * sr));
    lines[memory].prepare ((int) (3.4f * sr));

    const float controlRate = sr / (float) controlInterval;

    for (int k = 0; k < numNodes; ++k)
    {
        diffusers[(size_t) k].prepare ((int) (diffuserMs[(size_t) k] * 0.001f * sr));
        dcBlock[(size_t) k].prepare (sr);
        // Golden-ratio spaced periods: the drifts never line up, so motion never repeats.
        drift[(size_t) k].prepare (controlRate, 1.3f * std::pow (golden, (float) k), 0x51ed27u + 7919u * (uint32_t) k);
        glideCoef[(size_t) k] = 1.0f - std::exp (-1.0f / (glideSeconds[(size_t) k] * sr));
    }

    itch.prepare (sr, controlRate);
    fadeStep = 1.0f / (0.025f * sr);
    brakeCatchUp = std::exp (-1.0f / (0.35f * sr));
    brakeLimit = 1.1f * sr; // stays inside the Echo buffer at any Size

    ecologySm.reset (controlRate, 0.06);
    sizeSm.reset (controlRate, 0.06);
    toneSm.reset (controlRate, 0.06);

    envAttack = 1.0f - std::exp (-1.0f / (0.005f * sr));
    envRelease = 1.0f - std::exp (-1.0f / (1.5f * sr));
    presenceCoef = 1.0f - std::exp (-1.0f / (2.0f * controlRate));

    reset();
}

void Network::reset()
{
    for (int k = 0; k < numNodes; ++k)
    {
        lines[(size_t) k].reset();
        diffusers[(size_t) k].reset();
        lowpass[(size_t) k].reset();
        highpass[(size_t) k].reset();
        dcBlock[(size_t) k].reset();
        drift[(size_t) k].reset();
    }

    ecologySm.setCurrentAndTargetValue (ecologySm.getTargetValue());
    sizeSm.setCurrentAndTargetValue (sizeSm.getTargetValue());
    toneSm.setCurrentAndTargetValue (toneSm.getTargetValue());

    phaseUp = phaseDown = 0.0f;
    inputEnvelope = 0.0f;
    itch.reset();
    fadePos.fill (1.0f);
    brakeOffset = 0.0f;
    grabElapsed = 0;
    presence = 1.0f;
    controlCounter = 0;
    snapOnNextUpdate = true;
    updateControl();

    grainRng.seed (0x2545f491u);
    for (size_t v = 0; v < grains.size(); ++v)
    {
        grains[v].length = grainLength;
        grains[v].age = (int) v * grainLength / (int) grains.size();
        grains[v].delay = nextGrainDelay();
    }
}

void Network::setMacros (float ecology, float size, float tone) noexcept
{
    ecologySm.setTargetValue (ecology);
    sizeSm.setTargetValue (size);
    toneSm.setTargetValue (tone);
}

void Network::setItch (float amount, bool knobsMoving) noexcept
{
    itch.setAmount (amount);
    itch.setKnobsMoving (knobsMoving);
}

void Network::setSync (bool enabled, double bpm) noexcept
{
    syncEnabled = enabled;
    beatSeconds = bpm > 0.0 ? (float) (60.0 / bpm) : 0.0f;
}

const char* Network::noteName (int index) noexcept
{
    return index >= 0 && index < (int) noteValues.size() ? noteValues[(size_t) index].name : "";
}

void Network::updateControl() noexcept
{
    const float e = ecologySm.getNextValue();
    const float s = sizeSm.getNextValue();
    const float t = toneSm.getNextValue();

    itch.tick (e);
    const auto& mods = itch.getMods();

    // Starvation: input level -> presence (0 below -75 dBFS, 1 above -55 dBFS).
    const float envDb = 20.0f * std::log10 (inputEnvelope + 1.0e-9f);
    const float present = juce::jlimit (0.0f, 1.0f, (envDb + 75.0f) / 20.0f);
    presence += presenceCoef * (present - presence);

    // ---- Ecology
    coupling = std::min (1.0f, evalPath (couplingPath, e) + mods.couplingAdd);
    diffusion = evalPath (diffusionPath, e);
    const float ecologyDrive = evalPath (ecologyDrivePath, e);

    for (size_t k = 0; k < (size_t) numNodes; ++k)
    {
        const float g = evalPath (loopGainPath[k], e);
        const float fed = g > starveGain ? starveGain + (g - starveGain) * presence : g;
        // Scratches push or cut the loops; the saturators keep anything above 1 bounded.
        const float push = mods.loopAdd + (k == (size_t) echo ? mods.echoLoopAdd : 0.0f);
        loopGain[k] = std::min (1.25f, (fed + push) * mods.loopScale);
    }

    echoSendGain = 1.0f + mods.echoSend;

    // ---- Size: small and close -> huge and far.
    float echoSeconds = expMap (0.07f, 1.0f, s);
    // Retune glides the Resonator to a new interval; a detune scar shifts it slightly.
    const float resonatorHz = std::clamp (expMap (520.0f, 65.0f, s) * mods.retuneRatio
                                              * std::exp2 (mods.scarDetune / 12.0f), 40.0f, 2000.0f);
    float shiftSeconds = expMap (0.04f, 0.45f, s);
    shiftWindow = expMap (0.05f, 0.12f, s) * sr;
    grainLength = (int) (expMap (0.05f, 0.26f, s) * sr);
    float memorySeconds = expMap (0.2f, 2.6f, s);
    const float spread = 0.25f + 0.75f * s;
    const float damping = expMap (1.0f, 0.55f, s);

    // ---- Sync: Size still chooses small or large; Sync rounds each time-based node to
    // the nearest note value. Delay times keep gliding, so stepping from one note
    // value to the next bends the pitch like tape.
    echoNote = -1;
    memorySynced = false;

    if (syncEnabled && beatSeconds > 0.0f)
    {
        const auto snap = [this] (float& seconds, float hi)
        {
            const int note = nearestNote (seconds, beatSeconds, syncMinSeconds, hi);
            if (note >= 0)
                seconds = noteValues[(size_t) note].beats * beatSeconds;
            return note;
        };

        echoNote = snap (echoSeconds, echoSyncMaxSeconds);
        snap (shiftSeconds, shiftSyncMaxSeconds);
        memorySynced = snap (memorySeconds, memorySyncMaxSeconds) >= 0;
    }

    // An echo scar nudges the Echo time, only when free (it would pull Sync off the grid).
    if (echoNote < 0)
        echoSeconds *= 1.0f + mods.scarEcho;

    // Lurch: Echo and Shift jump by a ratio (x2, x0.5, x1.5, x0.75), halved until it fits.
    if (std::abs (mods.lurchRatio - 1.0f) > 1.0e-6f)
    {
        const auto fit = [] (float seconds, float ratio, float hi)
        {
            while (seconds * ratio > hi && ratio > 0.25f)
                ratio *= 0.5f;
            return seconds * ratio;
        };
        echoSeconds = fit (echoSeconds, mods.lurchRatio, echoSyncMaxSeconds);
        shiftSeconds = fit (shiftSeconds, mods.lurchRatio, shiftSyncMaxSeconds);
    }

    jumpMode = (syncJump && echoNote >= 0) || itch.getActiveGesture() == Itch::lurch;

    // The fixed drift keeps the loops from ringing metallic; the breathing rides on top.
    const auto& b = mods.breath;
    targetDelay[echo] = echoSeconds * sr * (1.0f + driftDepth[echo] * drift[echo].tick() + 0.0035f * b[echo]);
    targetDelay[resonator] = sr / resonatorHz * (1.0f + driftDepth[resonator] * drift[resonator].tick() + 0.0015f * b[resonator]);
    targetDelay[shift] = shiftSeconds * sr * (1.0f + driftDepth[shift] * drift[shift].tick() + 0.0035f * b[shift]);
    targetDelay[memory] = memorySeconds * sr;
    drift[memory].tick();

    // ---- Tone: dark <-> bright tilt of the whole network.
    const float lowpassHz = expMap (700.0f, 16000.0f, t) * damping;
    const float highpassHz = expMap (25.0f, 160.0f, t);
    const float toneDrive = expMap (0.8f, 2.0f, t);
    const float balance = smoothstep (t);
    shiftUpGain = std::sin (balance * 0.5f * pi);   // bright: shimmer (octave up)
    shiftDownGain = std::cos (balance * 0.5f * pi); // dark: downward spirals (octave down)

    for (size_t k = 0; k < (size_t) numNodes; ++k)
    {
        // Breathing opens and closes each node's filter (and swells its level) a little.
        const float cutoff = std::clamp (lowpassHz * lowpassScale[k] * std::exp2 (0.35f * b[k] + mods.scarTone), 200.0f, 0.45f * sr);
        outLevel[k] = nodeLevel[k] * std::exp (0.17f * b[k]) // about 1.5 dB per unit of breath
                    * (k == (size_t) memory ? 1.0f + 0.8f * mods.grabMix : 1.0f);
        lowpass[k].setCoefficients (cutoff, lowpassQ[k], sr);
        highpass[k].setCutoff (highpassHz, sr);
        drive[k] = toneDrive * ecologyDrive;

        const float pan = panBase[k] * spread;
        const float angle = (pan + 1.0f) * 0.25f * pi;
        panL[k] = std::cos (angle);
        panR[k] = std::sin (angle);
        injectSide[k] = (panBase[k] < 0.0f ? 1.0f : -1.0f) * spread;

        if (snapOnNextUpdate)
            currentDelay[k] = targetDelay[k];
    }

    snapOnNextUpdate = false;
}

float Network::readShift() noexcept
{
    // Delay-line pitch shifter: a read head sweeping through a window at a steady
    // rate plays faster (or slower) than the write head. Two heads half a window
    // apart, each with a sine window, hide the wrap-around (equal-power crossfade).
    const float window = shiftWindow;

    phaseUp -= 1.0f / window; // delay shrinks by 1 sample per sample: read speed 2 (octave up)
    if (phaseUp < 0.0f)
        phaseUp += 1.0f;
    phaseDown += 0.5f / window; // delay grows by 0.5 sample per sample: read speed 0.5 (octave down)
    if (phaseDown >= 1.0f)
        phaseDown -= 1.0f;

    if (fadePos[shift] < 1.0f)
    {
        const float a = fadePos[shift] * 0.5f * pi;
        return std::cos (a) * readShiftAt (fadeFrom[shift]) + std::sin (a) * readShiftAt (currentDelay[shift]);
    }

    return readShiftAt (currentDelay[shift]);
}

float Network::readShiftAt (float base) const noexcept
{
    const float window = shiftWindow;
    const auto& line = lines[shift];

    auto pair = [&] (float phase)
    {
        float other = phase + 0.5f;
        if (other >= 1.0f)
            other -= 1.0f;
        return line.read (base + phase * window) * std::sin (pi * phase)
             + line.read (base + other * window) * std::sin (pi * other);
    };

    return shiftUpGain * pair (phaseUp) + shiftDownGain * pair (phaseDown);
}

float Network::readMemory() noexcept
{
    // Four overlapping Hann-windowed grains, each replaying a random moment from the
    // node's recent past. A grain's delay is fixed for its life, so it plays at 1x speed.
    float sum = 0.0f;
    auto& line = lines[memory];

    for (auto& g : grains)
    {
        if (g.age >= g.length)
        {
            g.age = 0;
            g.length = grainLength;
            g.delay = nextGrainDelay();
        }

        const float w = 0.5f - 0.5f * std::cos (2.0f * pi * (float) g.age / (float) g.length);
        sum += w * line.read (g.delay + brakeOffset);
        ++g.age;
    }

    const float grabMix = itch.getMods().grabMix;
    if (grabMix > 0.0f)
        return (1.0f - grabMix) * 0.5f * sum + grabMix * readGrab();

    grabElapsed = 0;
    return 0.5f * sum;
}

float Network::readGrab() noexcept
{
    // The read position stays fixed in absolute time and loops over the L samples
    // before the grab began: delay = L + elapsed - (elapsed mod L). Each pass has short
    // fades at its edges, which gives the stutter its little gaps.
    const int loop = std::max (64, (int) (itch.getMods().grabLoopSeconds * sr));
    const int inLoop = grabElapsed % loop;
    const float delay = (float) (loop + grabElapsed - inLoop);
    ++grabElapsed;

    if (delay > 3.3f * sr) // past the Memory buffer: let the grab fade, never wrap
        return 0.0f;

    const float fade = std::min (0.004f * sr, 0.25f * (float) loop);
    const float w = std::min (1.0f, std::min ((float) inLoop, (float) (loop - inLoop)) / fade);
    return w * lines[memory].read (delay);
}

float Network::nextGrainDelay() noexcept
{
    const float r = grainRng.next01();

    // Synced: one to four quarters of the reach (itself a note value), so a replayed
    // moment lands back on the grid. Free: anywhere in the last 25-100% of the reach.
    if (memorySynced)
        return targetDelay[memory] * 0.25f * (1.0f + std::floor (r * 3.999f));

    return currentDelay[memory] * (0.25f + 0.75f * r);
}

void Network::process (const float* inL, const float* inR, float* outL, float* outR, float* dryGain, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        if (controlCounter == 0)
            updateControl();
        if (++controlCounter >= controlInterval)
            controlCounter = 0;

        const float l = inL[i], r = inR[i];
        const float mid = 0.5f * (l + r), side = 0.5f * (l - r);

        const float level = std::max (std::abs (l), std::abs (r));
        inputEnvelope += (level > inputEnvelope ? envAttack : envRelease) * (level - inputEnvelope);
        itch.analyse (mid);

        const auto& mods = itch.getMods();
        dryGain[i] = mods.dryGain;

        // Tape glide: delay times move along a one-pole curve, so a change of Size
        // bends the pitch of what's already in the loop, like a varispeed tape.
        // In jump mode a big change (a new note value, a lurch) cuts instead: the old
        // read head fades out while the new one fades in.
        for (size_t k = 0; k < (size_t) numNodes; ++k)
        {
            const bool jumpable = k == (size_t) echo || k == (size_t) shift;
            if (jumpable && jumpMode && fadePos[k] >= 1.0f
                && std::abs (targetDelay[k] - currentDelay[k]) > 0.03f * currentDelay[k])
            {
                fadeFrom[k] = currentDelay[k];
                currentDelay[k] = targetDelay[k];
                fadePos[k] = 0.0f;
            }

            currentDelay[k] += glideCoef[k] * (targetDelay[k] - currentDelay[k]);
            if (fadePos[k] < 1.0f)
                fadePos[k] = std::min (1.0f, fadePos[k] + fadeStep);
        }

        // Tape brake: fall behind while braking, catch up after.
        if (mods.braking)
            brakeOffset = std::min (brakeLimit - currentDelay[echo], brakeOffset + (1.0f - mods.brakeSpeed));
        else
            brakeOffset *= brakeCatchUp;
        brakeOffset = std::max (0.0f, brakeOffset);

        std::array<float, numNodes> y {};
        if (fadePos[echo] < 1.0f)
        {
            const float a = fadePos[echo] * 0.5f * pi;
            y[echo] = std::cos (a) * lines[echo].read (fadeFrom[echo] + brakeOffset)
                    + std::sin (a) * lines[echo].read (currentDelay[echo] + brakeOffset);
        }
        else
        {
            y[echo] = lines[echo].read (currentDelay[echo] + brakeOffset);
        }
        y[resonator] = lines[resonator].read (currentDelay[resonator]);
        y[shift] = readShift();
        y[memory] = readMemory();

        for (size_t k = 0; k < (size_t) numNodes; ++k)
        {
            float v = diffusers[k].process (y[k], diffusion * diffuserScale[k]);
            v = lowpass[k].process (v);
            v = highpass[k].process (v);
            v = std::tanh (v * drive[k]) / drive[k]; // small-signal gain 1, bounded output
            y[k] = dcBlock[k].process (v);
        }

        // Coupling: blend each node's own output with the Hadamard mix of all four.
        const float h0 = 0.5f * (y[0] + y[1] + y[2] + y[3]);
        const float h1 = 0.5f * (y[0] - y[1] + y[2] - y[3]);
        const float h2 = 0.5f * (y[0] + y[1] - y[2] - y[3]);
        const float h3 = 0.5f * (y[0] - y[1] - y[2] + y[3]);
        const std::array<float, numNodes> h { h0, h1, h2, h3 };

        float wl = 0.0f, wr = 0.0f;
        for (size_t k = 0; k < (size_t) numNodes; ++k)
        {
            const float mixed = (1.0f - coupling) * y[k] + coupling * h[k];
            const float send = k == (size_t) echo ? echoSendGain : 1.0f;
            lines[k].push (send * inputGain * (mid + side * injectSide[k]) + loopGain[k] * mixed);

            wl += outLevel[k] * panL[k] * y[k];
            wr += outLevel[k] * panR[k] * y[k];
        }

        outL[i] = wetGain * wl;
        outR[i] = wetGain * wr;
    }
}
} // namespace dendrite::dsp
