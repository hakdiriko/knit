// The itch ("kaşınmak"): dendrite listens for the music repeating itself. When a whole
// frame (a quarter bar to 8 bars, Hakan's drag) sounds like the bars before it, it can't
// stand it any more: it grabs the last accent of the loop and throws it to three heads
// that sit one after another along the tape, like the playback heads of a Space Echo.
// There are five heads; each itch draws three of them, in an order of its own:
//
//   scream      the accent excites a throat: two narrow resonances with feedback that
//               climb, and the howl circles your head as it rises.
//   laugh       the accent's first syllable, retriggered at the rate of a laugh (about
//               5 per second, slowing), each "ha" a little lower and softer, hopping
//               from one side of your head to the other.
//   manipulate  a hand on the reel: the accent plays back while the tape motor drags,
//               so pitch and time fall together, and it swings around your head. It
//               falls into the echo, and the echo's whole space turns with it (healing
//               over about a minute).
//   swarm       the accent's attack, scattered into a dozen grains all around you, each
//               with a pulse of its own. They pull each other into step (Kuramoto) and
//               the cloud closes into one hit in front of you, on the beat.
//   ghost       the accent's air, smeared by velvet noise into a whisper right at one
//               ear, that passes behind your head to the other.
//
// Every head is placed with the cues the ears use (Binaural, below), so "around your
// head" means around it: in front, beside, behind. Everything a head plays also goes onto
// the echo's tape, so the itch keeps echoing in the dotted trunk instead of ending like a
// sound effect. The heads are made only of the music itself, at its own level, and never
// land twice the same way.
#pragma once

#include "Creature.h"

namespace dendrite
{
// Is this sixteenth the same as the same sixteenth one, two or four bars ago? The gaps
// between the notes of a loop are part of it: a silent sixteenth keeps the score where
// the notes left it, and only more than two bars of silence (the music stopped) lets it
// fall.
class Listener
{
public:
    void prepare (float sampleRate);
    void reset();
    void push (float mid) noexcept;          // every sample
    void step (int stepsPerBar) noexcept;    // every sixteenth note
    float repetition() const noexcept { return score; }
    int silentSteps() const noexcept { return quiet; } // sixteenths since anything sounded
    const std::array<float, 6>& lastBands() const noexcept { return last; } // dB, the last sixteenth

private:
    static constexpr int bands = 6, ringFrames = 512;
    std::array<Biquad, bands> filters {};
    std::array<float, bands> energy {};
    float envCoef = 0.0f, stepPeak = 0.0f, loudestLong = -120.0f;
    std::array<std::array<float, bands>, ringFrames> ring {};
    std::array<float, bands> last {};
    int written = 0, quiet = 1 << 20;
    float score = 0.0f;
};

// One sound placed around the listener with the cues the ears use: the time difference
// between the ears (Woodworth's spherical head), the head's shadow on the far ear (Brown
// and Duda's one-pole, one-zero model: the near ear gains up to +6 dB of highs, the far
// ear loses up to 20 dB) and the pinna's dulling from behind. Azimuth in radians: 0 ahead,
// +pi/2 right, pi behind. `near` (0..1) brings the source to the ear: the level
// difference grows the way it does for a voice at arm's length or closer.
class Binaural
{
public:
    void prepare (float sampleRate);
    void clear() noexcept;
    void process (float x, float azimuth, float near, float& left, float& right) noexcept;

private:
    struct Shadow
    {
        float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        void set (float alpha, float beta) noexcept;
        float process (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 - a1 * y1;
            x1 = x;
            y1 = y;
            return y;
        }
    };
    void aim (float azimuth, float near) noexcept;

    float sr = 48000.0f, beta = 12.0f;
    Ring line;
    Shadow shadowL, shadowR;
    OnePole backL, backR;
    float delayL = 0.0f, delayR = 0.0f, targetL = 0.0f, targetR = 0.0f;
    float gainL = 1.0f, gainR = 1.0f, behind = 0.0f;
    int tick = 0;
    bool primed = false;
};

class Heads
{
public:
    // The window reads these: how many heads there are and what they are called.
    static constexpr int count = 5;
    static constexpr const char* names[count] = { "scream", "laugh", "manipulate", "swarm", "ghost" };
    enum Head { scream, laugh, manipulate, swarm, ghost };
    static constexpr int perItch = 3;

    void prepare (float sampleRate);
    void reset();

    // Every sample, before process(): the input as Mid/Side and how much of this moment
    // is an onset (0..1).
    void listen (float m, float s, float onset) noexcept;

