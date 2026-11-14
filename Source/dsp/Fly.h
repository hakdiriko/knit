// The fly: the itch is a real fly's nervous system. The part of its brain that answers music
// (FlyData.h: 11,705 neurons from the FlyWire connectome) runs live as Shiu et al. (2024) run the
// whole brain: leaky integrate-and-fire neurons, each connection as strong as its synapse count,
// excitatory or inhibitory by its transmitter. Nothing about its wiring is designed here.
//
// What it senses: the music in its ears (Johnston's organ on each antenna: low vibration, sound,
// and the slow push of the level; left channel left antenna) and the hits as something coming at
// it (the looming detectors of each eye; lows from below, highs from above). Each sense tires when
// the same thing keeps coming and recovers slowly, so it gets used to a loop and startles at the new.
//
// It smells the music too: the spectrum, as a few glomeruli of its antennal lobe, reaches the
// mushroom body, where a sparse code of Kenyon cells says what this sound is.
//
// Its inner life (2.2): every neuron is a little restless (small random kicks, as real neurons
// have); two moods move slowly, arousal (wound up by its own jumps) and fatigue (worn by busy
// music); and it remembers. When it jumps, the punishing dopamine neurons (PPL1) fire, and where
// dopamine meets a Kenyon cell that was just active, that cell's synapses onto the mushroom
// body's output neurons (MBONs) weaken. So the sounds that made it jump stop sounding safe. It
// forgets over about ten minutes.
//
// What it does moves dendrite's own echo through a Touch: its escape (the giant fiber) makes the
// tape lurch and throws the moment in, its grooming (DNg11, the antennal scratch) rubs a narrow
// band in the loop back and forth, its turning leans the space, its excitement feeds the echo.
// The moods and the memory choose how: wound up it jumps sooner and harder, tired it scratches
// less, and its MBONs (keep away against come closer) decide whether a jump dives or rises,
// how long it throws, and where it scratches.
#pragma once

