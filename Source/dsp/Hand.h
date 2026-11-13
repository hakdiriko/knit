// The hand (2.1): the itch as a motif. When the music bores the creature, it takes
// dendrite's own controls for the length of the itch frame and moves several of them as
// one gesture, then hands them back exactly where they were. Nothing it does reaches the
// parameters: it writes offsets that the echo adds on top (Echo::Touch).
//
// Its handwriting: Hollerbach (1981) made handwriting from coupled oscillators, a person's
// style being how the oscillators are set. Each creature grows four coupled phase
// oscillators from its genome (their pace in cycles per bar, how they pull on each other)
// and a readout that couples them to the desk's controls (which controls move together,
// the shape of the curves, how far it reaches, how early or late it writes). A motif is
// one run of that generator: the creature's thought of the moment (the Gesture) sets the
// strokes' sizes and starting phases, and the music steers the pen while it writes (a hit
// pulls the oscillators into step with it). Same hand every time, never the same word.
#pragma once

#include "Tools.h"

namespace dendrite
{
struct Gesture;

// What the hand does to the echo, per sample. Neutral = no touch at all.
struct Touch
{
    float speed = 1.0f;     // tape speed: the heads' distance multiplied by 1 / speed
    float feed = 0.0f;      // added to feed (0..1 range)
    float space = 0.0f;     // added to space
    float band = 0.0f;      // 0..1: how much of the loop passes through a narrow band
    float bandHz = 1000.0f; // the band's centre
    float freeze = 0.0f;    // 0..1: the send closes and the loop holds what it has
    bool throwing = false;  // it throws the moment into the echo by itself
    float lift = 1.0f;      // the echo's return ridden up while it writes (gain)
};

class Hand
{
public:
    static constexpr int oscillators = 4, channels = 6;
    enum Channel { cSpeed, cFeed, cSpace, cBandPlace, cBandAmount, cFreeze };

    void prepare (float sampleRate) noexcept;
    void grow (uint64_t genome) noexcept; // the style, from the genome (same on every machine)
    void rest() noexcept;                 // let go at once (reset)

    // A motif. lengthSamples: the whole gesture, the hand-back included. barSamples: one bar.
    // emptyHz: the centre of the band the music leaves emptiest. reach: 0..1 how far it may go
    // (arousal and boredom). brightness: 0..1 of the music.
    void start (const Gesture& g, double lengthSamples, double barSamples, float emptyHz, float reach,
                float brightness) noexcept;

    // Per sample, with the music's onset (0..1). Returns the touch for this sample.
    const Touch& process (float onset) noexcept;

    bool busy() const noexcept { return remaining > 0.0; }
    // For the window and the tools: the stroke as the light would draw it (feed, space).
    float strokeFeed() const noexcept { return touch.feed; }
    float strokeSpace() const noexcept { return touch.space; }
    float envelope() const noexcept { return env; }
    const Touch& now() const noexcept { return touch; }

private:
    void control() noexcept; // every 16 samples

    float sr = 48000.0f;
    // the style
    float pace[oscillators] {};                  // cycles per bar
    float pull[oscillators][oscillators] {};     // coupling
    float weight[channels][oscillators] {};      // readout
    float offset[channels][oscillators] {};      // readout phase
    float use[channels] {};                      // how much this creature uses each control
    float snap[channels] {};                     // curve shape: 1 soft .. 4 hard
    float lean = 0.0f;                           // writes early (<0) or late (>0), in radians
    float attack = 0.15f, release = 0.3f;        // fractions of the motif
    float throwFor = 0.06f, ride = 0.6f;         // how long it throws in at the start; how far it rides the return
    bool grown = false;

    // the motif
    float phase[oscillators] {}, amp[oscillators] {};
    float channelGain[channels] {};
    float speedSign = 1.0f, reachNow = 0.0f, brightNow = 0.5f, emptyNow = 1000.0f;
    double length = 0.0, remaining = 0.0, bar = 96000.0;
    float env = 0.0f, kick = 0.0f;
    int tick = 0;
    bool frozen = false, threw = false, armed = true;

    // the output, smoothed
    Touch target, touch;
    float cSmooth = 0.01f, speedRate = 0.0f;
};
} // namespace dendrite
