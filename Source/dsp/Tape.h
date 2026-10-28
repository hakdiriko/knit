#pragma once

#include "Primitives.h"

namespace dendrite::dsp
{
// The tape: everything the sound passes through in series. There is no dry copy
// running beside a moving copy, so nothing here can comb-filter or flange.
//
// wear. The tape is a 4-bar loop of 64 cells (16ths) in 4 bands. Each time a cell
// plays something that sounds the same as 4 or 8 bars ago, that cell loses a little
// oxide in that band. Weak stretches go first and wear speeds up as it goes, so a
// loop that never changes slowly disintegrates at fixed places, like a tape loop
// played to death. Anything new heals the tape: a new hit comes through clean at
// once, and a cell that differs from before grows back. Damage is applied through a
// fine, fixed oxide texture, so worn spots are irregular flakes, never a gate on the
// grid. Whatever wears away is handed to the room.
//
// friction. The material: tape saturation (pre/de-emphasised, so the highs give first,
// with a little asymmetry for even harmonics), 1/f wow and flutter, the tape's own
// grain riding the signal, and dust where the sound has worn away.
//
// Both age the tape as a whole: the more worn it is, the darker it gets and the more
// it drags.
class Tape
{
public:
    static constexpr int numBands = 4;
    static constexpr int cellsPerTape = 64;  // 4 bars of 16ths
    static constexpr int historyCells = 128; // 8 bars
    static constexpr int subsPerCell = 4;    // 64th notes
    static constexpr int textureSize = 1024; // 256th notes

    struct Transport
    {
        double ppq = 0.0;   // song position of the block's first sample, in quarter notes
        double bpm = 120.0;
        bool playing = true;
    };

    void prepare (float sampleRate, uint32_t seed);
    void reset();

    void setWear (float amount) noexcept;
    // A gentle curve: the ear notices the first bit of saturation and drift most, so the
    // low end of the knob moves a little faster; the top end adds roughness on its own.
    void setFriction (float amount) noexcept { friction = std::pow (std::clamp (amount, 0.0f, 1.0f), 0.6f); }

    // In place. lostL/lostR receive what wore away this block.
    void process (float* left, float* right, float* lostL, float* lostR, int numSamples, const Transport&) noexcept;

    int getLatencySamples() const noexcept { return (int) tapeDelay; }

    // For the window.
    float getAge() const noexcept { return age; }
    float getCellWear (int cell) const noexcept;
    float getTapePosition() const noexcept { return tapePosition; } // in cells, 0..64

private:
    using CellHistory = std::array<std::array<float, subsPerCell>, numBands>;

    void startCell (int cellInSong);
    void closeCell();
    void closeSub();
    void updateExpected();
    void controlTick() noexcept;
    float textureAt (double position) const noexcept;
    void buildTexture (uint32_t seed);
    static float similarity (const std::array<float, subsPerCell>&, const std::array<float, subsPerCell>&) noexcept;
    static float saturate (float x, float drive) noexcept;

    struct Channel
    {
        Crossover split1, split2, split3;
        CrossoverAllpass lowAp2, lowAp3, mid1Ap3;
        std::array<float, numBands> bandGain {};
        DelayLine delay;
        Svf emphasis, deemphasis, ageFilter;
        DcBlocker dcBlock;
        OnePoleHighpass dustHighpass;
        OnePoleLowpass dustLowpass;
        float lostEnvelope = 0.0f, envelope = 0.0f;
    };

    float sr = 48000.0f;
    std::array<Channel, 2> channels;
    PinkWalk wow, flutter;
    Rng noise;

    // The tape's state: damage per cell and band, how fragile each one is, and the
    // fine oxide texture that damage shows through.
    std::array<std::array<float, numBands>, cellsPerTape> damage {};
    std::array<std::array<float, numBands>, cellsPerTape> fragility {};
    std::array<float, textureSize> texture {};
    std::array<double, numBands> textureOffset {};

    // What each of the last 8 bars of cells sounded like: mean and peak level per 64th.
    std::array<CellHistory, historyCells> historyMean {}, historyPeak {};
    std::array<bool, historyCells> historyValid {};

    // The cell being heard now.
    int playCell = 0;            // counts cells in the order they were played
    int songCell = 0;            // the same cell's position in the song
    int sub = -1;
    int subsHeard = 0;
    bool wasPlaying = false;
    CellHistory nowMean {}, nowPeak {};
    std::array<std::array<float, numBands>, 2> energy {};
    std::array<float, numBands> subPeak {}, fastEnergy {}, fastEnvelope {}, expectedPeakDb {};
    std::array<bool, numBands> fresh {};
    int energyCount = 0;

    float wearRate = 0.0f, friction = 0.3f;
    float age = 0.0f, ageMix = 0.0f, engaged = 1.0f, tapePosition = 0.0f;
    float modulation = 0.0f, tapeDelay = 0.0f;
    float gainCoef = 0.004f, fastAttack = 0.01f, fastRelease = 0.001f;
    float lostAttack = 0.02f, lostRelease = 0.001f, envAttack = 0.05f, envRelease = 0.002f;
    float engageCoef = 0.001f;
    uint32_t sampleCounter = 0;

    static constexpr int controlInterval = 32;
};
} // namespace dendrite::dsp
