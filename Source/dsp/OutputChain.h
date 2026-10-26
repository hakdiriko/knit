#pragma once

#include "Primitives.h"

namespace dendrite::dsp
{
// Wet auto-gain: slow RMS envelopes on the input and on the wet signal pull the wet
// level toward the input level (within +-12 dB). Without it, louder settings simply
// sound "better", and honest exploration is impossible.
class AutoGain
{
public:
    void prepare (float sampleRate) noexcept
    {
        attackCoef = 1.0f - std::exp (-1.0f / (0.08f * sampleRate));
        releaseCoef = 1.0f - std::exp (-1.0f / (0.8f * sampleRate));
        gainCoef = 1.0f - std::exp (-1.0f / (0.15f * sampleRate));
        reset();
    }

    void reset() noexcept
    {
        inPower = wetPower = 0.0f;
        gain = 1.0f;
        targetGain = 1.0f;
        counter = 0;
        hold = false;
    }

    // Power is averaged over both channels (not mono-summed), so a wide, decorrelated
    // wet signal isn't mistaken for a quiet one. wetL/wetR are scaled in place.
    void process (const float* dryL, const float* dryR, float* wetL, float* wetR, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float d = 0.5f * (dryL[i] * dryL[i] + dryR[i] * dryR[i]);
            const float w = 0.5f * (wetL[i] * wetL[i] + wetR[i] * wetR[i]);
            // Same fast-rise / slow-fall shape on both sides: a wet that suddenly gets
            // louder than the input is pulled back within ~100 ms, while steady
            // material sees an unbiased ratio.
            if (! hold)
            {
                inPower += (d > inPower ? attackCoef : releaseCoef) * (d - inPower);
                wetPower += (w > wetPower ? attackCoef : releaseCoef) * (w - wetPower);
            }

            if (++counter >= 32 && ! hold)
            {
                counter = 0;
                // Hold the gain while the input is (nearly) silent, so tails and
                // drones keep the level they had instead of being pumped up.
                if (inPower > 1.0e-7f && wetPower > 1.0e-10f)
                    targetGain = std::clamp (std::sqrt (inPower / wetPower), minGain, maxGain);
            }

            gain += gainCoef * (targetGain - gain);
            wetL[i] *= gain;
            wetR[i] *= gain;
        }
    }

    // While a scratch plays, everything freezes: the gesture is heard at its real
    // level, and afterwards the gain carries on from where it was, with no dip.
    void setHold (bool shouldHold) noexcept { hold = shouldHold; }

private:
    static constexpr float minGain = 0.2511886f; // -12 dB
    static constexpr float maxGain = 3.9810717f; // +12 dB
    float attackCoef = 0.0f, releaseCoef = 0.0f, gainCoef = 0.0f;
    float inPower = 0.0f, wetPower = 0.0f, gain = 1.0f, targetGain = 1.0f;
    int counter = 0;
    bool hold = false;
};

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
