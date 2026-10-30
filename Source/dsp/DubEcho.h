#pragma once

#include "Primitives.h"

namespace dendrite::dsp
{
// The dotted dub echo (0.5.0).
//
// One tape loop per side. Two heads read it: a dotted eighth on the same side and a
// quarter crossed to the other side, so every repeat branches in two and the echo
// thickens into its own space, with no reverb after it. Everything written to the
// loop, the input and the feedback alike, goes through one pass of tape: gentle
// saturation, lows and highs lost, and a little drifting diffusion, so every pass
// is softer and more smeared than the one before.
//
// Around the loop:
// - breathing: the echo ducks under what's playing and blooms in the gaps, leans a
//   few ms late when the part is busy, and swells and exhales on its own;
// - filling: when the music stops, the feedback rises and keeps the last sound
//   alive for up to 4 bars;
// - throws: like a dub engineer opening the send for one moment. Automatic throws
//   catch an accent followed by space (written into the loop retroactively, so the
//   echo stays on the grid); a held throw opens the send to whatever is playing.
//
// Real-time safe: every buffer is allocated in prepare().
class DubEcho
{
public:
    void prepare (float sampleRate, uint32_t seed);
    void reset();

    void setAmount (float newAmount) noexcept { amountTarget = newAmount; }
    void setHeldThrow (bool held) noexcept { heldThrow = held; }

    // Writes the echo (wet only) into wetL/wetR. bpm comes from the host.
    void process (const float* inL, const float* inR, float* wetL, float* wetR, int numSamples, double bpm) noexcept;

    // For the window: rises to 1 when a throw happens, then fades.
    float getThrowGlow() const noexcept { return throwGlow; }

private:
    // A ring buffer that can also be written into the recent past or the near future.
    class Ring
    {
    public:
        void prepare (int minSize);
        void reset();
        void push (float x) noexcept { buffer[(size_t) (writeIndex & mask)] = x; ++writeIndex; }
        float at (int back) const noexcept { return buffer[(size_t) ((writeIndex - back) & mask)]; } // back 1 = newest
        float read (float delay) const noexcept;                                                      // Hermite
        void addAt (int back, float x) noexcept { buffer[(size_t) ((writeIndex - back) & mask)] += x; }
        float& slot (int64_t index) noexcept { return buffer[(size_t) (index & mask)]; }
        int64_t written() const noexcept { return writeIndex; }

    private:
        std::vector<float> buffer;
        int64_t mask = 0, writeIndex = 0;
    };

    // Slow 1/f-ish wander, smoothed twice so a delay that follows it never jitters.
    class Wander
    {
    public:
        void prepare (float sampleRate, float baseHz);
        void reset() noexcept;
        float next (Rng& rng) noexcept;

    private:
        std::array<float, 3> state {}, rate {}, kick {};
        float smooth = 0.0f, s1 = 0.0f, s2 = 0.0f;
    };

    // An allpass whose delay drifts slowly.
    class DriftAllpass
    {
    public:
        void prepare (float sampleRate, float ms, float gain);
        void reset() { line.reset(); }
        float process (float x, float modSamples) noexcept;

    private:
        Ring line;
        float base = 1.0f, g = 0.4f;
    };

    // One pass over the tape.
    class TapePass
    {
    public:
        void prepare (float sampleRate, int side);
        void reset();
        float process (float x, Rng& rng) noexcept;

    private:
        OnePoleLowpass lp1, lp2, emphasis;
        OnePoleHighpass hp;
        DcBlocker dc;
        std::array<DriftAllpass, 4> diffusers;
        Wander drift;
        float sampleRate = 48000.0f;
    };

    void detectThrow (int64_t now) noexcept;
    void copyThrow() noexcept;

    float sr = 48000.0f;
    uint32_t seedValue = 1;
    Rng rng;

    std::array<Ring, 2> loop, history, pending;
    std::array<TapePass, 2> tape, throwTape;
    std::array<OnePoleLowpass, 2> breathTilt;
    Wander wowA, wowB, flutter, breathWander;

    float amountTarget = 0.5f, amount = 0.5f;
    bool heldThrow = false;
    float heldSend = 0.0f;

    float dottedSm = 0.0f, quarterSm = 0.0f; // the heads glide like tape when the tempo changes
    float eFast = 0.0f, eDuck = 0.0f, rmsShort = 0.0f, rmsLong = 1.0e-4f, peakLong = 1.0e-3f;
    float lean = 0.0f, fillAmount = 0.0f, throwGlow = 0.0f, loopEnvelope = 0.0f;
    int64_t silentFor = 0;

    // automatic throws
    bool inEvent = false;
    int64_t eventStart = 0, eventEnd = 0, quietFor = 0, lastThrow = -(int64_t) 1 << 40, sampleCount = 0;
    float eventPeak = 0.0f;
    // a throw is copied into the loop a little at a time, ahead of the heads
    bool copying = false;
    int64_t copySource = 0, copyShift = 0, copyLength = 0, copyDone = 0;

    // coefficients, set in prepare
    float aFast = 0, rFast = 0, aDuck = 0, rDuck = 0, cShort = 0, cLong = 0, cPeak = 0, cLean = 0, cFillUp = 0,
          cFillDown = 0, cAmount = 0, cHeldUp = 0, cHeldDown = 0, cHead = 0, cGlow = 0, aLoop = 0, rLoop = 0;
};
} // namespace dendrite::dsp
