#include "Fly.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace dendrite
{
namespace
{
// Shiu et al. (2024): rest and reset -52 mV, threshold -45 mV, membrane 20 ms, synapses 5 ms,
// refractory 2.2 ms, 0.275 mV per synapse (per FlyWire synapse: each atlas says what its synapse is worth).
constexpr float vRest = -52.0f, vThreshold = -45.0f, wSynapse = flydata::synapseMv;
constexpr int refractorySteps = 4;
// how each sense tires: (use per second at full drive, recovery in seconds at awake 0.5); the last is smell
constexpr float tireUse[7] = { 0.8f, 0.8f, 0.3f, 12.0f, 12.0f, 12.0f, 0.5f };
constexpr float tireBack[7] = { 3.0f, 3.0f, 2.0f, 10.0f, 10.0f, 10.0f, 5.0f };
// the inner life: restless kicks (per neuron per second, calm; mV), learning rate, forgetting (s)
constexpr float kickMv = 1.5f; // kick rate, learning rate and forgetting are in Taste
constexpr float liveSec = 0.010f; // traces, moods and learning run every 10 ms

float coeff (float seconds, float dt) noexcept { return 1.0f - std::exp (-dt / seconds); }
} // namespace

Fly::Fly()
{
    w.resize ((size_t) flydata::synapses);
    role.assign ((size_t) neurons, -1);
    for (int k = 0; k < flydata::kenyonCount; ++k)
        role[flydata::kenyon[k]] = (int16_t) k;
    for (int d = 0; d < flydata::danCount; ++d)
        role[flydata::dan[d]] = (int16_t) (-2 - d);
    for (int m = 0; m < flydata::mbonCount; ++m)
        role[flydata::mbon[m]] = (int16_t) (-10000 - m);
    does.assign ((size_t) neurons, 0);
    for (auto n : flydata::escapeNeuron)
        does[n] |= doesEscape;
    does[flydata::legNeuron[0]] |= doesLegA;
    does[flydata::legNeuron[1]] |= doesLegB;
    for (auto n : flydata::leftNeuron)
        does[n] |= doesLeft;
    for (auto n : flydata::rightNeuron)
        does[n] |= doesRight;
    for (int q = 0; q < flydata::steerCount; ++q)
    {
        does[flydata::steerLeft[q]] |= doesSteerL;
        does[flydata::steerRight[q]] |= doesSteerR;
    }
    double birth = 0.0;
    plasticAtBirth.resize ((size_t) flydata::plasticCount);
    for (int p = 0; p < flydata::plasticCount; ++p)
    {
        birth += (double) flydata::weight[flydata::plastic[p]];
        plasticAtBirth[(size_t) p] = wSynapse * (float) flydata::weight[flydata::plastic[p]];
    }
    plasticBirth = (float) std::max (1.0, birth * (double) wSynapse);
    // an inhibitory synapse can weigh more than an excitatory one (the atlas's E/I balance; 1 for FlyWire)
    const float wInhibitory = flydata::inhibitoryScale * wSynapse;
    for (int s = 0; s < flydata::synapses; ++s)
        w[(size_t) s] = (flydata::weight[s] < 0 ? wInhibitory : wSynapse) * (float) flydata::weight[s];
    forget();
}

void Fly::forget() noexcept
{
    // only the KC to MBON synapses learn, so a new fly resets those 28,761 and not all million
    // (forget runs on the audio thread when the window asks for a new fly)
    for (int p = 0; p < flydata::plasticCount; ++p)
        w[flydata::plastic[p]] = plasticAtBirth[(size_t) p];
    arousal = 0.3f;
    fatigue = 0.0f;
    memory = 1.0f;
    feelingHabit = 0.0f;
    livedSec = 0.0;
}

bool Fly::Memory::valid() const noexcept
{
    const auto fine = [] (float x, float lo, float hi) { return std::isfinite (x) && x >= lo && x <= hi; };
    return magic == tag && count == (uint32_t) flydata::plasticCount && fine (arousal, 0.0f, 1.0f)
        && fine (fatigue, 0.0f, 1.0f) && fine (feelingHabit, -1.0f, 1.0f) && std::isfinite (lived) && lived >= 0.0;
}

void Fly::save (Memory& m) const noexcept
{
    m.magic = Memory::tag;
    m.count = (uint32_t) flydata::plasticCount;
    m.arousal = clamp01 (arousal);
    m.fatigue = clamp01 (fatigue);
    m.feelingHabit = std::clamp (feelingHabit, -1.0f, 1.0f);
    m.lived = livedSec;
    for (int p = 0; p < flydata::plasticCount; ++p)
    {
        const float birth = wSynapse * (float) flydata::weight[flydata::plastic[p]];
        const float share = std::abs (birth) > 1.0e-9f ? clamp01 (w[flydata::plastic[p]] / birth) : 1.0f;
        m.strength[(size_t) p] = (uint16_t) std::lround (share * 65535.0f);
    }
}

void Fly::adopt (const Memory& m) noexcept
{
    if (! m.valid())
        return;
    double sum = 0.0;
    for (int p = 0; p < flydata::plasticCount; ++p)
    {
        const float birth = wSynapse * (float) flydata::weight[flydata::plastic[p]];
        auto& wp = w[flydata::plastic[p]];
        wp = birth * ((float) m.strength[(size_t) p] * (1.0f / 65535.0f));
        sum += (double) wp;
    }
    memory = (float) (sum / (double) plasticBirth);
    arousal = m.arousal;
    fatigue = m.fatigue;
    feelingHabit = m.feelingHabit;
    feeling = feelingHabit;
    livedSec = m.lived;
}

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
    touchGlide = coeff (0.004f, 1.0f / sr);
    scaleUp = coeff (0.05f, dtSec);
    scaleDown = coeff (30.0f, dtSec);
    for (int b = 0; b < flydata::smells; ++b)
    {
        // 60 Hz .. 12 kHz in even steps of pitch, each band about as wide as the step
        const float r = std::pow (200.0f, 1.0f / (float) flydata::smells);
        nose[(size_t) b].bp.bandpass (60.0f * std::pow (r, (float) b + 0.5f), std::sqrt (r) / (r - 1.0f), sr);
    }
    noseA = coeff (0.010f, 1.0f / sr);
    peakUp = coeff (0.05f, 0.005f);
    peakDown = coeff (30.0f, 0.005f);
    rest(); // the memory and moods stay (they start fresh when the plug-in loads)
}

