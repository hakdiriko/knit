#pragma once

#include "Primitives.h"

namespace dendrite::dsp
{
// What the itch engine asks the network to do. Refreshed every control tick.
struct ItchMods
{
    std::array<float, 4> breath {}; // per node, depth already applied (0 = still)
    float echoSend = 0.0f;          // extra input gain into the Echo node (Throw, dub drop)
    float echoLoopAdd = 0.0f;       // added to the Echo loop gain (Throw, dub drop)
    float loopAdd = 0.0f;           // added to every loop gain (Runaway)
    float loopScale = 1.0f;         // multiplies every loop gain (the cut after a Runaway)
    float couplingAdd = 0.0f;
    float dryGain = 1.0f;           // dub drop: the dry ducks, the echoes stay
    float lurchRatio = 1.0f;        // Echo and Shift times x ratio, as a clean jump
    bool braking = false;           // tape brake: read speed below 1 while true
    float brakeSpeed = 1.0f;
    float grabMix = 0.0f;           // the Memory node loops one short moment
    float grabLoopSeconds = 0.25f;
    float retuneRatio = 1.0f;       // Resonator pitch ratio
    bool holdGain = false;          // freeze auto-gain, so a gesture is heard as a gesture
    float scarDetune = 0.0f;        // semitones, Resonator
    float scarEcho = 0.0f;          // relative, Echo time (free time only)
    float scarTone = 0.0f;          // octaves, every lowpass
};

// The itch: an integrate-and-fire neuron that gets bored.
//
//   listen   8 band envelopes, 20 frames a second. Sameness = how little the recent
//            spectrum differs from the long-term one, plus (with a tempo) how much
//            this bar looks like the last one.
//   build    a leaky integrator: sameness fills it, novelty and moving knobs drain it.
//   fire     past a slightly noisy threshold, on the next 4-bar boundary (2 bars when
//            compulsive), or at once without a running transport.
//   scratch  one dub-desk gesture, chosen by what the input is (hits or sustain).
//   relief   breathing drops below normal for a few seconds and nothing builds.
//   scar     each scratch leaves a small offset that heals over minutes; 15% stays.
//
// Under it, every node breathes: a 1/f walk plus a phase oscillator. The oscillators
// pull on each other (Kuramoto) with a strength that follows Ecology, so past a
// critical coupling the four wobbles lock and the whole network swells as one.
//
// Everything depends only on the audio, the knobs, the song position and the seed,
// so playing from the same bar gives the same scratches.
class Itch
{
public:
    enum Gesture { none = -1, throwEcho = 0, runaway, dubDrop, lurch, brake, grab, retune, numGestures };
    static const char* gestureName (int gesture) noexcept;

    struct Transport
    {
        double ppq = 0.0, bpm = 0.0, barQuarters = 4.0;
        bool playing = false;
    };

    void prepare (float sampleRate, float controlRate);
    void reset(); // dynamics only: the scars survive

    void setAmount (float itch) noexcept { amount = itch; }
    void setKnobsMoving (bool moving) noexcept { knobsMoving = moving; }
    void setTransport (const Transport&) noexcept;
    void triggerManual() noexcept { manualPending = true; }

    void analyse (float mono) noexcept; // every sample
    void tick (float ecology) noexcept; // every control tick
    const ItchMods& getMods() const noexcept { return mods; }

    float getLevel() const noexcept { return std::min (1.0f, level / threshold); }
    int getActiveGesture() const noexcept { return active; }
    float getSameness() const noexcept { return sameness; }
    uint32_t getScratchCount() const noexcept { return scratchCount; }

    // Three transient scars then three permanent ones: detune, echo, tone.
    std::array<float, 6> getScars() const noexcept;
    void setScars (const std::array<float, 6>&) noexcept;

private:
    static constexpr int numBands = 8;
    static constexpr int historyFrames = 512; // 25.6 s of frames

    void analyseFrame() noexcept;
    void start (int gesture, bool manual) noexcept;
    int chooseGesture() noexcept;
    void updateGesture() noexcept;
    void addScar() noexcept;
    void restartDynamics (uint32_t seed) noexcept;

    float sr = 48000.0f, controlRate = 3000.0f, dt = 1.0f / 3000.0f;
    float amount = 0.0f;
    bool knobsMoving = false, manualPending = false;

    // Transport, advanced every tick between host updates.
    Transport transport;
    bool wasPlaying = false;
    long long lastPhrase = 0;

    // Listen
    std::array<SvfBandpass, numBands> bands;
    std::array<float, numBands> energy {}, shortDb {}, longDb {};
    std::array<std::array<float, numBands>, historyFrames> history {};
    int hopSamples = 2400, hopCounter = 0, historyPos = 0, framesSeen = 0, soundFrames = 0;
    float hopSeconds = 0.05f;
    float specSim = 0.0f, barSim = 0.0f, sameness = 0.0f;
    float prevBroadband = -100.0f, onsetRate = 0.0f, transient = 0.0f;
    float idleSeconds = 0.0f;

    // Build and fire
    float level = 0.0f, threshold = 1.0f, armedSeconds = 0.0f;
    float relief = 0.0f, holdSeconds = 0.0f;
    Rng rng;

    // Scratch
    int active = none, lastGesture = none;
    float elapsed = 0.0f, length = 0.0f, depth = 0.0f;
    float beat = 0.5f, bar = 2.0f, ratio = 1.0f, dropBeats = 1.0f, grabLoop = 0.25f, grabHold = 1.0f;
    uint32_t scratchCount = 0;

    // Scars: index 0 detune, 1 echo, 2 tone.
    std::array<float, 3> scarNow {}, scarKept {};
    float healCoef = 1.0f, fastHealCoef = 1.0f;

    // Breathing
    std::array<PinkWalk, 4> pink;
    std::array<float, 4> phase {};

    ItchMods mods;
};
} // namespace dendrite::dsp
