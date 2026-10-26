#pragma once

#include "Itch.h"
#include "Primitives.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace dendrite::dsp
{
// The "ecosystem": four nodes (Echo, Resonator, Shift, Memory), each a delay with a
// filter, a soft saturator and a DC blocker in its loop. A coupling matrix mixes
// every node's output back into every node's input.
//
//   feedback = diag(g) * ((1 - c) * I + c * H) * y
//
// H is the 4x4 Hadamard matrix scaled by 0.5 (orthogonal: it mixes energy without
// changing the total). c comes from Ecology and decides how much the nodes talk;
// g is the loop gain per node, a separate Ecology path, so space and sustain are
// tuned independently. Above g = 1 the saturators hold the loop at the level where
// their average gain falls back to 1: a self-sustaining texture that stays bounded.
class Network
{
public:
    static constexpr int numNodes = 4;
    enum Node { echo = 0, resonator, shift, memory };

    void prepare (double sampleRate);
    void reset();

    // Targets for the three macros, each 0..1. Smoothed internally.
    void setMacros (float ecology, float size, float tone) noexcept;

    // Sync: the Echo, Shift and Memory delay times snap to the note value nearest to
    // where Size puts them, at the host tempo. bpm <= 0 (no play head or no tempo)
    // leaves them free. The Resonator always stays free, so it keeps its pitch.
    void setSync (bool enabled, double bpm) noexcept;

    // Steady: delay times never wobble, and a change of time (a Size move, a note
    // value, a step) crossfades between two read heads instead of bending the pitch,
    // like Live's Delay in Fade mode. Off, it's tape: times breathe and bend.
    void setSteady (bool on) noexcept { steady = on; }

    // The itch (see Itch.h). Amount is the Itch knob, 0..1.
    void setItch (float amount, bool autoMoves, int patienceBars, bool build, bool knobsMoving) noexcept;
    void setTransport (const Itch::Transport& t) noexcept { itch.setTransport (t); }
    void triggerScratch() noexcept { itch.triggerManual(); }
    const Itch& getItch() const noexcept { return itch; }
    Itch& getItch() noexcept { return itch; }

    // The Echo node's current note value as an index for noteName(), or -1 when free.
    int getEchoNote() const noexcept { return echoNote; }
    static const char* noteName (int index) noexcept;

    // Renders one sub-block of wet signal. inL/inR may alias (mono).
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

private:
    void updateControl() noexcept;
    float readShift() noexcept;
    float readShiftAt (float base) const noexcept;
    float readMemory() noexcept;
    float nextGrainDelay() noexcept;

    float sr = 48000.0f;
    static constexpr int controlInterval = 16;
    int controlCounter = 0;
    bool snapOnNextUpdate = true;

    juce::SmoothedValue<float> ecologySm, sizeSm, toneSm;

    bool syncEnabled = false, memorySynced = false, steady = false;
    float beatSeconds = 0.0f;
    int echoNote = -1;

    std::array<DelayLine, numNodes> lines;
    std::array<Allpass, numNodes> diffusers;
    std::array<SvfLowpass, numNodes> lowpass;
    std::array<OnePoleHighpass, numNodes> highpass;
    std::array<DcBlocker, numNodes> dcBlock;
    std::array<RandomWalk, numNodes> drift;

    // Per-node targets computed at control rate.
    std::array<float, numNodes> targetDelay {}, currentDelay {}, glideCoef {};
    std::array<float, numNodes> loopGain {}, drive {}, panL {}, panR {}, injectSide {}, outLevel {};
    float coupling = 0.0f, diffusion = 0.0f;

    // Shift node: two pairs of crossfaded read heads (octave up, octave down).
    float shiftWindow = 2400.0f;
    float phaseUp = 0.0f, phaseDown = 0.0f;
    float shiftUpGain = 0.7f, shiftDownGain = 0.7f;

    // Memory node: grains read from the node's recent past.
    struct Grain
    {
        float delay = 0.0f;
        int age = 0, length = 1;
    };
    std::array<Grain, 4> grains;
    float memoryReach = 0.0f;
    int grainLength = 4800;
    Rng grainRng;

    // The network feeds on its input: when the input has been silent for a while,
    // loop gains above `starveGain` ease down, so a self-sustaining drone fades out
    // instead of running forever.
    float inputEnvelope = 0.0f, presence = 1.0f;
    float envAttack = 0.0f, envRelease = 0.0f, presenceCoef = 0.0f;

    // The itch.
    Itch itch;
    bool jumpMode = false;

    // Phase guard. Dry plus a delayed copy whose time keeps moving is a comb filter
    // whose notches sweep: flanging. Drums (broadband, sharp) expose it most, so on
    // percussive input the delay-time wobble shrinks, and Steady removes it.
    float wobble = 1.0f, wobbleCoef = 0.0f;

    // Clean jumps for Echo and Shift: the old read head fades out while the new one
    // fades in (equal power, 25 ms), so a time change has no pitch bend and no click.
    std::array<float, numNodes> fadeFrom {}, fadePos {};
    float fadeStep = 0.001f;
};
} // namespace dendrite::dsp
