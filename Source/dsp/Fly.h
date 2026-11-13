// The fly: the itch is a real fly's nervous system. The part of its brain that answers music
// (FlyData.h: 4,431 neurons from the FlyWire connectome) runs live as Shiu et al. (2024) run the
// whole brain: leaky integrate-and-fire neurons, each connection as strong as its synapse count,
// excitatory or inhibitory by its transmitter. Nothing about its wiring is designed here.
//
// What it senses: the music in its ears (Johnston's organ on each antenna: low vibration, sound,
// and the slow push of the level; left channel left antenna) and the hits as something coming at
// it (the looming detectors of each eye; lows from below, highs from above). Each sense tires when
// the same thing keeps coming and recovers slowly, so it gets used to a loop and startles at the new.
//
// What it does moves dendrite's own echo through a Touch: its escape (the giant fiber) makes the
// tape lurch and throws the moment in, its grooming (DNg11, the antennal scratch) rubs a narrow
// band in the loop back and forth, its turning leans the space, its excitement feeds the echo.
#pragma once

#include "Hand.h"
#include "FlyData.h"
#include <array>
#include <atomic>

namespace dendrite
{
class Fly
{
public:
    static constexpr int neurons = flydata::neurons;
    static constexpr int padded = (neurons + 15) / 16 * 16; // whole vectors: the update loop needs no tail

    void prepare (float sampleRate) noexcept;
    void rest() noexcept; // asleep and rested: no activity, every sense fresh

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
    };
    const Doing& doing() const noexcept { return now; }
    uint16_t takeSpikes (int neuron) noexcept { return spikes[(size_t) neuron].exchange (0, std::memory_order_relaxed); }

private:
    void step() noexcept; // 0.5 ms of brain
    void reflexes() noexcept;

    static constexpr float dtSec = 0.5e-3f;
    static constexpr int delaySlots = 5; // 2.5 ms from spike to the next neuron

    float sr = 48000.0f, stepEvery = 24.0f, untilStep = 0.0f, awakeNow = 0.0f, emptyHz = 1000.0f;

    // the brain
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
    std::array<float, 12> drive {}, fresh {}, scale {};
    float scaleUp = 0.0f, scaleDown = 0.0f;

    // its reflexes, at the brain's pace
    float escape = 0.0f, groom = 0.0f, groomHabit = 0.0f, rub = 0.0f, lean = 0.0f, leanHabit = 0.0f, excited = 0.0f;
    float sinceJump = 10.0f, lurchLeft = 0.0f, lurchSize = 0.0f, throwLeft = 0.0f, freezeFrom = 0.0f, freezeLeft = 0.0f;
    float throwGlow = 0.0f, band = 0.0f;
    bool busyNow = false; // jumping or scratching, with hysteresis (for keep's diary and the lamps)
    Touch target, touch;
    Doing now;
};
} // namespace dendrite
