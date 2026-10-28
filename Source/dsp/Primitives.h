#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Small real-time-safe DSP building blocks. Everything that allocates does so in
// prepare(), never on the audio thread.
namespace dendrite::dsp
{
constexpr float pi = 3.14159265358979f;

//==============================================================================
// Circular delay line with 4-point Hermite interpolation. Linear interpolation
// dulls the highs and makes modulated delays grainy; Hermite keeps them clean.
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 8)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        maxDelay = (float) (size - 4);
        writeIndex = 0;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    // Delay is measured from the most recently pushed sample (delay 1 = last sample).
    float read (float delay) const noexcept
    {
        delay = delay < 2.0f ? 2.0f : (delay > maxDelay ? maxDelay : delay);
        const auto whole = (int) delay;
        const float t = 1.0f - (delay - (float) whole);
        const int i = writeIndex - whole - 1;

        const float xm1 = buffer[(size_t) ((i - 1) & mask)];
        const float x0  = buffer[(size_t) (i & mask)];
        const float x1  = buffer[(size_t) ((i + 1) & mask)];
        const float x2  = buffer[(size_t) ((i + 2) & mask)];

        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    void push (float x) noexcept
    {
        buffer[(size_t) writeIndex] = x;
        writeIndex = (writeIndex + 1) & mask;
    }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writeIndex = 0;
    float maxDelay = 2.0f;
};

//==============================================================================
// Topology-preserving-transform state-variable filter (Zavalishin) with all three
// outputs. Stays stable and smooth when its cutoff moves every few samples.
class Svf
{
public:
    void setCoefficients (float cutoffHz, float q, float sampleRate) noexcept
    {
        const float g = std::tan (pi * std::min (cutoffHz, 0.45f * sampleRate) / sampleRate);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void process (float x, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }

    float damping() const noexcept { return k; }
    void reset() noexcept { ic1 = ic2 = 0.0f; }

private:
    float k = 1.41421356f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

// Linkwitz-Riley 4th-order crossover: two Butterworth sections on each side. The two
// halves sum to an allpass, so splitting and summing back colours nothing.
class Crossover
{
public:
    void setFrequency (float hz, float sampleRate) noexcept
    {
        for (auto* f : { &lo1, &lo2, &hi1, &hi2 })
            f->setCoefficients (hz, 0.70710678f, sampleRate);
    }

    void split (float x, float& low, float& high) noexcept
    {
        float lp, bp, hp, unused;
        lo1.process (x, lp, bp, hp);
        lo2.process (lp, low, bp, unused);
        hi1.process (x, lp, bp, hp);
        hi2.process (hp, unused, bp, high);
    }

    void reset() noexcept
    {
        for (auto* f : { &lo1, &lo2, &hi1, &hi2 })
            f->reset();
    }

private:
    Svf lo1, lo2, hi1, hi2;
};

// The allpass a Crossover adds. A band that skips a split passes through one, so all
// bands stay in phase when they are summed.
class CrossoverAllpass
{
public:
    void setFrequency (float hz, float sampleRate) noexcept { svf.setCoefficients (hz, 0.70710678f, sampleRate); }

    float process (float x) noexcept
    {
        float lp, bp, hp;
        svf.process (x, lp, bp, hp);
        return x - 2.0f * svf.damping() * bp;
    }

    void reset() noexcept { svf.reset(); }

private:
    Svf svf;
};

// One-pole lowpass (6 dB/oct).
class OnePoleLowpass
{
public:
    void setCutoff (float cutoffHz, float sampleRate) noexcept
    {
        a = 1.0f - std::exp (-2.0f * pi * cutoffHz / sampleRate);
    }

    float process (float x) noexcept { return s += a * (x - s); }
    void reset() noexcept { s = 0.0f; }

private:
    float a = 1.0f, s = 0.0f;
};

// One-pole highpass (6 dB/oct): keeps low-end mud from building up in a loop.
class OnePoleHighpass
{
public:
    void setCutoff (float cutoffHz, float sampleRate) noexcept
    {
        a = 1.0f - std::exp (-2.0f * pi * cutoffHz / sampleRate);
    }

    float process (float x) noexcept
    {
        s += a * (x - s);
        return x - s;
    }

    void reset() noexcept { s = 0.0f; }

private:
    float a = 0.0f, s = 0.0f;
};

// DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1]. Mandatory inside any saturated
// feedback loop, where asymmetric clipping slowly builds up an offset.
class DcBlocker
{
public:
    void prepare (float sampleRate) noexcept { r = std::exp (-2.0f * pi * 8.0f / sampleRate); }

    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    void reset() noexcept { x1 = y1 = 0.0f; }

private:
    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
};

// Schroeder allpass: smears transients in time without colouring the long-term
// spectrum. Unity gain at every frequency, so it never destabilises a loop.
class Allpass
{
public:
    void prepare (int delaySamples)
    {
        buffer.assign ((size_t) std::max (1, delaySamples), 0.0f);
        index = 0;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        index = 0;
    }

    float process (float x, float coefficient) noexcept
    {
        const float delayed = buffer[(size_t) index];
        const float v = x - coefficient * delayed;
        buffer[(size_t) index] = v;
        index = (index + 1 == (int) buffer.size()) ? 0 : index + 1;
        return coefficient * v + delayed;
    }

private:
    std::vector<float> buffer;
    int index = 0;
};

//==============================================================================
// Deterministic RNG (xorshift32). Seeded explicitly so a render is repeatable.
class Rng
{
public:
    void seed (uint32_t s) noexcept { state = s != 0 ? s : 0x9e3779b9u; }

    float next01() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (state >> 8) * (1.0f / 16777216.0f);
    }

    float nextBipolar() noexcept { return 2.0f * next01() - 1.0f; }

private:
    uint32_t state = 0x9e3779b9u;
};

// 1/f ("pink") control signal, Voss-McCartney style: eight held random sources, the
// k-th redrawn every 2^k steps, summed. Each octave of modulation rate carries about
// the same energy, so there are slow swells with smaller swells inside them, the
// fluctuation spectrum Voss and Clarke measured in music and speech. A random walk
// (brown) only wanders; white noise only jitters.
class PinkWalk
{
public:
    void prepare (float updateRateHz, float fastestHz, uint32_t seed)
    {
        stepTicks = std::max (1, (int) (updateRateHz / fastestHz));
        coefficient = 1.0f - std::exp (-2.0f * pi * fastestHz / updateRateHz);
        seedValue = seed;
        reset();
    }

