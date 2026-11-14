#include "Hand.h"
#include "Creature.h"

namespace dendrite
{
namespace
{
uint64_t mix (uint64_t& s) noexcept // splitmix64: integers only, the same on every machine
{
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
float unit (uint64_t& s) noexcept { return (float) (mix (s) >> 40) * (1.0f / 16777216.0f); }
float range (uint64_t& s, float lo, float hi) noexcept { return lo + (hi - lo) * unit (s); }
} // namespace

void Hand::prepare (float sampleRate) noexcept
{
    sr = sampleRate;
    cSmooth = approach (0.025f, sr);
    speedRate = 1.2f / sr; // the tape's speed changes by at most 1.2x per second: a dive, not a zipper
    if (! grown)
        grow (1);
    rest();
}

void Hand::grow (uint64_t genome) noexcept
{
    uint64_t s = genome ^ 0x68616E642E322E31ull; // "hand.2.1"
    // four paces from a dub-friendly set, each a little off the grid (the hand is not a sequencer)
    static constexpr float paces[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    for (int j = 0; j < oscillators; ++j)
        pace[j] = paces[mix (s) % 6] * range (s, 0.97f, 1.03f);
    for (int j = 0; j < oscillators; ++j)
        for (int k = 0; k < oscillators; ++k)
            pull[j][k] = j == k ? 0.0f : range (s, -0.5f, 0.5f);
    for (int c = 0; c < channels; ++c)
    {
        const int lead = (int) (mix (s) % oscillators);
        for (int j = 0; j < oscillators; ++j)
        {
            weight[c][j] = j == lead ? 1.0f : range (s, -0.45f, 0.45f);
            offset[c][j] = range (s, 0.0f, tau);
        }
        snap[c] = range (s, 1.0f, 4.0f);
    }
    // which controls this hand reaches for: three or four strongly, the rest lightly
    float raw[channels];
    for (int c = 0; c < channels; ++c)
        raw[c] = unit (s);
    raw[cFreeze] *= unit (s) < 0.5f ? 0.0f : 1.0f; // half the creatures never freeze
    for (int c = 0; c < channels; ++c)
    {
        int above = 0;
        for (int d = 0; d < channels; ++d)
            above += raw[d] > raw[c] ? 1 : 0;
        use[c] = above < 4 ? 0.65f + 0.35f * raw[c] : 0.25f * raw[c];
    }
    use[cBandPlace] = std::max (use[cBandPlace], 0.5f);
    lean = range (s, -0.45f, 0.45f);
    attack = range (s, 0.06f, 0.22f);
    release = range (s, 0.18f, 0.35f);
    throwFor = range (s, 0.03f, 0.12f);
    ride = range (s, 0.4f, 0.9f);
    grown = true;
}

void Hand::rest() noexcept
{
    remaining = 0.0;
    env = kick = 0.0f;
    frozen = threw = false;
    target = Touch {};
    touch = Touch {};
    tick = 0;
}

void Hand::start (const Gesture& g, double lengthSamples, double barSamples, float emptyHz, float reach,
                  float brightness) noexcept
{
    const auto& u = g.u;
    for (int j = 0; j < oscillators; ++j)
    {
        amp[j] = 0.35f + 0.65f * u[(size_t) j];
        phase[j] = tau * u[(size_t) (4 + j)] + lean;
    }
    for (int c = 0; c < channels; ++c)
        channelGain[c] = use[c] * (0.55f + 0.9f * u[(size_t) (8 + c % 5)]);
    speedSign = u[2] < 0.5f ? -1.0f : 1.0f;
    reachNow = clamp01 (reach);
    brightNow = clamp01 (brightness);
    emptyNow = std::clamp (emptyHz, 150.0f, 6000.0f);
    bar = std::max (barSamples, 1000.0);
    length = remaining = std::max (lengthSamples, 0.25 * bar);
    frozen = threw = false;
    tick = 0;
}

void Hand::control() noexcept
{
    if (remaining <= 0.0)
    {
        env = 0.0f;
        target = Touch {};
        target.bandHz = touch.bandHz;
        return;
    }
    const float t = (float) (1.0 - remaining / length);
    env = smooth01 (t / attack) * smooth01 ((1.0f - t) / release);

    // the oscillators: their own pace, their pull on each other, and the music's pull
    const float perBar = tau * 16.0f / (float) bar;
    float next[oscillators];
    for (int j = 0; j < oscillators; ++j)
    {
        float d = pace[j];
        for (int k = 0; k < oscillators; ++k)
            d += pull[j][k] * std::sin (phase[k] - phase[j]);
        next[j] = phase[j] + perBar * d - 0.25f * kick * std::sin (phase[j]); // a hit pulls the strokes onto it
    }
    for (int j = 0; j < oscillators; ++j)
        phase[j] = wrapAngle (next[j]);
    kick *= 0.8f;

    float y[channels];
    for (int c = 0; c < channels; ++c)
    {
        float sum = 0.0f;
        for (int j = 0; j < oscillators; ++j)
            sum += weight[c][j] * amp[j] * std::sin (phase[j] + offset[c][j]);
        y[c] = std::tanh (snap[c] * sum) / std::tanh (snap[c]);
        y[c] = std::clamp (y[c], -1.0f, 1.0f);
    }

    const float r = (0.45f + 0.55f * reachNow) * env;
    target.speed = std::pow (2.0f, speedSign * 0.5f * y[cSpeed] * channelGain[cSpeed] * r);
    target.feed = 0.32f * y[cFeed] * channelGain[cFeed] * r;
    target.space = 0.45f * y[cSpace] * channelGain[cSpace] * r;
    target.band = clamp01 (0.5f + 0.6f * y[cBandAmount]) * channelGain[cBandAmount] * env;
    target.bandHz = std::clamp (emptyNow * std::pow (2.0f, 0.9f * y[cBandPlace] * channelGain[cBandPlace]
                                                              + 0.8f * (brightNow - 0.5f)),
                                150.0f, 7000.0f);
    if (! frozen && channelGain[cFreeze] > 0.3f && y[cFreeze] > 0.65f && env > 0.6f)
        frozen = true;
    else if (frozen && (y[cFreeze] < 0.2f || env < 0.6f))
        frozen = false;
    target.freeze = frozen ? 1.0f : 0.0f;
    // it throws the moment in as it starts (longer when wound up), and rides the return up
    // while it writes, the way an engineer does
    target.throwing = t < throwFor * (0.6f + 0.8f * reachNow);
    target.lift = 1.0f + ride * env * (0.5f + 0.5f * reachNow);
    threw = threw || target.throwing;
}

const Touch& Hand::process (float onset) noexcept
{
    // a hit is an event: the onset crossing up through one half
    if (onset > 0.5f && armed)
    {
        kick = std::max (kick, onset);
        armed = false;
    }
    else if (onset < 0.3f)
    {
        armed = true;
    }
    if ((tick++ & 15) == 0)
        control();
    if (remaining > 0.0)
        remaining -= 1.0;

    touch.feed += cSmooth * (target.feed - touch.feed);
    touch.space += cSmooth * (target.space - touch.space);
    touch.band += cSmooth * (target.band - touch.band);
    touch.bandHz += cSmooth * (target.bandHz - touch.bandHz);
    touch.freeze += 0.25f * cSmooth * (target.freeze - touch.freeze);
    touch.throwing = target.throwing;
    touch.lift += cSmooth * (target.lift - touch.lift);
    const float want = touch.speed + cSmooth * (target.speed - touch.speed);
    touch.speed += std::clamp (want - touch.speed, -speedRate, speedRate);
    return touch;
}
} // namespace dendrite