#include "Hand.h"
#include "FlyData.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace dendrite
{
class Fly
{
public:
    static constexpr int neurons = flydata::neurons;
    static constexpr int padded = (neurons + 15) / 16 * 16; // whole vectors: the update loop needs no tail

    Fly();
    void prepare (float sampleRate) noexcept;
    void rest() noexcept;   // asleep and rested: no activity, every sense fresh (it keeps its memory and moods)
    void forget() noexcept; // a fly that never heard anything: memory and moods as at birth

    // What it has learned and how it feels, as saved with the set (about 57 KB): every plastic
    // KC -> MBON synapse as a share of its strength at birth, and the slow moods. Its activity,
    // senses and reflexes are not part of it: a fly brought back wakes up rested, remembering.
    struct Memory
    {
        static constexpr uint32_t tag = 0x464C5931u; // "FLY1"
        uint32_t magic = 0, count = 0;
        float arousal = 0.3f, fatigue = 0.0f, feelingHabit = 0.0f;
        double lived = 0.0; // seconds of inner life so far
        std::array<uint16_t, flydata::plasticCount> strength {};
        bool valid() const noexcept;
    };
    void save (Memory&) const noexcept;  // audio thread (or wherever the fly runs)
    void adopt (const Memory&) noexcept; // between two blocks; a memory that is not valid is ignored
    double lived() const noexcept { return livedSec; }

    // Its temperament: the few numbers taste training moves (tools/fly/taste/). The defaults are
    // where Hakan's rounds 1 and 2 put them (2026-11-14; before: 0.06, 600, 10, 1, 0.85, 0.4, 0.35, 40).
    // jumpy and lurch are near their old values for 2.4: the rounds asked for fewer moves when every move was the same wobble.
    // lurch: since 2.4, how readily a jump becomes a big move (the intervals themselves are fixed).
    struct Taste
    {
        float learn = 0.075f;    // how fast what hurt it becomes fearful
        float forget = 640.0f;   // seconds to forget
        float restless = 7.2f;   // random kicks per neuron per second
        float jumpy = 0.8f;      // >1 jumps more easily
        float scratch = 0.95f;   // how hard it grooms the band
        float lurch = 0.4f;      // octaves of tape at a full jump
        float lean = 0.45f;      // how far it leans the space
        float eyesTire = 4.0f;   // how fast its eyes tire of hits (use per second at full drive; 12 and 10 s until 2.4, when a steady groove blinded it)
        float eyesBack = 6.0f;   // seconds for them to recover
        float calm = 42.0f;      // seconds for arousal to settle
    };
    void setTaste (const Taste& t) noexcept
    {
        taste = t;
        taste.forget = std::max (taste.forget, 1.0f);
        taste.restless = std::max (taste.restless, 0.01f);
        taste.jumpy = std::max (taste.jumpy, 0.05f);
        taste.calm = std::max (taste.calm, 1.0f);
    }
    const Taste& getTaste() const noexcept { return taste; }

    // The song's clock (2.4): the beat now, in quarter notes, and the tempo. Its brain decides what
    // to do and how hard; its body does it on the grid, as a real fly's steering commands only take
    // hold at one phase of the wingbeat (the halteres keep that time). Here the song is the wingbeat.
    void setClock (double beat, double bpm) noexcept
    {
        beatNow = beat;
        bpmNow = bpm;
    }
    // false: the 2.3 fly (moves whenever its neurons say so, one sine lurch of the tape)
    void setMusical (bool on) noexcept { musical = on; }
    // what its body can do with the tape; each bends the repeats by exact intervals, for exact note values
    enum Move { none, hop, leap, duck, dive, rush };

    // false: the 2.1.0 fly (no restlessness, no moods, no learning), for comparison
    void setInnerLife (bool on) noexcept { innerLife = on; }

    // awake 0..1 (0 = asleep, the fly does nothing). emptyHz: where the music leaves room for the band.
    void setAwake (float awake) noexcept { awakeNow = clamp01 (awake); }
    void setEmptyHz (float hz) noexcept { emptyHz = hz; }

    // Per sample, the input as it comes in. Returns the touch for this sample.
    const Touch& process (float left, float right) noexcept;

    bool busy() const noexcept { return busyNow; }

    // For the window: what it is doing, and every neuron's recent spikes (the editor takes them).
    struct Doing
    {
        float jump = 0.0f;      // 0..1 while it jumps (the tape's lurch)
        float semitones = 0.0f; // the tape's speed now
        bool throwing = false, frozen = false;
        float scratch = 0.0f, scratchHz = 1000.0f; // the band
        float turn = 0.0f;                         // -1 right .. 1 left
        float arousal = 0.0f, fatigue = 0.0f;      // its moods, 0..1
        float memory = 1.0f;                       // the plastic synapses' strength against at birth
        float feeling = 0.0f;                      // its MBONs: -1 keep away .. 1 come closer
        float fear = 0.0f;                         // 0..1: how much of what it smells now once hurt
        int move = 0;                              // which Move it is in the middle of
    };
    const Doing& doing() const noexcept { return now; }
    uint16_t takeSpikes (int neuron) noexcept { return spikes[(size_t) neuron].exchange (0, std::memory_order_relaxed); }

private:
    void step() noexcept; // 0.5 ms of brain
    void reflexes() noexcept;
    void smell() noexcept; // every 5 ms
    void live() noexcept;  // every 10 ms: traces, moods, learning
    float uniform() noexcept // (0, 1]
    {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        return ((float) (rng >> 40) + 1.0f) * (1.0f / 16777216.0f);
    }

    static constexpr float dtSec = 0.5e-3f;
    static constexpr int delaySlots = 5; // 2.5 ms from spike to the next neuron

    float sr = 48000.0f, stepEvery = 24.0f, untilStep = 0.0f, awakeNow = 0.0f, emptyHz = 1000.0f;

    // the brain (w: every connection's strength in mV; the KC -> MBON ones change as it learns)
    std::vector<float> w;
    std::array<float, padded> v {}, g {};
    std::array<float, padded> refractory {}; // steps left resting
    std::array<std::array<uint16_t, neurons>, delaySlots> fired {};
    std::array<int, delaySlots> firedCount {};
    int slot = 0;
    uint64_t rng = 0x666C792E322E3031ull;
    std::array<std::atomic<uint16_t>, neurons> spikes {};

    // the senses
    struct Ear
    {
        Biquad hp, lp;
        float env = 0.0f, a = 0.0f;
        float run (float x) noexcept { env += a * (std::abs (lp.process (hp.process (x))) - env); return env; }
    };
    struct Eye
    {
        Biquad hp, lp;
        float fast = 0.0f, slow = 0.0f, af = 0.0f, as = 0.0f;
        float run (float x) noexcept
        {
            const float y = std::abs (lp.process (hp.process (x)));
            fast += af * (y - fast);
            slow += as * (y - slow);
            return 0.0f;
        }
        float looming() const noexcept { return std::max (0.0f, std::log ((fast + 1.0e-4f) / (slow + 1.0e-4f))); }
    };
    std::array<Ear, 6> ears;      // B, A, CE per side
    std::array<Eye, 6> eyes;      // low, mid, high per side
    static constexpr int inputs = flydata::senses + flydata::smells;
    std::array<float, inputs> drive {}, fresh {};
    std::array<float, 6> scale {};
    float scaleUp = 0.0f, scaleDown = 0.0f;

    // the nose: the spectrum in bands, each heard against its own recent loudest
    struct Band
    {
        Biquad bp;
        float env = 0.0f, peak = 0.0f;
    };
    std::array<Band, flydata::smells> nose;
    float noseA = 0.0f, peakUp = 0.0f, peakDown = 0.0f;
    int untilSmell = 0, untilLive = 0;

    // its inner life
    bool innerLife = true;
    Taste taste;
    std::vector<int16_t> role; // per neuron: Kenyon cell k (k), DAN d (-2 - d), MBON m (-10000 - m), else -1
    std::array<float, flydata::kenyonCount> kcTrace {};
    std::array<float, flydata::danCount> danTrace {};
    std::array<float, flydata::mbonCount> mbonSpikes {}, mbonRate {}, dopamine {};
    float kickNext = 0.0f;
    int stepSpikes = 0;
    float brainBusy = 0.0f, arousal = 0.3f, fatigue = 0.0f, memory = 1.0f, plasticBirth = 1.0f;
    float feeling = 0.0f, feelingHabit = 0.0f, fear = 0.0f, fearNow = 0.0f;
    double livedSec = 0.0;
    // fatigue grows while the brain is busier than this (spikes per second, the whole cut brain)
    static constexpr float fatigueFrom = 40000.0f, fatigueSpan = 100000.0f;

    // its reflexes, at the brain's pace
    float escape = 0.0f, groom = 0.0f, groomHabit = 0.0f, rub = 0.0f, lean = 0.0f, leanHabit = 0.0f, excited = 0.0f;
    float sinceJump = 10.0f, lurchLeft = 0.0f, lurchSize = 0.0f, throwLeft = 0.0f, freezeFrom = 0.0f, freezeLeft = 0.0f;
    float throwGlow = 0.0f, band = 0.0f, lurchSign = 1.0f, lurchLength = 0.9f, readiness = 0.0f;
    static constexpr float fearFull = 0.35f; // this much weaker counts as full fear
    bool busyNow = false; // jumping or scratching, with hysteresis (for keep's diary and the lamps)
    Touch target, touch;

    // its body in time (2.4)
    void startMove (float size, float f) noexcept;
    bool musical = true;
    double beatNow = 0.0, bpmNow = 120.0;
    long long lastTick = -1;
    bool armed = false;
    float armedSize = 0.0f, armedFear = 0.0f;
    struct Leg { float slope = 0.0f, left = 0.0f, end = 1.0f; }; // per 16th: how the heads' distance changes, for how long, where it ends
    std::array<Leg, 3> legs {};
    int legCount = 0, legAt = 0, moveNow = none;
    float stretch = 1.0f, moveSize = 0.0f; // the heads' distance against the grid's (1 / tape speed)
    float sinceMove16 = 64.0f;
    float throw16 = 0.0f, freezeIn16 = -1.0f, freeze16 = 0.0f;
    float heldBand = 0.0f, heldBandHz = 1000.0f, heldLean = 0.0f, heldFeed = 0.0f;
    float touchGlide = 0.005f;
    Doing now;
};
} // namespace dendrite
