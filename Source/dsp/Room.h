#pragma once

#include "Primitives.h"

namespace dendrite::dsp
{
// The room. Metal rings because its modes are few, sharp and evenly spaced; air sounds
// like air because its modes are so many and so blurred that no single one is heard.
// Everything here pushes towards air:
// - 32 delay lines with irregular lengths, mixed through a Hadamard matrix, with an
//   allpass inside every loop, so echo density explodes within a few passes;
// - every line drifts slowly on its own 1/f path, so no mode sits still long enough
//   to ring;
// - highs die several times faster than lows, the way air and soft walls absorb them.
// Measured on a noise burst, the tail is as dense as Gaussian noise at every setting
// (0.3.0's network reached about a third of that).
class Room
{
public:
    static constexpr int numLines = 32;

    void prepare (float sampleRate, uint32_t seed)
    {
        sr = sampleRate;
        Rng r;
        r.seed (seed * 7477u + 3u);
        for (size_t i = 0; i < numLines; ++i)
        {
            // exponential spread from 19 to 101 ms with jitter, so no two ratios are simple
            const float t = ((float) i + 0.35f * r.nextBipolar()) / (float) (numLines - 1);
            baseMs[i] = 19.0f * std::pow (101.0f / 19.0f, std::clamp (t, 0.0f, 1.0f));
            loopApMs[i] = 1.3f + 6.5f * r.next01();
            lines[i].prepare ((int) (sr * (0.001f * baseMs[i] * maxSize + 0.004f)));
            loopAp[i].prepare (std::max (1, (int) (sr * 0.001f * loopApMs[i])));
            drift[i].prepare (sr / (float) controlInterval, 0.6f + 0.5f * r.next01(), seed * 131u + (uint32_t) i * 977u);
            outSign[i] = r.next01() < 0.5f ? -1.0f : 1.0f;
        }

        constexpr float diffuserMs[2][4] = { { 1.7f, 2.9f, 4.3f, 6.1f }, { 1.9f, 3.3f, 4.7f, 5.6f } };
        for (size_t c = 0; c < 2; ++c)
        {
            for (size_t k = 0; k < 4; ++k)
                diffusers[c][k].prepare (std::max (1, (int) (sr * 0.001f * diffuserMs[c][k])));
            preDelay[c].prepare ((int) (sr * 0.05f));
            dcBlock[c].prepare (sr);
        }

        set (0.3f);
        reset();
    }

    void reset()
    {
        for (auto& l : lines) l.reset();
        for (auto& a : loopAp) a.reset();
        for (auto& side : diffusers)
            for (auto& a : side) a.reset();
        for (auto& p : preDelay) p.reset();
        for (auto& d : dcBlock) d.reset();
        for (auto& d : drift) d.reset();
        damp.fill (0.0f);
        driftSamples.fill (0.0f);
        tick = 0;
    }

    // Regimes: a small wooden room, a hall, then a long dark afterlife.
    void set (float x) noexcept
    {
        const float t60 = evalPath (std::array<Breakpoint, 5> { { { 0.0f, 0.35f }, { 0.25f, 0.7f }, { 0.5f, 2.0f }, { 0.8f, 6.0f }, { 1.0f, 30.0f } } }, x);
        const float size = evalPath (std::array<Breakpoint, 4> { { { 0.0f, 0.35f }, { 0.3f, 0.55f }, { 0.7f, 1.1f }, { 1.0f, maxSize } } }, x);
        // above dampHz, each pass loses this power of the loop gain: highs fade hfRatio x faster
        const float hfRatio = evalPath (std::array<Breakpoint, 3> { { { 0.0f, 3.0f }, { 0.6f, 4.0f }, { 1.0f, 6.0f } } }, x);
        const float dampHz = evalPath (std::array<Breakpoint, 3> { { { 0.0f, 6500.0f }, { 0.6f, 4500.0f }, { 1.0f, 3000.0f } } }, x);
        preDelaySamples = std::max (2.0f, sr * 0.001f * evalPath (std::array<Breakpoint, 3> { { { 0.0f, 3.0f }, { 0.5f, 15.0f }, { 1.0f, 40.0f } } }, x));
        driftMs = evalPath (std::array<Breakpoint, 3> { { { 0.0f, 0.25f }, { 0.6f, 0.6f }, { 1.0f, 1.1f } } }, x);

        for (size_t i = 0; i < numLines; ++i)
        {
            length[i] = sr * 0.001f * baseMs[i] * size;
            const float loopSamples = length[i] + sr * 0.001f * loopApMs[i];
            gain[i] = std::pow (10.0f, -3.0f * loopSamples / (t60 * sr));
            hfKeep[i] = std::clamp (std::pow (gain[i], hfRatio - 1.0f), 0.05f, 1.0f);
        }
        damping.setCutoff (dampHz, sr);
    }

