#pragma once

#include "Primitives.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace viber::dsp
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

    // Renders one sub-block of wet signal. inL/inR may alias (mono).
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

private:
    void updateControl() noexcept;
    float readShift() noexcept;
    float readMemory() noexcept;

    float sr = 48000.0f;
    static constexpr int controlInterval = 16;
    int controlCounter = 0;
    bool snapOnNextUpdate = true;

    juce::SmoothedValue<float> ecologySm, sizeSm, toneSm;

    std::array<DelayLine, numNodes> lines;
    std::array<Allpass, numNodes> diffusers;
    std::array<SvfLowpass, numNodes> lowpass;
    std::array<OnePoleHighpass, numNodes> highpass;
    std::array<DcBlocker, numNodes> dcBlock;
    std::array<RandomWalk, numNodes> drift;

    // Per-node targets computed at control rate.
    std::array<float, numNodes> targetDelay {}, currentDelay {}, glideCoef {};
    std::array<float, numNodes> loopGain {}, drive {}, panL {}, panR {}, injectSide {};
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
};
} // namespace viber::dsp