    void reset() noexcept
    {
        rng.seed (seedValue);
        for (auto& v : sources)
            v = rng.nextBipolar();
        ticks = 0;
        counter = 0;
        value = sum();
    }

    // Call once per control tick; returns a value in roughly [-1, 1].
    float tick() noexcept
    {
        if (++ticks >= stepTicks)
        {
            ticks = 0;
            ++counter;
            // Source k changes when bit k is the lowest set bit of the counter: source 0
            // every step, source 1 every 2nd, source 2 every 4th, and so on.
            int k = 0;
            while (k < (int) sources.size() - 1 && ((counter >> k) & 1u) == 0)
                ++k;
            sources[(size_t) k] = rng.nextBipolar();
        }

        value += coefficient * (sum() - value); // removes the steps, keeps the shape
        return value;
    }

private:
    float sum() const noexcept
    {
        float s = 0.0f;
        for (auto v : sources)
            s += v;
        return std::clamp (s * 0.42f, -1.5f, 1.5f);
    }

    Rng rng;
    std::array<float, 8> sources {};
    uint32_t seedValue = 1, counter = 0;
    int stepTicks = 1, ticks = 0;
    float coefficient = 0.1f, value = 0.0f;
};

//==============================================================================
// Macro paths: one knob drives many hidden parameters, each along its own curve.
// Smoothstep between breakpoints, so there are no corners and no audible kinks.
struct Breakpoint
{
    float x, y;
};

template <size_t N>
float evalPath (const std::array<Breakpoint, N>& p, float x) noexcept
{
    if (x <= p[0].x)
        return p[0].y;

    for (size_t i = 1; i < N; ++i)
        if (x <= p[i].x)
        {
            float t = (x - p[i - 1].x) / (p[i].x - p[i - 1].x);
            t = t * t * (3.0f - 2.0f * t);
            return p[i - 1].y + t * (p[i].y - p[i - 1].y);
        }

    return p[N - 1].y;
}

// Soft clipper shaped like tanh (within 2%), reaching exactly 1 with zero slope at 3: cheap
// enough to run on every line of the room every sample.
inline float fastTanh (float x) noexcept
{
    x = std::clamp (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float smooth01 (float t) noexcept
{
    t = std::clamp (t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Perceptual (exponential) mapping for frequencies and times.
inline float expMap (float lo, float hi, float x) noexcept
{
    return lo * std::pow (hi / lo, x);
}
} // namespace dendrite::dsp