    // How loud the room comes back, measured so it sits about -24 dB under the sound at
    // 10%, -9 dB at 50% and -3 dB at 90%. At the very top the sound itself steps back.
    static float returnGain (float x) noexcept
    {
        return evalPath (std::array<Breakpoint, 8> { { { 0.0f, 0.0f }, { 0.05f, 0.3f }, { 0.1f, 0.55f }, { 0.3f, 1.04f }, { 0.5f, 1.8f }, { 0.7f, 1.7f }, { 0.9f, 1.33f }, { 1.0f, 1.4f } } }, x);
    }

    static float directGain (float x) noexcept
    {
        return evalPath (std::array<Breakpoint, 3> { { { 0.0f, 1.0f }, { 0.7f, 1.0f }, { 1.0f, 0.75f } } }, x);
    }

    // What the room hears: the tape, and what wore off it.
    static constexpr float tapeSend = 0.55f, lostSend = 0.9f;

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        if (tick++ % controlInterval == 0)
            for (size_t i = 0; i < numLines; ++i)
                driftSamples[i] = sr * 0.001f * driftMs * drift[i].tick();

        // pre-delay, then diffusion: transients are smeared before they reach the loop
        float in[2] = { inL, inR };
        for (size_t c = 0; c < 2; ++c)
        {
            preDelay[c].push (in[c]);
            float v = preDelay[c].read (preDelaySamples * (c == 0 ? 1.0f : 1.09f));
            for (auto& a : diffusers[c])
                v = a.process (v, 0.75f);
            in[c] = v;
        }

        std::array<float, numLines> y {};
        const float a = damping.coefficient();
        for (size_t i = 0; i < numLines; ++i)
        {
            float v = lines[i].read (length[i] + driftSamples[i]);
            v = loopAp[i].process (v, 0.65f);
            // shelf: lows go round at the loop gain, highs at gain * hfKeep
            damp[i] += a * (v - damp[i]);
            v = (damp[i] + hfKeep[i] * (v - damp[i])) * gain[i];
            y[i] = v;
        }

        float l = 0.0f, r = 0.0f;
        for (size_t i = 0; i < numLines; i += 2)
        {
            l += outSign[i] * y[i];
            r += outSign[i + 1] * y[i + 1];
        }
        outL = dcBlock[0].process (0.25f * l);
        outR = dcBlock[1].process (0.25f * r);

        hadamard (y);
        for (size_t i = 0; i < numLines; ++i)
        {
            const float v = y[i] + in[i & 1] * (((i >> 1) & 1) != 0 ? -0.25f : 0.25f);
            lines[i].push (2.0f * fastTanh (0.5f * v)); // bounded, transparent at normal levels
        }
    }

private:
    // Orthogonal: mixes every line into every other without adding or losing energy.
    static void hadamard (std::array<float, numLines>& v) noexcept
    {
        for (size_t h = 1; h < numLines; h <<= 1)
            for (size_t i = 0; i < numLines; i += h << 1)
                for (size_t j = i; j < i + h; ++j)
                {
                    const float p = v[j], q = v[j + h];
                    v[j] = p + q;
                    v[j + h] = p - q;
                }
        for (auto& x : v)
            x *= 0.17677670f; // 1/sqrt(32)
    }

    // Shared one-pole coefficient for every line's damping shelf.
    struct Damping
    {
        void setCutoff (float hz, float sampleRate) noexcept { a = 1.0f - std::exp (-2.0f * pi * hz / sampleRate); }
        float coefficient() const noexcept { return a; }
        float a = 0.5f;
    };

    static constexpr float maxSize = 1.5f;
    static constexpr int controlInterval = 32;

    float sr = 48000.0f;
    std::array<DelayLine, numLines> lines;
    std::array<Allpass, numLines> loopAp;
    std::array<PinkWalk, numLines> drift;
    std::array<float, numLines> baseMs {}, loopApMs {}, length {}, gain {}, hfKeep {}, damp {}, driftSamples {}, outSign {};
    std::array<std::array<Allpass, 4>, 2> diffusers;
    std::array<DelayLine, 2> preDelay;
    std::array<DcBlocker, 2> dcBlock;
    Damping damping;
    float preDelaySamples = 100.0f, driftMs = 0.5f;
    uint32_t tick = 0;
};
} // namespace dendrite::dsp
