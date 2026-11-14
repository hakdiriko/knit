// Small DSP building blocks for dendrite. Header-only, plain C++ (no JUCE), so the
// engine can also be rendered offline by the tools. Everything allocates in
// prepare() only and is safe to call on the audio thread afterwards.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace dendrite
{
constexpr float pi = 3.14159265358979f;
constexpr float tau = 2.0f * pi;

inline float clamp01 (float x) noexcept { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
inline float smooth01 (float t) noexcept { t = clamp01 (t); return t * t * (3.0f - 2.0f * t); }
inline float wrapAngle (float x) noexcept { return x - tau * std::floor (x / tau + 0.5f); } // into [-pi, pi)
inline float toDb (float x) noexcept { return 20.0f * std::log10 (std::max (x, 1.0e-7f)); }
inline float fromDb (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

// One-pole coefficient: the fraction of the remaining distance covered per sample.
inline float approach (float seconds, float sampleRate) noexcept
{
    return 1.0f - std::exp (-1.0f / (std::max (seconds, 1.0e-5f) * sampleRate));
}

// Deterministic xorshift64* generator: same seed, same behaviour, so a render matches
// what was heard.
struct Dice
{
    uint64_t state = 0x9E3779B97F4A7C15ull;

    void reseed (uint64_t s) noexcept
    {
        state = (s * 0x2545F4914F6CDD1Dull) ^ 0xD1B54A32D192ED03ull;
        if (state == 0)
            state = 1;
        next();
    }
    uint64_t next() noexcept
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1Dull;
    }
    float unit() noexcept { return (float) (next() >> 40) * (1.0f / 16777216.0f); } // [0, 1)
    float range (float lo, float hi) noexcept { return lo + (hi - lo) * unit(); }
    float sign() noexcept { return unit() < 0.5f ? -1.0f : 1.0f; }
    float jitter (float amount) noexcept { return 1.0f + amount * (2.0f * unit() - 1.0f); }
};

// A breakpoint path over a control: smoothstep between points, so no audible corners.
// Each hidden setting has its own path, and the paths turn at different places, so a
// control passes through regimes instead of scaling one amount.
template <size_t N>
struct Path
{
    std::array<std::pair<float, float>, N> pts;

    float operator() (float x) const noexcept
    {
        if (x <= pts[0].first)
            return pts[0].second;
        for (size_t i = 1; i < N; ++i)
            if (x <= pts[i].first)
            {
                const float t = (x - pts[i - 1].first) / (pts[i].first - pts[i - 1].first);
                return lerp (pts[i - 1].second, pts[i].second, smooth01 (t));
            }
        return pts[N - 1].second;
    }
};

// A power-of-two ring buffer. `at (1)` is the newest sample. Reads between samples
// are cubic Hermite: linear interpolation dulls the highs and makes a moving read grainy.
class Ring
{
public:
    void prepare (int minSize)
    {
        int64_t size = 1;
        while (size < minSize + 8)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writeIndex = 0;
    }
    void clear() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }
    void push (float x) noexcept { buffer[(size_t) (writeIndex & mask)] = x; ++writeIndex; }
    float at (int64_t back) const noexcept { return buffer[(size_t) ((writeIndex - back) & mask)]; }
    float& slot (int64_t absoluteIndex) noexcept { return buffer[(size_t) (absoluteIndex & mask)]; }
    int64_t written() const noexcept { return writeIndex; }
    int capacity() const noexcept { return (int) mask; }

    float read (float delay) const noexcept
    {
        delay = std::clamp (delay, 1.0f, (float) (mask - 4));
        const auto whole = (int64_t) delay;
        const float f = delay - (float) whole;
        const float xm1 = at (whole - 1), x0 = at (whole), x1 = at (whole + 1), x2 = at (whole + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

private:
    std::vector<float> buffer { 0.0f };
    int64_t mask = 0, writeIndex = 0;
};

struct OnePole
{
    float a = 1.0f, z = 0.0f;

    void setCutoff (float hz, float sampleRate) noexcept
    {
        a = 1.0f - std::exp (-tau * std::min (hz, 0.45f * sampleRate) / sampleRate);
    }
    float lowpass (float x) noexcept { z += a * (x - z); return z; }
    float highpass (float x) noexcept { z += a * (x - z); return x - z; }
    void clear() noexcept { z = 0.0f; }
};

struct DcBlocker
{
    float r = 0.995f, x1 = 0.0f, y1 = 0.0f;

    void prepare (float sampleRate) noexcept { r = std::exp (-tau * 12.0f / sampleRate); }
    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void clear() noexcept { x1 = y1 = 0.0f; }
};

// Transposed direct form II biquad (RBJ cookbook designs).
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;

    float process (float x) noexcept
    {
        const float y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
    void clear() noexcept { s1 = s2 = 0.0f; }

    void bandpass (float f, float q, float sr) noexcept // 0 dB at the centre
    {
        f = std::clamp (f, 20.0f, 0.45f * sr);
        const float w = tau * f / sr, c = std::cos (w), al = std::sin (w) / (2.0f * q), n = 1.0f / (1.0f + al);
        b0 = al * n; b1 = 0.0f; b2 = -al * n; a1 = -2.0f * c * n; a2 = (1.0f - al) * n;
    }
    void highpass (float f, float q, float sr) noexcept
    {
        f = std::clamp (f, 10.0f, 0.45f * sr);
        const float w = tau * f / sr, c = std::cos (w), al = std::sin (w) / (2.0f * q), n = 1.0f / (1.0f + al);
        b0 = (1.0f + c) * 0.5f * n; b1 = -(1.0f + c) * n; b2 = b0; a1 = -2.0f * c * n; a2 = (1.0f - al) * n;
    }
    void peak (float f, float q, float gainDb, float sr) noexcept // a bell, gainDb at the centre
    {
        f = std::clamp (f, 10.0f, 0.45f * sr);
        const float A = std::pow (10.0f, gainDb / 40.0f), w = tau * f / sr, c = std::cos (w), al = std::sin (w) / (2.0f * q);
        const float n = 1.0f / (1.0f + al / A);
        b0 = (1.0f + al * A) * n; b1 = -2.0f * c * n; b2 = (1.0f - al * A) * n; a1 = b1; a2 = (1.0f - al / A) * n;
    }
    void lowpass (float f, float q, float sr) noexcept
    {
        f = std::clamp (f, 10.0f, 0.45f * sr);
        const float w = tau * f / sr, c = std::cos (w), al = std::sin (w) / (2.0f * q), n = 1.0f / (1.0f + al);
        b0 = (1.0f - c) * 0.5f * n; b1 = (1.0f - c) * n; b2 = b0; a1 = -2.0f * c * n; a2 = (1.0f - al) * n;
    }
};

// Organic wander: three layers of seeded random targets at golden-ratio periods, each
// eased with a half cosine, then smoothed by two poles. Never a raw random step, so it
// can move a delay read, a gain or a phase without grain or hiss (the 0.5 sketches
// taught us that raw steps on a read position come out as noise on every repeat).
class Wander
{
public:
    void prepare (float sampleRate, float slowestSeconds, uint64_t seed)
    {
        dice.reseed (seed);
        float period = slowestSeconds, weight = 0.62f;
        for (auto& l : layers)
        {
            l.inc = 1.0f / (period * sampleRate);
            l.from = dice.range (-1.0f, 1.0f);
            l.to = dice.range (-1.0f, 1.0f);
            l.phase = dice.unit();
            l.weight = weight;
            period /= 1.618034f;
            weight *= 0.62f;
        }
        smoothing = approach (0.02f * slowestSeconds + 0.004f, sampleRate);
        s1 = s2 = 0.0f;
        seedValue = seed;
    }
    void reset() noexcept { restart(); }
    float next() noexcept
    {
        float sum = 0.0f;
        for (auto& l : layers)
        {
            l.phase += l.inc;
            if (l.phase >= 1.0f)
            {
                l.phase -= 1.0f;
                l.from = l.to;
                l.to = dice.range (-1.0f, 1.0f);
            }
            sum += l.weight * lerp (l.from, l.to, 0.5f - 0.5f * std::cos (pi * l.phase));
        }
        s1 += smoothing * (1.15f * sum - s1);
        s2 += smoothing * (s1 - s2);
        return s2;
    }

private:
    void restart() noexcept
    {
        dice.reseed (seedValue);
        for (auto& l : layers)
        {
            l.from = dice.range (-1.0f, 1.0f);
            l.to = dice.range (-1.0f, 1.0f);
            l.phase = dice.unit();
        }
        s1 = s2 = 0.0f;
    }

    struct Layer { float phase = 0, inc = 0, from = 0, to = 0, weight = 0; };
    std::array<Layer, 3> layers {};
    Dice dice;
    float smoothing = 0.001f, s1 = 0.0f, s2 = 0.0f;
    uint64_t seedValue = 1;
};

// A 90-degree phase pair from two chains of allpasses (Olli Niemitalo's design).
// `re` and `im` are 90 degrees apart at every audible frequency, with equal magnitude.
// Both are dispersed against the input, which is fine here: the pair is only used on
// the echo's Side, where a little dispersion is part of the spread.
class PhasePair
{
public:
    void clear() noexcept
    {
        for (auto* c : { &a, &b })
            for (auto& s : *c)
                s = {};
        delayed = 0.0f;
    }
    void process (float x, float& re, float& im) noexcept
    {
        static constexpr float ka[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        static constexpr float kb[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        float ya = x, yb = x;
        for (int k = 0; k < 4; ++k)
        {
            ya = a[(size_t) k].run (ya, ka[k] * ka[k]);
            yb = b[(size_t) k].run (yb, kb[k] * kb[k]);
        }
        re = delayed; // chain A runs one sample late
        delayed = ya;
        im = yb;
    }

private:
    struct Section
    {
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float run (float x, float c) noexcept
        {
            const float y = c * (x + y2) - x2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };
    std::array<Section, 4> a {}, b {};
    float delayed = 0.0f;
};

// Schroeder allpass with a slowly drifting delay: flat in magnitude, it smears in time.
class DriftAllpass
{
public:
    void prepare (float sampleRate, float ms)
    {
        line.prepare ((int) (sampleRate * (ms + 4.0f) * 0.001f) + 8);
        base = ms * 0.001f * sampleRate;
    }
    void clear() noexcept { line.clear(); }
    float process (float x, float g, float modSamples) noexcept
    {
        const float delayed = line.read (std::max (1.5f, base + modSamples));
        const float v = x + g * delayed;
        line.push (v);
        return delayed - g * v;
    }

private:
    Ring line;
    float base = 1.0f;
};

// Soft clip with unity small-signal gain.
inline float softClip (float x, float drive) noexcept { return std::tanh (x * drive) / drive; }
} // namespace dendrite