void Fly::rest() noexcept
{
    v.fill (vRest);
    g.fill (0.0f);
    refractory.fill (0.0f);
    firedCount.fill (0);
    slot = 0;
    rng = rngSeed;
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
    for (auto& b : nose)
    {
        b.bp.clear();
        b.env = b.peak = 0.0f;
    }
    untilSmell = untilLive = 0;
    kcTrace.fill (0.0f);
    danTrace.fill (0.0f);
    kcSpikes.fill (0.0f);
    dopamineBefore.fill (0.0f);
    dopamineTonic.fill (0.0f);
    reliefIn = reliefLeft = 0.0f;
    mbonSpikes.fill (0.0f);
    mbonRate.fill (0.0f);
    dopamine.fill (0.0f);
    kickNext = 0.0f;
    stepSpikes = 0;
    brainBusy = 0.0f;
    feeling = feelingHabit;
    drive.fill (0.0f);
    fresh.fill (1.0f);
    scale.fill (0.0f);
    escape = groom = groomHabit = rub = lean = leanHabit = excited = 0.0f;
    sinceJump = 10.0f;
    lurchSign = 1.0f;
    lurchLength = 0.9f;
    fear = fearNow = readiness = 0.0f;
    lurchLeft = lurchSize = throwLeft = freezeFrom = freezeLeft = throwGlow = band = 0.0f;
    target = touch = Touch {};
    lastTick = -1;
    sinceMove16 = 64.0f;
    busyShare = 0.0f;
    pulse = 0.5f;
    slotHit = false;
    lastMoves = {};
    armed = false;
    legCount = legAt = 0;
    moveNow = none;
    stretch = 1.0f;
    moveSize = throw16 = freeze16 = 0.0f;
    freezeIn16 = -1.0f;
    heldBand = heldLean = heldFeed = 0.0f;
    heldBandHz = 1000.0f;
    now = Doing {};
    untilStep = 0.0f;
    for (auto& sub : standRing)
        sub.fill (0.0f);
    standAcc.fill (0.0f);
    standOut.fill (0.0f);
    standSteps = standAt = 0;
}

const Touch& Fly::process (float left, float right) noexcept
{
    if (awakeNow <= 0.0f)
    {
        // asleep: the echo glides back to itself
        target = Touch {};
        now = Doing {};
        armed = false;
        legCount = legAt = 0;
        moveNow = none;
        stretch = 1.0f;
        throw16 = freeze16 = 0.0f;
        freezeIn16 = -1.0f;
        heldBand = heldLean = heldFeed = 0.0f;
        touch.speed += touchGlide * (1.0f - touch.speed);
        touch.feed += touchGlide * (0.0f - touch.feed);
        touch.space += touchGlide * (0.0f - touch.space);
        touch.band += touchGlide * (0.0f - touch.band);
        touch.freeze += touchGlide * (0.0f - touch.freeze);
        touch.lift += touchGlide * (1.0f - touch.lift);
        touch.throwing = false;
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
    const float mono = 0.5f * (left + right);
    for (auto& b : nose)
        b.env += noseA * (std::abs (b.bp.process (mono)) - b.env);
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
        if (--untilSmell <= 0)
        {
            untilSmell = 10;
            smell();
        }
        step();
        if (stand != nullptr)
            standStep();
        reflexes();
        if (--untilLive <= 0)
        {
            untilLive = 20;
            live();
        }
    }
    // the brain moves in 0.5 ms steps; the echo is moved smoothly, sample by sample, or the tape's
    // speed would jump every step and the repeats would crackle like a bitcrusher
    touch.speed += touchGlide * (target.speed - touch.speed);
    touch.feed += touchGlide * (target.feed - touch.feed);
    touch.space += touchGlide * (target.space - touch.space);
    touch.band += touchGlide * (target.band - touch.band);
    touch.bandHz += touchGlide * (target.bandHz - touch.bandHz);
    touch.freeze += touchGlide * (target.freeze - touch.freeze);
    touch.lift += touchGlide * (target.lift - touch.lift);
    touch.throwing = target.throwing;
    return touch;
}

void Fly::smell() noexcept
{
    // each band against its own recent loudest; an odour is the few bands that stand above the
    // spectrum's own middle, not all of them
    std::array<float, flydata::smells> n {}, sorted {};
    for (int b = 0; b < flydata::smells; ++b)
    {
        auto& x = nose[(size_t) b];
        x.peak += (x.env > x.peak ? peakUp : peakDown) * (x.env - x.peak);
        n[(size_t) b] = std::log1p (4.0f * x.env / std::max (x.peak, 1.0e-4f)) / std::log1p (4.0f);
    }
    sorted = n;
    std::nth_element (sorted.begin(), sorted.begin() + flydata::smells / 2, sorted.end());
    const float middle = sorted[(size_t) (flydata::smells / 2)];
    for (int b = 0; b < flydata::smells; ++b)
        drive[(size_t) (flydata::senses + b)] = std::min (1.2f, 2.0f * std::max (0.0f, n[(size_t) b] - middle));
}

