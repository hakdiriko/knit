// The itch: a listener that notices when the music keeps repeating, and three heads
// that play when it can't stand it any more.
//
// Like the three playback heads of a tape echo, the heads sit one after another along
// the tape: the same moment passes head 1, then head 2, then head 3, at gaps of
// 1, 2 and 3 quarters of the frame (the frame is the tape speed). None of them echoes
// anything. Each one is a gesture of the space itself, and all three live only in the
// Side, so they vanish in mono:
//   scream      a dichotic ghost: a narrow band of the music (500 Hz to 1.6 kHz, where
//               the ear still reads phase) is torn apart between the ears and climbs.
//               Each ear alone hears a slight resonance; both together hear a voice
//               inside the skull (the Huggins-pitch mechanism). The rest of the space
//               contracts toward it.
//   laugh       the image giggles: a few quick, decaying hops of the whole image at the
//               rate of a human laugh (about 5 per second, slowing down).
//   manipulate  the Franssen trick: the body of the sound migrates around your head and
//               spins up, while the attacks stay where they were, so the ear keeps
//               believing nothing moved. Afterwards the attacks catch up, and the new
//               place heals back over about a minute.
#pragma once

#include "Field.h"

namespace hyp
{
class Listener
{
public:
    void prepare (double sampleRate);
    void reset();
    void push (float mid);                 // every sample
    void step (int stepsPerBar);           // every sixteenth note
    float repetition() const { return score; }

private:
    static constexpr int bands = 6, ringFrames = 512;
    double sr = 48000.0;
    std::array<Biquad, bands> filters;
    std::array<float, bands> energy {};
    float envCoef = 0.0f;
    std::array<std::array<float, bands>, ringFrames> ring {};
    int written = 0;
    float score = 0.0f;
};

class Heads
{
public:
    void prepare (double sampleRate);
    void reset();
    void fire (float intensity, uint64_t seed, double gapSamples);
    bool busy() const { return clock >= 0.0 && clock < end; }

    // Advances one sample: fills the mods for the field and returns the scream's Side.
    float process (float re, float im, HeadMods& mods);

    float lamp (int head) const { return lamps[(size_t) head]; }

private:
    struct Window { double start = 0, length = 1; float phase (double t) const { return (float) ((t - start) / length); } };

    double sr = 48000.0;
    double clock = -1.0, end = 0.0;
    float strength = 0.0f;
    Dice dice;
    std::array<float, 3> lamps {};

    // scream
    Window screamWin;
    float screamFrom = 600.0f, screamRise = 0.6f, screamSide = 1.0f;
    Biquad lowRe, lowIm, highRe, highIm;
    Wander flutter;
    int screamTick = 0;

    // laugh
    Window laughWin;
    double nextHa = 0.0, haStart = -1.0, haLength = 0.0;
    float haRate = 5.0f, haSlow = 0.93f, haAmp = 0.0f, haSign = 1.0f, haDecay = 0.8f;
    int haCount = 0;

    // manipulate
    Window manipWin;
    float turnTotal = 0.0f, spinPeak = 0.0f, spinPhase = 0.0f, bloom = 0.0f;
    float scar = 0.0f, scarHeal = 0.0f;
    Glide anchorCatch;
    bool settled = true;
};
} // namespace hyp
