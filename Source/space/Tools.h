// Small DSP building blocks for the hypnagogic engine. Header-only, allocation-free
// after prepare(), safe to call on the audio thread.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hyp
{
constexpr float pi = 3.14159265358979f;
constexpr float tau = 2.0f * pi;
constexpr double piD = 3.14159265358979323846;

inline float clamp01 (float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
inline float ease (float t) { t = clamp01 (t); return t * t * (3.0f - 2.0f * t); }
inline float wrapUnit (float x) { return x - std::floor (x); }                       // into [0, 1)
inline float wrapHalf (float x) { return x - std::floor (x + 0.5f); }                // into [-0.5, 0.5)
inline float wrapAngle (float x) { return x - tau * std::floor (x / tau + 0.5f); }   // into [-pi, pi)

// Turns an analytic pair (re, im) by `angle`: the result is the band delayed in phase
// by that angle at every frequency at once.
inline float turn (float re, float im, float c, float s) { return re * c + im * s; }

// Deterministic xorshift64* generator: same seed, same itch.
struct Dice
{
    uint64_t state = 0x9E3779B97F4A7C15ull;

    void reseed (uint64_t s) { state = (s * 0x2545F4914F6CDD1Dull) ^ 0xD1B54A32D192ED03ull; if (state == 0) state = 1; next(); }
    uint64_t next() { state ^= state >> 12; state ^= state << 25; state ^= state >> 27; return state * 0x2545F4914F6CDD1Dull; }
    float unit() { return (float) (next() >> 40) * (1.0f / 16777216.0f); }      // [0, 1)
    float range (float lo, float hi) { return lo + (hi - lo) * unit(); }
    float sign() { return unit() < 0.5f ? -1.0f : 1.0f; }
    float jitter (float amount) { return 1.0f + amount * (2.0f * unit() - 1.0f); }
};

// A breakpoint path over a knob: smoothstep between points, so no audible corners.
template <size_t N>
struct Path
{
    std::array<std::pair<float, float>, N> pts;

    float operator() (float x) const
    {
        if (x <= pts[0].first) return pts[0].second;
        for (size_t i = 1; i < N; ++i)
            if (x <= pts[i].first)
            {
                const float t = (x - pts[i - 1].first) / (pts[i].first - pts[i - 1].first);
                return lerp (pts[i - 1].second, pts[i].second, ease (t));
            }
        return pts[N - 1].second;
    }
};

// Smooth value noise around the circle of orbit: the "terrain" that makes a few degrees
// of rotation a different place. Periodic, so a full turn lands where it started.
struct Terrain
{
    std::array<float, 16> lattice {};
    int cells = 8;

    void build (uint64_t seed, int numCells)
    {
        cells = std::clamp (numCells, 2, (int) lattice.size());
        Dice d; d.reseed (seed);
        for (auto& v : lattice) v = d.range (-1.0f, 1.0f);
    }

    float at (float x) const
    {
        const float p = wrapUnit (x) * (float) cells;
        const int i = std::min ((int) p, cells - 1);
        const float f = p - (float) i;
        const float w = 0.5f - 0.5f * std::cos (pi * f);
        return lerp (lattice[(size_t) i], lattice[(size_t) ((i + 1) % cells)], w);
    }
};

// One-pole smoother / envelope.
struct Glide
{
    float coef = 0.0f, value = 0.0f;

    void setTime (float seconds, double sr) { coef = std::exp (-1.0f / (float) (seconds * sr)); }
    float step (float target) { value = target + coef * (value - target); return value; }
};

// Transposed direct form II biquad with RBJ cookbook designs.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;

    float process (float x)
    {
        const float y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
    void clear() { s1 = s2 = 0.0f; }
    void copyCoefficients (const Biquad& o) { b0 = o.b0; b1 = o.b1; b2 = o.b2; a1 = o.a1; a2 = o.a2; }

    void highpass (float f, float q, double sr)
    {
        const float w = tau * f / (float) sr, c = std::cos (w), al = std::sin (w) / (2.0f * q), n = 1.0f / (1.0f + al);
        b0 = (1.0f + c) * 0.5f * n; b1 = -(1.0f + c) * n; b2 = b0; a1 = -2.0f * c * n; a2 = (1.0f - al) * n;
    }
    void bandpass (float f, float q, double sr)          // 0 dB at the centre
    {
        const float w = tau * f / (float) sr, c = std::cos (w), al = std::sin (w) / (2.0f * q), n = 1.0f / (1.0f + al);
        b0 = al * n; b1 = 0.0f; b2 = -al * n; a1 = -2.0f * c * n; a2 = (1.0f - al) * n;
    }
    void highShelf (float f, float dB, double sr)
    {
        const float A = std::pow (10.0f, dB / 40.0f), w = tau * f / (float) sr, c = std::cos (w);
        const float al = std::sin (w) / 2.0f * std::sqrt (2.0f), sA = 2.0f * std::sqrt (A) * al;
        const float n = 1.0f / ((A + 1) - (A - 1) * c + sA);
        b0 = A * ((A + 1) + (A - 1) * c + sA) * n;
        b1 = -2 * A * ((A - 1) + (A + 1) * c) * n;
        b2 = A * ((A + 1) + (A - 1) * c - sA) * n;
        a1 = 2 * ((A - 1) - (A + 1) * c) * n;
        a2 = ((A + 1) - (A - 1) * c - sA) * n;
    }
};

// A linear-phase analytic band-pass pair. For any input, `re` is its band (about
// 150 Hz to 18 kHz) exactly in phase with the input delayed by `latency` samples,
// and `im` is the same band turned 90 degrees, with the same magnitude.
// Because the pair is exact, turning it moves the whole band as one, and the dry
// signal (delayed by the same latency) is a perfect reference: nothing smears.
struct Analytic
{
    std::vector<float> kRe, kIm, hist;      // kernels stored time-reversed; history doubled
    int taps = 1, pos = 0, latency = 0;

    void prepare (double sr, float halfMs, float lowHz, float highHz, float beta)
    {
        latency = std::max (8, (int) std::lround (halfMs * 0.001 * sr));
        taps = 2 * latency + 1;
        kRe.assign ((size_t) taps, 0.0f); kIm.assign ((size_t) taps, 0.0f);
        hist.assign ((size_t) (2 * taps), 0.0f);
        pos = 0;

        const double w1 = 2.0 * piD * lowHz / sr, w2 = 2.0 * piD * std::min ((double) highHz, 0.45 * sr) / sr;
        const double i0b = bessel0 (beta);
        for (int k = 0; k < taps; ++k)
        {
            const int n = k - latency;                            // coefficient for x[t - k]
            const double ratio = (double) n / (double) latency;
            const double win = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - ratio * ratio))) / i0b;
            const double re = n == 0 ? (w2 - w1) / piD : (std::sin (w2 * n) - std::sin (w1 * n)) / (piD * n);
            const double im = n == 0 ? 0.0 : (std::cos (w1 * n) - std::cos (w2 * n)) / (piD * n);
            kRe[(size_t) (taps - 1 - k)] = (float) (re * win);    // reversed: oldest sample first
            kIm[(size_t) (taps - 1 - k)] = (float) (im * win);
        }
    }

    void clear() { std::fill (hist.begin(), hist.end(), 0.0f); pos = 0; }

    void push (float x, float& re, float& im)
    {
        hist[(size_t) pos] = x;
        hist[(size_t) (pos + taps)] = x;
        pos = pos + 1 == taps ? 0 : pos + 1;
        const float* window = hist.data() + pos;                  // oldest ... newest, contiguous
        std::array<float, 8> ar {}, ai {};
        int k = 0;
        for (; k + 8 <= taps; k += 8)
            for (int j = 0; j < 8; ++j)
            {
                ar[(size_t) j] += kRe[(size_t) (k + j)] * window[k + j];
                ai[(size_t) j] += kIm[(size_t) (k + j)] * window[k + j];
            }
        float sumRe = 0.0f, sumIm = 0.0f;
        for (; k < taps; ++k) { sumRe += kRe[(size_t) k] * window[k]; sumIm += kIm[(size_t) k] * window[k]; }
        for (int j = 0; j < 8; ++j) { sumRe += ar[(size_t) j]; sumIm += ai[(size_t) j]; }
        re = sumRe; im = sumIm;
    }

    static double bessel0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 30; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
        return sum;
    }
};