void Fly::live() noexcept
{
    const float dt = liveSec;
    const float kcFade = std::exp (-dt / 1.0f), danFade = std::exp (-dt / 0.5f), mbonA = coeff (0.3f, dt);
    for (auto& t : kcTrace)
        t *= kcFade;
    for (auto& t : danTrace)
        t *= danFade;
    for (int m = 0; m < flydata::mbonCount; ++m)
    {
        mbonRate[(size_t) m] += mbonA * (mbonSpikes[(size_t) m] / dt - mbonRate[(size_t) m]);
        mbonSpikes[(size_t) m] = 0.0f;
    }

    // how busy its brain is (spikes per step), the moods, and what its MBONs say
    brainBusy += coeff (1.0f, dt) * ((float) stepSpikes / (20.0f * dtSec) - brainBusy);
    stepSpikes = 0;
    float closer = 0.0f, away = 0.0f, nCloser = 0.0f, nAway = 0.0f;
    for (int m = 0; m < flydata::mbonCount; ++m)
    {
        if (flydata::mbonSign[m] > 0)
        {
            closer += mbonRate[(size_t) m];
            nCloser += 1.0f;
        }
        else
        {
            away += mbonRate[(size_t) m];
            nAway += 1.0f;
        }
    }
    closer /= std::max (1.0f, nCloser);
    away /= std::max (1.0f, nAway);
    feeling = std::clamp ((closer - away) / (closer + away + 2.0f) - flydata::feelingRest, -1.0f, 1.0f);
    if (! innerLife)
        return;
    livedSec += (double) dt;

    // arousal: every jump winds it up a little (in the reflexes); it calms over about forty seconds
    arousal += dt * (0.2f - arousal) / taste.calm;
    fatigue += dt * (clamp01 ((brainBusy - fatigueFrom) / fatigueSpan) - fatigue) / 120.0f;
    feelingHabit += coeff (120.0f, dt) * (feeling - feelingHabit);

    // the learning: dopamine reaching each MBON, through the DAN -> MBON wiring
    dopamine.fill (0.0f);
    for (int k = 0; k < flydata::dopamineCount; ++k)
        dopamine[flydata::dopamineMbon[k]] += flydata::dopamineShare[k] * danTrace[flydata::dopamineDan[k]];
    std::array<float, flydata::mbonCount> d {};
    std::array<float, flydata::kenyonCount> active {}, kcNow {};
    for (int k = 0; k < flydata::kenyonCount; ++k)
    {
        active[(size_t) k] = 1.0f - std::exp (-kcTrace[(size_t) k] / 5.0f);
        kcNow[(size_t) k] = 1.0f - std::exp (-kcSpikes[(size_t) k]);
        kcSpikes[(size_t) k] = 0.0f;
    }
    std::array<float, flydata::mbonCount> before {};
    for (int m = 0; m < flydata::mbonCount; ++m)
    {
        // only a burst above its steady level teaches (dopamine neurons fire all the time; learning
        // follows their phasic bursts, Schultz 1998, Cohn et al. 2015)
        dopamineTonic[(size_t) m] += coeff (tonicSec, dt) * (dopamine[(size_t) m] - dopamineTonic[(size_t) m]);
        dopamine[(size_t) m] = std::max (0.0f, dopamine[(size_t) m] - dopamineTonic[(size_t) m]);
        d[(size_t) m] = 1.0f - std::exp (-dopamine[(size_t) m] / 5.0f);
        before[(size_t) m] = 1.0f - std::exp (-dopamineBefore[(size_t) m] / 5.0f);
    }
    double sum = 0.0;
    std::array<double, 2> heard {}, heardAtBirth {}; // through the keep-away MBONs, and the come-closer ones
    const float back = dt / taste.forget;
    for (int p = 0; p < flydata::plasticCount; ++p)
    {
        const float dm = d[flydata::plasticMbon[p]];
        const float t = kcTrace[flydata::plasticKenyon[p]];
        auto& wp = w[flydata::plastic[p]];
        const float birth = plasticAtBirth[(size_t) p];
        const int side = flydata::mbonSign[flydata::plasticMbon[p]] > 0 ? 1 : 0;
        heard[(size_t) side] += (double) (t * wp);
        heardAtBirth[(size_t) side] += (double) (t * birth);
        // where dopamine meets an active Kenyon cell, the synapse weakens; slowly it comes back
        if (dm > 1.0e-4f && t > 1.0e-3f)
            wp *= 1.0f - dt * taste.learn * dm * active[flydata::plasticKenyon[p]];
        // the other way round, dopamine first and the Kenyon cell after, it strengthens back toward
        // birth (Handler et al. 2019: the order decides the sign, through two dopamine receptors)
        const float b = before[flydata::plasticMbon[p]], k = kcNow[flydata::plasticKenyon[p]];
        if (b > 1.0e-4f && k > 1.0e-3f)
            wp += dt * taste.learn * b * k * (birth - wp);
        wp += (birth - wp) * back;
        sum += (double) wp;
    }
    memory = (float) (sum / (double) plasticBirth);
    // what reached the MBONs now is a moment ago for the next 10 ms
    const float beforeA = coeff (dopamineBeforeSec, dt);
    for (int m = 0; m < flydata::mbonCount; ++m)
        dopamineBefore[(size_t) m] += beforeA * (dopamine[(size_t) m] - dopamineBefore[(size_t) m]);
    // fear: what it smells now reaches its MBONs weaker than it would have at birth, because
    // these Kenyon cells were active when it got hurt before
    // reach its come-closer MBONs (the punished compartments) weaker than at birth; what weakened its
    // keep-away MBONs (the rewarded ones, PAM's) counts the other way (Aso et al. 2014: the fly
    // goes where the balance of the two sends it). Dopamine everywhere at once cancels out.
    const auto lost = [&] (int k) { return heardAtBirth[(size_t) k] > 1.0e-3 ? (float) (1.0 - heard[(size_t) k] / heardAtBirth[(size_t) k]) : 0.0f; };
    fearNow = std::max (0.0f, lost (1) - lost (0));
    fear += coeff (0.2f, dt) * (clamp01 (fearNow / fearFull) - fear);
}

