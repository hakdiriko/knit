#pragma once

#include "Primitives.h"

namespace dendrite::dsp
{
// What the itch asks the network to do. Refreshed every control tick.
struct ItchMods
{
    std::array<float, 4> breath {};                  // per node, depth already applied (0 = still)
    float ecologyAdd = 0.0f;                         // a move leans Ecology up (open) or down (close)
    float sizeRatio = 1.0f;                          // a step: Echo, Shift and Memory times x ratio
    float toneAdd = 0.0f;                            // octaves, the whole network's tone
    std::array<float, 4> nodeGain { 1.0f, 1.0f, 1.0f, 1.0f }; // a lean brings one node forward
    float scarDetune = 0.0f;                         // semitones, Resonator
    float scarEcho = 0.0f;                           // relative, Echo time (free time only)
    float scarTone = 0.0f;                           // octaves, every lowpass
};

// The itch: an integrate-and-fire neuron that gets bored of repetition.
//
//   listen   8 band envelopes (80 Hz to 10 kHz, one per octave), 20 frames a second.
//            With a running transport, each frame is compared with the same moment
//            1, 2, 4 and 8 bars earlier and the best match is the repeat score, so a
//            1-bar loop and a 4-bar phrase both count as repeating. Without one, the
//            score is how steady the spectrum is (the last 2 s against the last 10 s).
//   count    bar by bar, the repeat score fills a counter. It is full after
//            `patience` bars of exact repetition. New material empties it within about
//            two bars, and moving a knob drains it.
//   build    (optional) while the counter fills, the network already leans a little
//            toward the move it is going to make, so the move is felt coming.
//   fire     when the counter is full: on the next 4-bar line (or at once without a
//            running transport). Only when Auto is on; the Scratch button always fires.
//   move     the network turns its own knobs: open, close, step, lean or tilt. The move
//            arrives over a beat, holds for a few bars, then returns over two.
//   relief   breathing drops below normal for a few seconds and nothing builds.
//   scar     each move leaves a small offset that heals over minutes; 15% stays.
//
// Under it, every node breathes: a 1/f walk plus a phase oscillator. The oscillators
// pull on each other (Kuramoto) with a strength that follows Ecology, so past a
// critical coupling the four wobbles lock and the whole network swells as one.
//
// Everything depends only on the audio, the knobs, the song position and the seed,
// so playing from the same bar gives the same moves.
class Itch
{
public:
    enum Move { none = -1, open = 0, close, step, lean, tilt, numMoves };
    static const char* moveName (int move) noexcept;

    struct Transport
    {
        double ppq = 0.0, bpm = 0.0, barQuarters = 4.0;
        bool playing = false;
    };

    void prepare (float sampleRate, float controlRate);
    void reset(); // dynamics only: the scars survive

    void setAmount (float itch) noexcept { amount = itch; }
    void setAuto (bool on) noexcept { autoOn = on; }
    void setPatience (int bars) noexcept { patience = std::max (1, bars); }
    void setBuild (bool on) noexcept { buildOn = on; }
    void setKnobsMoving (bool moving) noexcept { knobsMoving = moving; }
    void setTransport (const Transport&) noexcept;
    void triggerManual() noexcept { manualPending = true; }

    void analyse (float mono) noexcept; // every sample
    void tick (float ecology) noexcept; // every control tick
    const ItchMods& getMods() const noexcept { return mods; }

    float getLevel() const noexcept { return std::min (1.0f, level / threshold); }
    float getBarsHeard() const noexcept { return getLevel() * (float) patience; }
    int getPatience() const noexcept { return patience; }
    bool isAuto() const noexcept { return autoOn; }
    int getActiveMove() const noexcept { return stage == idle ? none : current; }
    float getTransient() const noexcept { return transient; }
    float getSameness() const noexcept { return sameness; }
    uint32_t getScratchCount() const noexcept { return scratchCount; }

    // Three transient scars then three permanent ones: detune, echo, tone.
    std::array<float, 6> getScars() const noexcept;
    void setScars (const std::array<float, 6>&) noexcept;

private:
    static constexpr int numBands = 8;
    static constexpr int historyFrames = 1024; // 51 s of frames: 8 bars down to about 38 BPM

    enum Stage { idle, arriving, holding, returning };

    void analyseFrame() noexcept;
    float repeatScore (const std::array<float, numBands>& db) const noexcept;
    float barSeconds() const noexcept;
    bool hasTempo() const noexcept { return transport.playing && transport.bpm > 0.0; }

    int chooseMove() noexcept;
    void prepareMove (int move, float depth) noexcept;
    void fire (bool manual) noexcept;
    void updateMove() noexcept;
    void addScar() noexcept;
    void restartDynamics (uint32_t seed) noexcept;

    float sr = 48000.0f, controlRate = 3000.0f, dt = 1.0f / 3000.0f;
    float amount = 0.0f;
    int patience = 16;
    bool autoOn = false, buildOn = true, knobsMoving = false, manualPending = false;

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
    float specSim = 0.0f, barSim = 0.0f, fastSim = 0.0f, sameness = 0.0f;
    float prevBroadband = -100.0f, onsetRate = 0.0f, transient = 0.0f;

    // Count and fire
    float level = 0.0f, threshold = 1.0f, armedSeconds = 0.0f, relief = 0.0f;
    Rng rng;

    // The move: chosen early (so the build can lean toward it), then played.
    struct Offsets
    {
        float ecology = 0.0f, ratio = 1.0f, tone = 0.0f;
        std::array<float, 4> gain { 1.0f, 1.0f, 1.0f, 1.0f };
    };
    Stage stage = idle;
    int current = none, pending = none, lastMove = none;
    bool manualMove = false;
    Offsets target, applied;
    float moveAmount = 0.0f, stageTime = 0.0f, arriveFrom = 0.0f;
    float arriveSeconds = 0.5f, holdSeconds = 8.0f, returnSeconds = 4.0f;
    float appliedCoef = 0.0f;
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
