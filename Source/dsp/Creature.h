// The creature: the itch's own small mind (2.0). It lives inside dendrite from the moment
// it loads and takes one breath per sixteenth note. It hears the music (the Listener's
// bands, the hits, how repetitive it is), feels the hand (where the light is, how fast it
// moves, the throws), and breathes on three slow clocks of its own. Its 32 neurons never
// rest and never repeat: when the music bores it, the itch plays whatever the creature is
// thinking at that moment, instead of rolling dice.
//
//   senses + body rhythms + kicks  ->  32 leaky neurons  ->  14 numbers (the gesture)
//   memory (diary, tiredness, habits, loop prints)  ->  novelty, choosing the heads
//   familiarity + arousal + its own patience  ->  when it has had enough
//
// The whole mind is plain fixed-size data: no allocation, no locks, safe on the audio
// thread. Its life (Life) is saved with the set; its wiring grows again from the genome.
#pragma once

#include "Tools.h"

namespace dendrite
{
// One itch, as the creature (or the old dice) decided it. u: 14 numbers in 0..1.
//   0 register (dark .. bright)   1 pace            2 handedness     3 whirl
//   4 place around the head       5 rise / depth    6 weariness      7 scatter
//   8..12 the urge for each head (scream, laugh, manipulate, swarm, ghost)
//   13 patience (the creature's own; read by Creature::threshold)
struct Gesture
{
    static constexpr int dims = 14;
    std::array<float, dims> u {};
    std::array<int, 3> order { 0, 1, 2 };
    uint64_t texture = 1;  // seeds the fine grain (laugh jitter, grain phases, velvet taps)
    float heal = 45.0f;    // seconds: the time constant of the turned space healing
    float tape = 1.0f;     // how much of the heads goes onto the echo's tape (x 1.2's share)
    float lift = 0.0f;     // added to the itch's intensity

    // 1.2's itch: every number from dice seeded by song position, a shuffled draw of heads
    // that is never the same set twice running. Kept for A/B renders (tools/render --dice).
    static Gesture dice (uint64_t seed, int lastSetMask) noexcept;
    int setMask() const noexcept { return (1 << order[0]) | (1 << order[1]) | (1 << order[2]); }
};

class Creature
{
public:
    static constexpr int neurons = 32, senses = 11, rhythms = 3, kicks = 8, heads = 5;
    static constexpr int candidates = 32, diarySize = 64, printCount = 32, maxBarSteps = 32;

    // Everything that makes this creature this creature. Plain data, saved with the set.
    struct Life
    {
        static constexpr uint32_t magicWord = 0x6372746Eu, currentVersion = 1; // "crtn"
        uint32_t magic = magicWord, version = currentVersion, size = sizeof (Life), pad0 = 0;
        uint64_t genome = 1;
        uint64_t age = 0;      // sixteenths lived
        uint32_t itches = 0, pad1 = 0;
        float y[neurons] {};
        float rhythm[rhythms][2] {};
        float outMean[Gesture::dims] {};
        float outVar[Gesture::dims] {};
        float tired[heads] {};
        float habit[8] {};
        float arousal = 0.0f, motion = 0.0f, throwRate = 0.0f, density = 0.0f;
        float bandMean[6] {};
        int32_t bandMeanReady = 0, lastSet = 0, pad3 = 0;
        struct Print { uint64_t hash = 0; uint32_t itches = 0, seen = 0; };
        Print print[printCount] {};
        struct Entry { float u[Gesture::dims]; int8_t order[3]; int8_t frame; float ppq; uint64_t print; };
        Entry diary[diarySize] {};
        int32_t diaryCount = 0, diaryHead = 0;
        uint32_t checksum = 0, pad2 = 0;

        uint32_t sum() const noexcept; // FNV-1a over everything but the checksum
        bool valid() const noexcept;   // magic, version, size, checksum, finite, in range
    };

    // Born with a genome: its wiring grows from it, the same on every machine.
    void beBorn (uint64_t genome) noexcept;
    // A sample rate or a reset: the creature keeps its life; only the moment is cleared.
    void rest() noexcept;

    struct Senses
    {
        std::array<float, 6> bandsDb {};
        bool sounding = false;
        float onset = 0.0f;       // strongest onset this sixteenth, 0..1
        float repetition = 0.0f;  // the Listener's score, 0..1
        float feed = 0.5f, space = 0.4f;
        bool throwStarted = false;
        int stepInBar = 0, stepsPerBar = 16;
        float stepSeconds = 0.125f;
    };
    void step (const Senses&) noexcept;

    // The boredom a frame must pass to itch, now: its patience, how familiar the loop is,
    // how wound up it is. 1.2 used a fixed 0.72.
    float threshold() const noexcept;

    // It itches: the freshest recent thought, pushed away from its habits, tied to the
    // music, the heads it is least tired of. Written into the diary; it feels what it did.
    Gesture itch (double ppq, int frameChoice) noexcept;

    const Life& life() const noexcept { return me; }
    // Its life as it stands, sealed with a checksum, ready to save.
    Life saved() const noexcept
    {
        Life l = me;
        l.checksum = l.sum();
        return l;
    }
    bool adopt (const Life&) noexcept; // a saved creature; false (unchanged) if broken

    float familiarity() const noexcept { return familiar; }
    float arousal() const noexcept { return me.arousal; }
    float brightness() const noexcept { return bright; }
    float density() const noexcept { return dense; }
    float firing (int i) const noexcept { return fire[(size_t) std::clamp (i, 0, neurons - 1)]; }

private:
    void grow() noexcept;         // the wiring, from the genome
    void fingerprint() noexcept;  // once per bar
    void readOut (std::array<float, Gesture::dims>& u) const noexcept;
    float novelty (const std::array<float, Gesture::dims>& u) const noexcept;

    Life me;

    // wiring (from the genome, never saved)
    float W[neurons][neurons] {}, U[neurons][senses] {}, B[neurons][2 * rhythms] {}, K[neurons][kicks] {};
    float R[Gesture::dims][neurons] {}, theta[neurons] {}, invTau[neurons] {};
    float turnCos[rhythms] {}, turnSin[rhythms] {};
    uint64_t planes[64][(maxBarSteps * 6 + 63) / 64] {}; // the fingerprint's hyperplanes
    uint64_t grownFrom = 0;
    bool grown = false;

    // the moment (not saved)
    std::array<float, neurons> fire {}, kick {};
    std::array<std::array<float, Gesture::dims>, candidates> thought {};
    std::array<float, candidates> fresh {};
    int thoughts = 0, thoughtHead = 0;
    float lastFeed = -1.0f, lastSpace = -1.0f;
    std::array<float, 6> lastBands {};
    std::array<std::array<float, 6>, maxBarSteps> bar {};
    std::array<float, 6> barMax {};
    int barSteps = 0, barOnsets = 0;
    uint64_t currentPrint = 0;
    bool printReady = false;
    float familiar = 0.0f, bright = 0.5f, dense = 0.0f;
    std::array<float, heads> urge {};
    float patience = 0.5f;
    float goodY[neurons] {};
};
} // namespace dendrite