void Fly::step() noexcept
{
    // each sense is heard against its own recent loudest (one scale per sense for both sides)
    const float rate = 80.0f + 240.0f * awakeNow;
    const float recoverBy = 1.5f - awakeNow;
    // the moods turn its senses up (wound up) or down (tired)
    const float gain = innerLife ? (0.75f + 0.5f * arousal) * (1.0f - 0.35f * fatigue) : 1.0f;
    float hit[inputs];
    for (int k = flydata::senses; k < inputs; ++k)
    {
        const float d = drive[(size_t) k];
        float& f = fresh[(size_t) k];
        f += dtSec * ((1.0f - f) / (tireBack[6] * recoverBy) - tireUse[6] * d * f);
        hit[k] = std::min (0.5f, rate * gain * d * f * dtSec);
    }
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
            {
                const bool eye = s >= 3;
                f += dtSec * ((1.0f - f) / ((eye ? taste.eyesBack : tireBack[s]) * recoverBy) - (eye ? taste.eyesTire : tireUse[s]) * d * f);
            }
            // the chance its neurons fire this step (senseGain: how hard this atlas's ear or eye neurons
            // must fire for the music to reach the brain as hard as it reached the FlyWire fly)
            hit[k] = std::min (0.5f, flydata::senseGain[k] * rate * gain * d * f * dtSec);
        }
    }
    for (int k = 0; k < inputs; ++k)
        heardNow[(size_t) k] = hit[k];
    if (probing)
        for (int k = 0; k < inputs; ++k)
        {
            probeSum.drive[(size_t) k] += drive[(size_t) k];
            probeSum.heard[(size_t) k] += hit[k];
        }
    if (stand != nullptr)
        return; // no brain: the stand-in answers in standStep()

    // spikes arriving now, 2.5 ms after they left
    const int arrive = slot;
    for (int n = 0; n < firedCount[(size_t) arrive]; ++n)
    {
        const int pre = fired[(size_t) arrive][(size_t) n];
        for (uint32_t s = flydata::rowStart[pre]; s < flydata::rowStart[pre + 1]; ++s)
            g[flydata::target[s]] += w[(size_t) s];
    }
    firedCount[(size_t) arrive] = 0;

    const float am = dtSec / 0.020f, ag = std::exp (-dtSec / 0.005f);
    {
        // branch free, so it vectorises: a resting neuron neither integrates nor decays
        float* __restrict vp = v.data();
        float* __restrict gp = g.data();
        float* __restrict rp = refractory.data();
        // and it notes which sixteen neurons may fire, so the spike search skips the rest
        for (int c = 0; c < padded; c += 16)
        {
            int any = 0;
            for (int n = c; n < c + 16; ++n)
            {
                const float on = rp[n] > 0.5f ? 0.0f : 1.0f;
                vp[n] += on * am * (vRest - vp[n] + gp[n]);
                gp[n] *= 1.0f - on * (1.0f - ag);
                rp[n] = std::max (0.0f, rp[n] - 1.0f);
                any |= vp[n] > vThreshold ? 1 : 0;
            }
            hot[(size_t) (c / 16)] = (uint8_t) any;
        }
    }
    // restless: a few small random kicks, about taste.restless per neuron per second (more when wound up),
    // found by jumping from one kicked neuron to the next instead of asking every neuron
    if (innerLife)
    {
        const float meanGap = 1.0f / (taste.restless * (1.0f + arousal) * dtSec);
        while (kickNext < (float) neurons)
        {
            v[(size_t) kickNext] += kickMv;
            hot[(size_t) kickNext / 16] = 1;
            kickNext += meanGap * -std::log (uniform());
        }
        kickNext -= (float) neurons;
        // a jump hurts: the punishing dopamine neurons fire
        if (escape > 30.0f)
        {
            for (int k = 0; k < flydata::ppl1Count; ++k)
                if (uniform() < 0.05f)
                {
                    v[flydata::ppl1[k]] = vThreshold + 1.0f;
                    hot[flydata::ppl1[k] / 16] = 1;
                }
            reliefIn = reliefDelay;
            reliefLeft = 0.0f;
        }
        // and when it ends, relief: a moment later the rewarding dopamine neurons (PAM) fire
        // (Tanimoto et al. 2004: what a fly smells just after a shock ends, it comes to like)
        else if (reliefIn > 0.0f)
        {
            reliefIn -= dtSec;
            if (reliefIn <= 0.0f)
                reliefLeft = reliefLength;
        }
        if (reliefLeft > 0.0f)
        {
            reliefLeft -= dtSec;
            const float p = 0.05f * taste.relief;
            for (int k = 0; k < flydata::pamCount; ++k)
                if (uniform() < p)
                {
                    v[flydata::pam[k]] = vThreshold + 1.0f;
                    hot[flydata::pam[k] / 16] = 1;
                }
        }
    }
    // the senses fire their neurons, Poisson
    for (int k = 0; k < inputs; ++k)
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
            {
                v[flydata::senseNeuron[s]] = vThreshold + 1.0f;
                hot[flydata::senseNeuron[s] / 16] = 1;
            }
        }
    }
    auto& out = fired[(size_t) arrive];
    int count = 0;
    for (int c = 0; c < padded / 16; ++c)
    {
        if (hot[(size_t) c] == 0)
            continue;
        for (int n = c * 16; n < std::min (neurons, c * 16 + 16); ++n)
        {
            if (v[(size_t) n] <= vThreshold)
                continue;
            v[(size_t) n] = vRest;
            g[(size_t) n] = 0.0f;
            refractory[(size_t) n] = (float) refractorySteps;
            out[(size_t) count++] = (uint16_t) n;
            spikes[(size_t) n].fetch_add (1, std::memory_order_relaxed);
            const int r = role[(size_t) n];
            if (r >= 0)
            {
                kcTrace[(size_t) r] += 1.0f;
                kcSpikes[(size_t) r] += 1.0f;
            }
            else if (r <= -10000)
                mbonSpikes[(size_t) (-10000 - r)] += 1.0f;
            else if (r <= -2)
                danTrace[(size_t) (-2 - r)] += 1.0f;
        }
    }
    firedCount[(size_t) arrive] = count;
    stepSpikes += count;
    slot = (slot + 1) % delaySlots;
}