// Circular buffer with Hermite-interpolated reads.
struct Line
{
    std::vector<float> buf;
    int mask = 0, w = 0;

    void prepare (int minSize) { int n = 1; while (n < minSize + 4) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float tap (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }   // 0 = newest

    float read (float d) const      // d samples behind the newest sample, d >= 1
    {
        const int i = (int) d; const float f = d - (float) i;
        const float y0 = tap (i - 1), y1 = tap (i), y2 = tap (i + 1), y3 = tap (i + 2);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
};

// Organic wander: three layers of seeded random targets at golden-ratio periods,
// each eased with a half cosine and then smoothed. Never a raw random step,
// so it can move a delay, a pole or a phase without grain or noise.
struct Wander
{
    struct Layer { float phase = 0, inc = 0, from = 0, to = 0, weight = 0; };
    std::array<Layer, 3> layers;
    Dice dice;
    Glide smooth;

    void prepare (double sr, float slowestSeconds, uint64_t seed, float smoothSeconds = 0.08f)
    {
        dice.reseed (seed);
        float period = slowestSeconds, weight = 0.62f;
        for (auto& l : layers)
        {
            l.inc = 1.0f / (period * (float) sr);
            l.from = dice.range (-1, 1); l.to = dice.range (-1, 1);
            l.phase = dice.unit(); l.weight = weight;
            period /= 1.618034f; weight *= 0.62f;
        }
        smooth.setTime (smoothSeconds, sr); smooth.value = 0;
    }

    float next()
    {
        float sum = 0;
        for (auto& l : layers)
        {
            l.phase += l.inc;
            if (l.phase >= 1.0f) { l.phase -= 1.0f; l.from = l.to; l.to = dice.range (-1, 1); }
            sum += l.weight * lerp (l.from, l.to, 0.5f - 0.5f * std::cos (pi * l.phase));
        }
        return smooth.step (sum * 1.1f);
    }
};
} // namespace hyp