    // Itch now, as the gesture says (the creature's, or 1.2's dice). gap: samples between
    // the heads. beat: samples per quarter note.
    void fire (const Gesture& gesture, float intensity, double gapSamples, double beatSamples, double barSamples) noexcept;
    int lastSetMask() const noexcept { return lastSet; }

    bool busy() const noexcept { return clock >= 0.0 && clock < end; }

    // Itch turned off: a running itch fades out within 100 ms.
    void stop() noexcept;

    // Per sample: what the heads play (out), what they write onto the echo's tape
    // (toTape), and how far they turn the echo's space this sample.
    void process (float& outM, float& outS, float& tapeM, float& tapeS, float& turn) noexcept;

    float lamp (int head) const noexcept { return lamps[(size_t) std::clamp (head, 0, count - 1)]; }

    // What each head played this sample, as left/right: for the offline tools that level
    // the heads against each other.
    std::array<float, count> probeL {}, probeR {};

private:
    struct Window
    {
        double start = 0, length = 0;
        bool on = false;
        float phase (double t) const noexcept { return on ? (float) ((t - start) / length) : -1.0f; }
    };

    float readCapture (float position) const noexcept;
    float readAir (float position) const noexcept;
    void copyCapture() noexcept;

    void playScream (double t, float level, float& l, float& r) noexcept;
    void playLaugh (double t, float level, float& l, float& r) noexcept;
    void playManipulate (double t, float level, float& l, float& r, float& turn) noexcept;
    void playSwarm (double t, float level, float& l, float& r) noexcept;
    void playGhost (double t, float level, float& l, float& r) noexcept;

    float sr = 48000.0f;
    Dice dice;

    // the recent past, and the onsets heard in it
    Ring historyM;
    // strength: the peak over 30 ms; bright: how much of that peak is above 2.5 kHz
    struct Onset { int64_t at = -1; float strength = 0.0f, bright = 0.0f; };
    Biquad brightFilter;
    float tapeScale = 1.0f;
    std::array<Onset, 32> onsets {};
    size_t onsetHead = 0;
    float lastOnset = 0.0f;
    int64_t now = 0;

    // the grabbed accent (Mid only: the heads place it themselves), and its air
    std::vector<float> capture, air;
    int captureLength = 0, copied = 0;
    int64_t captureFrom = 0;
    float capturePeak = 0.1f;
    Biquad airFilter1, airFilter2;

    double clock = -1.0, end = 0.0;
    float fade = 1.0f, fadeStep = 0.0f;
    bool stopping = false;
    float strength = 0.0f;
    std::array<float, count> lamps {};
    std::array<Window, count> windows {};
    int lastSet = 0;

    // scream
    float screamFrom = 600.0f, screamRise = 0.7f, screamFeedback = 0.9f, screamRms = 0.0f;
    float screamAz = 0.0f, screamTurns = 1.5f;
    float screamY1 = 0.0f, screamY2 = 0.0f, rmsCoef = 0.0f;
    Biquad throat1, throat2;
    Binaural screamPlace;
    Wander flutter;
    int screamTick = 0;

    // laugh
    double nextHa = 0.0, haStart = -1.0, haLength = 0.0;
    float haRate = 5.0f, haSlow = 0.9f, haAmp = 0.0f, haSide = 1.0f, haAz = 1.2f, haDecay = 0.8f, haSpeed = 1.0f, haPos = 0.0f;
    int haCount = 0, haMax = 5;
    Biquad vowel;
    Binaural laughPlace;

    // manipulate
    float manipPos = 0.0f, manipFloor = 0.5f, manipTurn = 1.0f, manipAz = 0.0f;
    Binaural manipPlace;

    // swarm: coupled pulses, each firing a grain of the attack from its own place
    static constexpr int swarmGrains = 12;
    struct Grain
    {
        float phase = 0.0f, rate = 4.0f, azimuth = 0.0f, position = 0.0f, speed = 1.0f, amp = 0.0f;
        int age = 0, length = 0;
        bool sounding = false;
        Binaural place;
    };
    std::array<Grain, swarmGrains> grains;
    float swarmBase = 4.0f, swarmSpread = 0.15f, swarmSync = 0.0f, swarmGrainLength = 0.05f;
    int swarmTick = 0;

    // ghost: the air, smeared by a sparse velvet-noise sequence, at the ear
    static constexpr int velvetTaps = 160;
    std::array<int, velvetTaps> velvetAt {};
    std::array<float, velvetTaps> velvetWeight {};
    float ghostPos = 0.0f, ghostFrom = 1.5f, ghostSpeed = 0.9f;
    Binaural ghostPlace;
};
} // namespace dendrite