void Fly::startMove (float size, float f) noexcept
{
    // Every slide is chosen so that both heads bend by simple ratios of the music's own pitch:
    //   slope -1/4 per 16th: the repeats rise to 7/4 and 2 (an octave)      slope +1/8: they fall to 5/8 and 1/2
    //   slope -1/2: they rise to 5/2 and 3                                  slope +1/6: they fall to 1/2 and 1/3
    // and every move ends where it began or on another note value (half or double the echo's time).
    const bool down = innerLife ? uniform() < f : true;
    const bool big = size * taste.lurch / 0.4f > 0.5f;
    const auto leg = [this] (int k, float slope, float sixteenths, float end) { legs[(size_t) k] = { slope, sixteenths, end }; };
    legAt = 0;
    moveSize = size;
    if (! down)
    {
        if (big && arousal > 0.45f && uniform() < 0.5f)
        {
            // rush: up an octave for an 8th, then the echo runs in double time, then falls back over a quarter
            moveNow = rush;
            leg (0, -0.25f, 2.0f, 0.5f);
            leg (1, 0.0f, arousal > 0.6f ? 8.0f : 4.0f, 0.5f);
            leg (2, 0.125f, 4.0f, 1.0f);
            legCount = 3;
            throw16 = 2.0f;
        }
        else if (big)
        {
            // leap: up an octave and a fifth for a 16th, back down through an octave over a dotted 8th
            moveNow = leap;
            leg (0, -0.5f, 1.0f, 0.5f);
            leg (1, 1.0f / 6.0f, 3.0f, 1.0f);
            legCount = 2;
            throw16 = 2.0f;
        }
        else
        {
            // hop: up an octave for a 16th, back over an 8th
            moveNow = hop;
            leg (0, -0.25f, 1.0f, 0.75f);
            leg (1, 0.125f, 2.0f, 1.0f);
            legCount = 2;
            throw16 = 1.0f;
        }
    }
    else if (big && f > 0.5f)
    {
        // dive: down an octave over a half note into half time, it stays there, then climbs back over a quarter
        moveNow = dive;
        leg (0, 0.125f, 8.0f, 2.0f);
        leg (1, 0.0f, f > 0.8f ? 16.0f : 8.0f, 2.0f);
        leg (2, -0.25f, 4.0f, 1.0f);
        legCount = 3;
        throw16 = 3.0f;
        freezeIn16 = 4.0f; // and holds its breath for a beat, from the next beat
    }
    else
    {
        // duck: down an octave for an 8th, back up in a 16th
        moveNow = duck;
        leg (0, 0.125f, 2.0f, 1.25f);
        leg (1, -0.25f, 1.0f, 1.0f);
        legCount = 2;
        throw16 = 2.0f;
        if (size > 0.7f)
            freezeIn16 = 2.0f;
    }
    // on bare material: nothing long, and half the time no tape at all, only a throw and a held breath
    if (1.0f - clamp01 ((pulse - 0.2f) / 0.2f) > 0.5f)
    {
        if (uniform() < 0.5f)
        {
            moveNow = hold;
            leg (0, 0.0f, 6.0f, 1.0f);
            legCount = 1;
            throw16 = 3.0f;
            freezeIn16 = 4.0f;
        }
        else
        {
            // sink: the one bend that keeps a chord a chord. Both heads fall by pure intervals (an
            // octave, and an octave and a fifth) for an 8th, then the tape snaps back in a 32nd,
            // too fast to be heard as a pitch. The other slides put one head on a sixth, a seventh
            // or a tenth, which drums hide and held harmony does not.
            moveNow = sink;
            leg (0, 1.0f / 6.0f, 2.0f, 4.0f / 3.0f);
            leg (1, -2.0f / 3.0f, 0.5f, 1.0f);
            legCount = 2;
            throw16 = 2.0f;
            freezeIn16 = -1.0f;
        }
    }
    // a third time the same move becomes its small opposite (a fly never repeats an escape exactly)
    if (moveNow == lastMoves[0] && moveNow == lastMoves[1])
    {
        const bool wasDown = moveNow == dive || moveNow == duck;
        if (moveNow == sink || moveNow == hold)
        {
            // on bare material the two gentle moves take turns
            const bool toHold = moveNow == sink;
            moveNow = toHold ? hold : sink;
            if (toHold)
            {
                leg (0, 0.0f, 6.0f, 1.0f);
                legCount = 1;
                throw16 = 3.0f;
                freezeIn16 = 4.0f;
            }
            else
            {
                leg (0, 1.0f / 6.0f, 2.0f, 4.0f / 3.0f);
                leg (1, -2.0f / 3.0f, 0.5f, 1.0f);
                legCount = 2;
                throw16 = 2.0f;
                freezeIn16 = -1.0f;
            }
        }
        else if (wasDown)
        {
            moveNow = hop;
            leg (0, -0.25f, 1.0f, 0.75f);
            leg (1, 0.125f, 2.0f, 1.0f);
            throw16 = 1.0f;
        }
        else
        {
            moveNow = duck;
            leg (0, 0.125f, 2.0f, 1.25f);
            leg (1, -0.25f, 1.0f, 1.0f);
            throw16 = 2.0f;
        }
        if (moveNow == hop || moveNow == duck)
        {
            legCount = 2;
            freezeIn16 = -1.0f;
        }
    }
    lastMoves[1] = lastMoves[0];
    lastMoves[0] = moveNow;
    if (innerLife)
        arousal += 0.12f * size * (1.0f - arousal);
}

