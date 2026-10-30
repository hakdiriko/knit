#pragma once

#include "Primitives.h"

namespace dendrite::dsp
{
// Safety limiter: a seatbelt, not an effect. Instant attack guarantees no sample
// leaves above the ceiling; a smooth release hides it. Linked across channels so
// the stereo image never shifts. In normal use it should never engage.
class SafetyLimiter
{
public:
    void prepare (float sampleRate) noexcept
    {
        releaseCoef = 1.0f - std::exp (-1.0f / (0.12f * sampleRate));
        reset();
    }

    void reset() noexcept { gain = 1.0f; }

    void process (float& l, float& r) noexcept
    {
        const float peak = std::max (std::abs (l), std::abs (r));
        const float target = peak > ceiling ? ceiling / peak : 1.0f;
        gain = target < gain ? target : gain + releaseCoef * (target - gain);
        l *= gain;
        r *= gain;
    }

private:
    static constexpr float ceiling = 0.9660509f; // -0.3 dBFS
    float releaseCoef = 0.0f, gain = 1.0f;
};
} // namespace dendrite::dsp
