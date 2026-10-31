// The itch ("kaşınmak"): dendrite listens for the music repeating itself. When a whole
// frame (1 to 8 bars, Hakan's drag) sounds like the bars before it, it can't stand it
// any more: it grabs the last accent of the loop and throws it to three heads that sit
// one after another along the tape, like the three playback heads of a Space Echo.
// The same moment passes head 1, then head 2, then head 3:
//
//   scream      the accent excites a throat: two narrow resonances with feedback that
//               climb, so the loop's own material howls like a feedback scream. It sits
//               between the ears (turned about 90 degrees, fluttering), a voice inside
//               the skull.
//   laugh       the accent's first syllable, retriggered at the rate of a laugh (about
//               5 per second, slowing), each "ha" a little lower and softer, bouncing
//               between the ears.
//   manipulate  a hand on the reel: the accent plays back while the tape motor drags,
//               so pitch and time fall together, and it swings around your head. It
//               falls into the echo, and the echo's whole space turns with it (healing
//               over about a minute).
//
// Everything a head plays also goes onto the echo's tape, so the itch keeps echoing in
// the dotted trunk instead of ending like a sound effect. The heads are made only of the
// music itself, at its own level, and never land twice the same way.
#pragma once

#include "Tools.h"

namespace dendrite
{
// Is this sixteenth the same as the same sixteenth one, two or four bars ago?
class Listener
{
public:
    void prepare (float sampleRate);
    void reset();
    void push (float mid) noexcept;          // every sample
    void step (int stepsPerBar) noexcept;    // every sixteenth note
    float repetition() const noexcept { return score; }

private:
    static constexpr int bands = 6, ringFrames = 512;
    std::array<Biquad, bands> filters {};
    std::array<float, bands> energy {};
    float envCoef = 0.0f;
    std::array<std::array<float, bands>, ringFrames> ring {};
    int written = 0;
    float score = 0.0f;
};

class Heads
{
public:
    void prepare (float sampleRate);
    void reset();

    // Every sample, before process(): the input as Mid/Side and how much of this moment
    // is an onset (0..1).
    void listen (float m, float s, float onset) noexcept;

    // Itch now. gap: samples between the heads. beat: samples per quarter note.
    void fire (float intensity, uint64_t seed, double gapSamples, double beatSamples, double barSamples) noexcept;

    bool busy() const noexcept { return clock >= 0.0 && clock < end; }

    // Per sample: what the heads play (out), what they write onto the echo's tape
    // (toTape), and how far they turn the echo's space this sample.
    void process (float& outM, float& outS, float& tapeM, float& tapeS, float& turn) noexcept;

    float lamp (int head) const noexcept { return lamps[(size_t) std::clamp (head, 0, 2)]; }

private:
    struct Window
    {
        double start = 0, length = 1;
        float phase (double t) const noexcept { return (float) ((t - start) / length); }
    };

    float readCapture (float position) const noexcept;
    void copyCapture() noexcept;

    float sr = 48000.0f;
    Dice dice;

    // the recent past, and the onsets heard in it
    Ring historyM, historyS;
    struct Onset { int64_t at = -1; float strength = 0.0f; };
    std::array<Onset, 32> onsets {};
    size_t onsetHead = 0;
    float lastOnset = 0.0f, levelEnv = 0.0f;
    int64_t now = 0;

    // the grabbed accent (Mid only: the heads place it themselves)
    std::vector<float> capture;
    int captureLength = 0, copied = 0;
    int64_t captureFrom = 0;
    float capturePeak = 0.1f;

    double clock = -1.0, end = 0.0;
    float strength = 0.0f;
    std::array<float, 3> lamps {};

    // scream
    Window screamWin;
    float screamFrom = 600.0f, screamRise = 0.7f, screamSide = 1.0f, screamFeedback = 0.9f, screamRms = 0.0f;
    float screamY1 = 0.0f, screamY2 = 0.0f, rmsCoef = 0.0f;
    Biquad throat1, throat2;
    PhasePair screamPair;
    Wander flutter;
    int screamTick = 0;

    // laugh
    Window laughWin;
    double nextHa = 0.0, haStart = -1.0, haLength = 0.0;
    float haRate = 5.0f, haSlow = 0.9f, haAmp = 0.0f, haSign = 1.0f, haDecay = 0.8f, haSpeed = 1.0f, haPos = 0.0f;
    int haCount = 0, haMax = 5;
    Biquad vowel;

    // manipulate
    Window manipWin;
    float manipPos = 0.0f, manipFloor = 0.5f, manipTurn = 1.0f;
    PhasePair manipPair;
};
} // namespace dendrite