void Fly::reflexes() noexcept
{
    const auto& out = fired[(size_t) ((slot + delaySlots - 1) % delaySlots)];
    const int count = firedCount[(size_t) ((slot + delaySlots - 1) % delaySlots)];
    // who fired this step, by what they do
    int esc = 0, legA = 0, legB = 0, lefts = 0, rights = 0, steerL = 0, steerR = 0;
    for (int k = 0; k < count; ++k)
    {
        const uint8_t d = does[out[(size_t) k]];
        if (d == 0)
            continue;
        esc += (d & doesEscape) ? 1 : 0;
        legA += (d & doesLegA) ? 1 : 0;
        legB += (d & doesLegB) ? 1 : 0;
        lefts += (d & doesLeft) ? 1 : 0;
        rights += (d & doesRight) ? 1 : 0;
        steerL += (d & doesSteerL) ? 1 : 0;
        steerR += (d & doesSteerR) ? 1 : 0;
    }
    const float dt = dtSec;
    const float escRate = (float) esc / ((float) flydata::escapeCount * dt);
    // groomGain, leanGain, excitedScale and feelingRest calibrate the electrodes per atlas (Atlas.h):
    // 1, 1, 1 and 0 for FlyWire, whose readouts the reflexes were tuned on
    const float groomRate = (float) (legA + legB) / ((float) flydata::groomAll * dt) * flydata::groomGain;
    const float l = (float) lefts / ((float) flydata::leftAll * dt), r = (float) rights / ((float) flydata::rightAll * dt);

    escape += coeff (0.03f, dt) * (escRate - escape);
    groom += coeff (0.15f, dt) * (groomRate - groom);
    // its turn: the steering neurons, DNa01 and DNa02 on each side (2.5; until then every descending
    // neuron's left against right, which mostly heard the music's level, not where it came from)
    const float turnNow = (float) (steerL - steerR) / ((float) flydata::steerCount * dt) * flydata::leanGain;
    lean += coeff (1.0f, dt) * (turnNow - lean);
    excited += coeff (0.4f, dt) * ((l + r) * flydata::excitedScale - excited);
    if (stand != nullptr)
    {
        // the stand-in's filter takes the place of the four output signals (its legs do not rub)
        escape = standOut[0];
        groom = standOut[1];
        lean = standOut[2];
        excited = standOut[3];
    }
    groomHabit += coeff (4.0f, dt) * (groom - groomHabit);
    rub += coeff (0.08f, dt) * ((float) (legA - legB) / dt - rub);
    leanHabit += coeff (6.0f, dt) * (lean - leanHabit);
    if (probing)
    {
        ++probeSum.steps;
        probeSum.escape += escape;
        probeSum.groom += groom;
        probeSum.lean += lean;
        probeSum.excited += excited;
    }

    // the jump: the giant fiber's path firing hard. Wound up, it jumps sooner and harder; tired,
    // it waits longer between jumps. What it remembers chooses the jump: the more of what it hears
    // once hurt, the likelier it dives (the tape slows, long, and it throws long and freezes);
    // otherwise it springs up (the tape speeds up, short).
    sinceJump += dt;
    // its readiness wanders by itself, slowly (as a real fly's escape does: the same loom makes it
    // jump one time and not the next), so not every hit makes it jump
    if (innerLife)
    {
        const float n = (uniform() + uniform() + uniform() + uniform() - 2.0f) * 1.7320508f;
        readiness += -readiness * dt / 3.0f + std::sqrt (2.0f * dt / 3.0f) * n;
    }
    const float jumpAt = innerLife ? 45.0f / taste.jumpy * (1.25f - 0.3f * arousal) * (1.0f + 0.4f * fatigue) * std::exp (-0.35f * readiness)
                                   : 45.0f;
    const float jumpRest = innerLife ? 1.0f + 1.5f * fatigue : 1.0f;
    const bool wantsJump = escape > jumpAt && sinceJump >= jumpRest;
    const float gmNow = clamp01 ((groom - groomHabit + 0.5f) / 7.0f);
    const float moodNow = innerLife ? std::clamp ((feeling - feelingHabit) / 0.08f, -1.0f, 1.0f) : 0.0f;
    const float leanSoft = std::clamp ((lean - leanHabit) / 8.0f, -1.0f, 1.0f);
    const float placeNow = std::clamp (rub / 40.0f, -1.2f, 1.2f) + (innerLife ? 0.8f * moodNow - 1.2f * fear : 0.0f);
    const float bandWish = taste.scratch * gmNow * std::sqrt (gmNow) * (innerLife ? (1.0f - 0.6f * fatigue) * (0.75f + 0.4f * moodNow) : 1.0f);
    if (musical)
    {
        // The song's grid, in 16th notes. Its neurons say what and how hard; the move itself waits
        // for the next line (a 16th when it is wide awake, an 8th otherwise; a big one for an 8th).
        const double pos = beatNow * 4.0;
        const long long tickNow = (long long) std::floor (pos);
        const bool tick = tickNow != lastTick;
        lastTick = tickNow;
        const float per16 = dt * (float) (bpmNow / 60.0 * 4.0);
        const bool moving = legAt < legCount;
        // between moves it leaves the music alone: about a bar when half awake, a beat when wide awake;
        // and over eight bars it moves for at most about a fifth of the time (a third when wide awake),
        // which is where Hakan's yes and no fell (round 2.4: 18% on the groove yes, 32% on the keys no)
        sinceMove16 = moving ? 0.0f : sinceMove16 + per16;
        busyShare += ((moving ? 1.0f : 0.0f) - busyShare) * std::min (1.0f, per16 / (32.0f * (float) barNow));
        // On bare material (chords with rests, no pulse to lean on) every move is naked, so it moves
        // far less and more gently; inside a groove it can dance. The pulse is how many 16ths of the
        // last two bars had a hit in the top of the spectrum (its high looming eyes).
        slotHit = slotHit || std::max (drive[10], drive[11]) > 1.0f;
        const float exposed = 1.0f - clamp01 ((pulse - 0.2f) / 0.2f);
        const float rest16 = 4.0f * (6.0f - 5.0f * awakeNow) * (1.0f + 4.0f * exposed);
        const float budget = (0.08f + 0.24f * awakeNow) * (1.0f - 0.6f * exposed);
        if (wantsJump && ! armed && ! moving && sinceMove16 >= rest16 && busyShare < budget)
        {
            armed = true;
            armedSize = std::min (1.0f, escape / 80.0f * (innerLife ? 0.7f + 0.6f * arousal : 1.0f));
            armedFear = innerLife ? fear : 1.0f;
            sinceJump = 0.0f;
        }
        if (tick)
        {
            pulse += ((slotHit ? 1.0f : 0.0f) - pulse) / 32.0f;
            slotHit = false;
            // small moves on a 16th (wide awake) or an 8th; big ones wait for a bar line (or the half bar)
            const int grid = awakeNow > 0.75f ? 1 : 2;
            const bool big = armedSize * taste.lurch / 0.4f > 0.5f;
            const long long bar16 = std::max (4LL, std::llround (4.0 * barNow));
            const long long bigGrid = awakeNow > 0.75f ? std::max (4LL, bar16 / 2) : bar16;
            if (armed && tickNow % (big ? bigGrid : grid) == 0)
            {
                armed = false;
                startMove (armedSize, armedFear);
            }
            // the scratch, the lean and the feed change on the lines and hold between them
            heldBand = bandWish > 0.12f + 0.4f * exposed ? bandWish : 0.0f; // on bare material only its strongest scratching
            heldBandHz = std::clamp (emptyHz * std::pow (2.0f, placeNow), 150.0f, 7000.0f);
            if (tickNow % 2 == 0)
                heldLean = taste.lean * leanSoft;
            if (tickNow % 4 == 0)
            {
                heldFeed = clamp01 (excited / 4.0f) * 0.18f - 0.05f;
                if (freezeIn16 >= 0.0f && freeze16 <= 0.0f)
                {
                    freeze16 = freezeIn16;
                    freezeIn16 = -1.0f;
                }
            }
        }
        // the tape: the heads slide in straight lines, so each slide bends the repeats by one exact
        // interval for exactly its note value (the dotted head by 1 - 3 x slope, the quarter head by 1 - 4 x slope)
        float slope = 0.0f;
        if (legAt < legCount)
        {
            auto& leg = legs[(size_t) legAt];
            slope = leg.slope;
            stretch += leg.slope * per16;
            leg.left -= per16;
            if (leg.left <= 0.0f)
            {
                stretch = leg.end;
                ++legAt;
            }
        }
        else
        {
            stretch = 1.0f;
            moveNow = none;
        }
        const bool throwing = throw16 > 0.0f;
        throw16 -= per16;
        const bool freezing = freeze16 > 0.0f;
        freeze16 -= per16;

        band = heldBand;
        throwGlow += coeff (0.3f, dt) * ((throwing ? 1.0f : 0.0f) - throwGlow);
        target.speed = 1.0f / std::clamp (stretch, 0.45f, 2.2f);
        target.feed = heldFeed;
        target.space = heldLean;
        target.band = band;
        target.bandHz = heldBandHz;
        target.freeze += coeff (0.02f, dt) * ((freezing ? 1.0f : 0.0f) - target.freeze);
        target.throwing = throwing;
        target.lift = 1.0f + 0.6f * band + 0.8f * throwGlow;

        busyNow = legAt < legCount || band > (busyNow ? 0.08f : 0.3f);
        now.jump = legAt < legCount ? moveSize : 0.0f;
        now.semitones = std::abs (slope) > 1.0e-6f ? 12.0f * std::log2 (std::max (0.05f, 1.0f - 3.0f * slope)) : 0.0f;
        now.throwing = throwing;
        now.frozen = target.freeze > 0.5f;
        now.scratch = band;
        now.scratchHz = heldBandHz;
        now.turn = heldLean / std::max (0.05f, taste.lean);
        now.arousal = arousal;
        now.fatigue = fatigue;
        now.memory = memory;
        now.feeling = feeling;
        now.fear = fear;
        now.move = moveNow;
        return;
    }
    if (wantsJump)
    {
        sinceJump = 0.0f;
        lurchSize = std::min (1.0f, escape / 80.0f * (innerLife ? 0.7f + 0.6f * arousal : 1.0f));
        const float f = innerLife ? fear : 1.0f;
        lurchSign = innerLife && uniform() > f ? 1.0f : -1.0f; // the more of it once hurt, the likelier a dive
        lurchSize *= innerLife ? (lurchSign > 0.0f ? 0.6f : 0.5f + 0.5f * f) : 1.0f;
        lurchLength = innerLife ? 0.5f + 0.9f * f : 0.9f;
        lurchLeft = lurchLength;
        throwLeft = 0.12f + 0.2f * lurchSize + (innerLife ? 0.3f * f : 0.0f);
        if (lurchSize > 0.7f || (innerLife && f > 0.6f && lurchSize > 0.4f))
        {
            freezeFrom = 0.1f;
            freezeLeft = 1.1f;
        }
        if (innerLife)
            arousal += 0.12f * lurchSize * (1.0f - arousal);
    }
    float lurch = 0.0f;
    if (lurchLeft > 0.0f)
    {
        lurch = lurchSign * lurchSize * std::sin (pi * (lurchLength - lurchLeft) / lurchLength);
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
    // what its MBONs say now against lately: come closer, it stays and grooms more; keep away, less
    const float mood = innerLife ? std::clamp ((feeling - feelingHabit) / 0.08f, -1.0f, 1.0f) : 0.0f;
    band = taste.scratch * gm * std::sqrt (gm) * (innerLife ? (1.0f - 0.6f * fatigue) * (0.75f + 0.4f * mood) : 1.0f);
    throwGlow += coeff (0.3f, dt) * ((throwing ? 1.0f : 0.0f) - throwGlow);
    const float leanNow = std::clamp ((lean - leanHabit) / 8.0f, -1.0f, 1.0f);

    target.speed = std::pow (2.0f, taste.lurch * lurch);
    target.feed = clamp01 (excited / 4.0f) * 0.18f - 0.05f;
    target.space = taste.lean * leanNow;
    target.band = band;
    // where it scratches: where the music leaves room, moved by its legs' rub, lower when scared,
    // and wandering with what its MBONs say
    const float place = std::clamp (rub / 40.0f, -1.2f, 1.2f) + (innerLife ? 0.8f * mood - 1.2f * fear : 0.0f);
    target.bandHz = std::clamp (emptyHz * std::pow (2.0f, place), 150.0f, 7000.0f);
    target.freeze += coeff (0.05f, dt) * ((freezing ? 1.0f : 0.0f) - target.freeze);
    target.throwing = throwing;
    target.lift = 1.0f + 0.6f * band + 0.8f * throwGlow;

    busyNow = lurchLeft > 0.0f || band > (busyNow ? 0.08f : 0.3f);
    now.jump = lurchLeft > 0.0f ? lurchSize : 0.0f;
    now.semitones = 12.0f * taste.lurch * lurch;
    now.throwing = throwing;
    now.frozen = target.freeze > 0.5f;
    now.scratch = band;
    now.scratchHz = target.bandHz;
    now.turn = leanNow;
    now.arousal = arousal;
    now.fatigue = fatigue;
    now.memory = memory;
    now.feeling = feeling;
    now.fear = fear;
}
void Fly::scramble (uint64_t seed)
{
    // tools only (allocates): the same strengths, the same signs among them, on random connections
    std::vector<bool> plasticAt ((size_t) flydata::synapses, false);
    for (int p = 0; p < flydata::plasticCount; ++p)
        plasticAt[flydata::plastic[p]] = true;
    std::vector<uint32_t> at;
    at.reserve ((size_t) flydata::synapses);
    for (uint32_t s = 0; s < (uint32_t) flydata::synapses; ++s)
        if (! plasticAt[s])
            at.push_back (s);
    uint64_t x = seed;
    const auto next = [&x] {
        x += 0x9E3779B97F4A7C15ull; // splitmix64
        uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    for (size_t i = at.size() - 1; i > 0; --i)
    {
        const size_t j = (size_t) (next() % (uint64_t) (i + 1));
        std::swap (w[at[i]], w[at[j]]);
    }
}

void Fly::standStep() noexcept
{
    for (int k = 0; k < inputs; ++k)
        standAcc[(size_t) k] += heardNow[(size_t) k];
    if (++standSteps < 20)
        return;
    // every 10 ms: a sub-bin into the ring, then six sliding 50 ms means and the filter
    for (int k = 0; k < inputs; ++k)
        standRing[(size_t) standAt][(size_t) k] = standAcc[(size_t) k] / (float) standSteps;
    standAcc.fill (0.0f);
    standSteps = 0;
    standAt = (standAt + 1) % standSubs;
    for (int o = 0; o < 4; ++o)
        standOut[(size_t) o] = stand->w[(size_t) o][(size_t) (inputs * standLags)];
    for (int lag = 0; lag < standLags; ++lag)
    {
        std::array<float, inputs> mean {};
        for (int s = 0; s < 5; ++s)
        {
            const auto& sub = standRing[(size_t) ((standAt - 1 - 5 * lag - s + 2 * standSubs) % standSubs)];
            for (int k = 0; k < inputs; ++k)
                mean[(size_t) k] += 0.2f * sub[(size_t) k];
        }
        for (int o = 0; o < 4; ++o)
        {
            const auto& wo = stand->w[(size_t) o];
            float sum = 0.0f;
            for (int k = 0; k < inputs; ++k)
                sum += wo[(size_t) (lag * inputs + k)] * mean[(size_t) k];
            standOut[(size_t) o] += sum;
        }
    }
    standOut[0] = std::max (0.0f, standOut[0]);
    standOut[1] = std::max (0.0f, standOut[1]);
    standOut[3] = std::max (0.0f, standOut[3]);
}
} // namespace dendrite
