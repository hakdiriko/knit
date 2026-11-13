#include "Fly.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace dendrite
{
namespace
{
// Shiu et al. (2024): rest and reset -52 mV, threshold -45 mV, membrane 20 ms, synapses 5 ms,
// refractory 2.2 ms, 0.275 mV per synapse.
constexpr float vRest = -52.0f, vThreshold = -45.0f, wSynapse = 0.275f;
constexpr int refractorySteps = 4;
// how each sense tires: (use per second at full drive, recovery in seconds at awake 0.5)
constexpr float tireUse[6] = { 0.8f, 0.8f, 0.3f, 12.0f, 12.0f, 12.0f };
constexpr float tireBack[6] = { 3.0f, 3.0f, 2.0f, 10.0f, 10.0f, 10.0f };

float coeff (float seconds, float dt) noexcept { return 1.0f - std::exp (-dt / seconds); }
} // namespace

void Fly::prepare (float sampleRate) noexcept
{
    sr = sampleRate;
    stepEvery = sr * dtSec;
    const float hz[6][2] = { { 40.0f, 300.0f }, { 300.0f, 4000.0f }, { 20.0f, 8000.0f },   // ears: B, A, CE
                             { 40.0f, 200.0f }, { 200.0f, 2000.0f }, { 2000.0f, 10000.0f } }; // eyes: low, mid, high
    const float envSec[3] = { 0.010f, 0.008f, 0.25f };
    for (int side = 0; side < 2; ++side)
        for (int k = 0; k < 3; ++k)
        {
            auto& e = ears[(size_t) (3 * side + k)];
            e.hp.highpass (hz[k][0], 0.707f, sr);
            e.lp.lowpass (hz[k][1], 0.707f, sr);
            e.a = coeff (envSec[k], 1.0f / sr);
            auto& y = eyes[(size_t) (3 * side + k)];
            y.hp.highpass (hz[3 + k][0], 0.707f, sr);
            y.lp.lowpass (hz[3 + k][1], 0.707f, sr);
            y.af = coeff (0.005f, 1.0f / sr);
            y.as = coeff (0.08f, 1.0f / sr);
        }
    scaleUp = coeff (0.05f, dtSec);
    scaleDown = coeff (30.0f, dtSec);
    rest();
}

void Fly::rest() noexcept
{
    v.fill (vRest);
    g.fill (0.0f);
    refractory.fill (0.0f);
    firedCount.fill (0);
    slot = 0;
    rng = 0x666C792E322E3031ull;
    for (auto& e : ears)
    {
        e.hp.clear();
        e.lp.clear();
        e.env = 0.0f;
    }
    for (auto& y : eyes)
    {
        y.hp.clear();
        y.lp.clear();
        y.fast = y.slow = 0.0f;
    }
    drive.fill (0.0f);
    fresh.fill (1.0f);
    scale.fill (0.0f);
    escape = groom = groomHabit = rub = lean = leanHabit = excited = 0.0f;
    sinceJump = 10.0f;
    lurchLeft = lurchSize = throwLeft = freezeFrom = freezeLeft = throwGlow = band = 0.0f;
    target = touch = Touch {};
    now = Doing {};
    untilStep = 0.0f;
}

const Touch& Fly::process (float left, float right) noexcept
{
    if (awakeNow <= 0.0f)
    {
        touch = Touch {};
        now = Doing {};
        return touch;
    }
    // the senses run every sample; what they hold is read at the brain's pace
    for (int k = 0; k < 3; ++k)
    {
        ears[(size_t) k].run (left);
        ears[(size_t) (3 + k)].run (right);
        eyes[(size_t) k].run (left);
        eyes[(size_t) (3 + k)].run (right);
    }
    untilStep -= 1.0f;
    if (untilStep <= 0.0f)
    {
        untilStep += stepEvery;
        for (int k = 0; k < 3; ++k)
        {
            drive[(size_t) (k * 2)] = ears[(size_t) k].env;
            drive[(size_t) (k * 2 + 1)] = ears[(size_t) (3 + k)].env;
            drive[(size_t) (6 + k * 2)] = eyes[(size_t) k].looming();
            drive[(size_t) (6 + k * 2 + 1)] = eyes[(size_t) (3 + k)].looming();
        }
        step();
        reflexes();
    }
    return touch;
}

void Fly::step() noexcept
{
    // each sense is heard against its own recent loudest (one scale per sense for both sides)
    const float rate = 80.0f + 240.0f * awakeNow;
    const float recoverBy = 1.5f - awakeNow;
    float hit[12];
    for (int s = 0; s < 6; ++s)
    {
        float& sc = scale[(size_t) s];
        const float x = std::max (drive[(size_t) (2 * s)], drive[(size_t) (2 * s + 1)]);
        if (x > sc)
            sc += scaleUp * (x - sc);
        else
            sc -= scaleDown * sc;
        const float floor = s < 3 ? 1.0e-3f : 0.3f;
        for (int side = 0; side < 2; ++side)
        {
            const int k = 2 * s + side;
            const float d = std::min (1.2f, drive[(size_t) k] / std::max (sc, floor));
            float& f = fresh[(size_t) k];
            f += dtSec * ((1.0f - f) / (tireBack[s] * recoverBy) - tireUse[s] * d * f);
            hit[k] = std::min (0.5f, rate * d * f * dtSec); // the chance its neurons fire this step
        }
    }

    // spikes arriving now, 2.5 ms after they left
    const int arrive = slot;
    for (int n = 0; n < firedCount[(size_t) arrive]; ++n)
    {
        const int pre = fired[(size_t) arrive][(size_t) n];
        for (uint32_t s = flydata::rowStart[pre]; s < flydata::rowStart[pre + 1]; ++s)
            g[flydata::target[s]] += wSynapse * (float) flydata::weight[s];
    }
    firedCount[(size_t) arrive] = 0;

    const float am = dtSec / 0.020f, ag = std::exp (-dtSec / 0.005f);
    {
        // branch free, so it vectorises: a resting neuron neither integrates nor decays
        float* __restrict vp = v.data();
        float* __restrict gp = g.data();
        float* __restrict rp = refractory.data();
        for (int n = 0; n < padded; ++n)
        {
            const float on = rp[n] > 0.5f ? 0.0f : 1.0f;
            vp[n] += on * am * (vRest - vp[n] + gp[n]);
            gp[n] *= 1.0f - on * (1.0f - ag);
            rp[n] = std::max (0.0f, rp[n] - 1.0f);
        }
    }
    // the senses fire their neurons, Poisson
    for (int k = 0; k < 12; ++k)
    {
        if (hit[k] <= 1.0e-6f)
            continue;
        const uint32_t limit = (uint32_t) (hit[k] * 4294967295.0f);
        for (int s = flydata::senseStart[k]; s < flydata::senseStart[k + 1]; ++s)
        {
            rng ^= rng << 13;
            rng ^= rng >> 7;
            rng ^= rng << 17;
            if ((uint32_t) (rng >> 32) < limit)
                v[flydata::senseNeuron[s]] = vThreshold + 1.0f;
        }
    }
    auto& out = fired[(size_t) arrive];
    int count = 0;
    for (int n = 0; n < neurons; ++n)
        if (v[(size_t) n] > vThreshold) [[unlikely]]
        {
            v[(size_t) n] = vRest;
            g[(size_t) n] = 0.0f;
            refractory[(size_t) n] = (float) refractorySteps;
            out[(size_t) count++] = (uint16_t) n;
            spikes[(size_t) n].fetch_add (1, std::memory_order_relaxed);
        }
    firedCount[(size_t) arrive] = count;
    slot = (slot + 1) % delaySlots;
}

void Fly::reflexes() noexcept
{
    const auto& out = fired[(size_t) ((slot + delaySlots - 1) % delaySlots)];
    const int count = firedCount[(size_t) ((slot + delaySlots - 1) % delaySlots)];
    // who fired this step, by what they do
    int esc = 0, legA = 0, legB = 0, lefts = 0, rights = 0;
    for (int k = 0; k < count; ++k)
    {
        const uint16_t n = out[(size_t) k];
        for (auto e : flydata::escapeNeuron)
            esc += e == n ? 1 : 0;
        legA += flydata::legNeuron[0] == n ? 1 : 0;
        legB += flydata::legNeuron[1] == n ? 1 : 0;
        // descending neurons are sorted, so a binary search finds them
        lefts += std::binary_search (std::begin (flydata::leftNeuron), std::end (flydata::leftNeuron), n) ? 1 : 0;
        rights += std::binary_search (std::begin (flydata::rightNeuron), std::end (flydata::rightNeuron), n) ? 1 : 0;
    }
    const float dt = dtSec;
    const float escRate = (float) esc / ((float) flydata::escapeCount * dt);
    const float groomRate = (float) (legA + legB) / ((float) flydata::groomAll * dt);
    const float l = (float) lefts / ((float) flydata::leftAll * dt), r = (float) rights / ((float) flydata::rightAll * dt);

    escape += coeff (0.03f, dt) * (escRate - escape);
    groom += coeff (0.15f, dt) * (groomRate - groom);
    groomHabit += coeff (4.0f, dt) * (groom - groomHabit);
    rub += coeff (0.08f, dt) * ((float) (legA - legB) / dt - rub);
    const float turnNow = l - r;
    lean += coeff (0.25f, dt) * (turnNow - lean);
    leanHabit += coeff (6.0f, dt) * (lean - leanHabit);
    excited += coeff (0.4f, dt) * (l + r - excited);

    // the jump: the giant fiber's path firing hard, at most once a second
    sinceJump += dt;
    if (escape > 45.0f && sinceJump >= 1.0f)
    {
        sinceJump = 0.0f;
        lurchSize = std::min (1.0f, escape / 80.0f);
        lurchLeft = 0.9f;
        throwLeft = 0.12f + 0.2f * lurchSize;
        if (lurchSize > 0.7f)
        {
            freezeFrom = 0.1f;
            freezeLeft = 1.1f;
        }
    }
    float lurch = 0.0f;
    if (lurchLeft > 0.0f)
    {
        lurch = -lurchSize * std::sin (pi * (0.9f - lurchLeft) / 0.9f);
        lurchLeft -= dt;
    }
    const bool throwing = throwLeft > 0.0f;
    throwLeft -= dt;
    bool freezing = false;
    if (freezeFrom > 0.0f)
        freezeFrom -= dt;
    else if (freezeLeft > 0.0f)
    {
        freezing = true;
        freezeLeft -= dt;
    }

    const float gm = clamp01 ((groom - groomHabit + 0.5f) / 7.0f);
    band = 0.85f * gm * std::sqrt (gm);
    throwGlow += coeff (0.3f, dt) * ((throwing ? 1.0f : 0.0f) - throwGlow);
    const float leanNow = std::clamp ((lean - leanHabit) / 0.8f, -1.0f, 1.0f);

    touch.speed = std::pow (2.0f, 0.4f * lurch);
    touch.feed = clamp01 (excited / 4.0f) * 0.18f - 0.05f;
    touch.space = 0.35f * leanNow;
    touch.band = band;
    touch.bandHz = std::clamp (emptyHz * std::pow (2.0f, std::clamp (rub / 40.0f, -1.2f, 1.2f)), 150.0f, 7000.0f);
    touch.freeze += coeff (0.05f, dt) * ((freezing ? 1.0f : 0.0f) - touch.freeze);
    touch.throwing = throwing;
    touch.lift = 1.0f + 0.6f * band + 0.8f * throwGlow;

    busyNow = lurchLeft > 0.0f || band > (busyNow ? 0.08f : 0.3f);
    now.jump = lurchLeft > 0.0f ? lurchSize : 0.0f;
    now.semitones = 12.0f * 0.4f * lurch;
    now.throwing = throwing;
    now.frozen = touch.freeze > 0.5f;
    now.scratch = band;
    now.scratchHz = touch.bandHz;
    now.turn = leanNow;
}
} // namespace dendrite
