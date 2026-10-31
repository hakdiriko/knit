#include "Itch.h"

namespace hyp
{
namespace
{
    constexpr float bandCentres[] = { 90.0f, 250.0f, 700.0f, 1800.0f, 4500.0f, 10000.0f };

    // A swell that rises over the first `rise` of its life and falls over the rest.
    float swell (float p, float rise)
    {
        if (p <= 0.0f || p >= 1.0f) return 0.0f;
        const float s = p < rise ? std::sin (0.5f * pi * p / rise) : std::cos (0.5f * pi * (p - rise) / (1.0f - rise));
        return s * s;
    }
}

// ------------------------------------------------------------------ listener

void Listener::prepare (double sampleRate)
{
    sr = sampleRate;
    for (size_t b = 0; b < filters.size(); ++b)
        filters[b].bandpass (std::min (bandCentres[b], 0.42f * (float) sr), 1.0f, sr);
    envCoef = std::exp (-1.0f / (0.025f * (float) sr));
    reset();
}

void Listener::reset()
{
    for (auto& f : filters) f.clear();
    energy.fill (0.0f);
    written = 0;
    score = 0.0f;
}

void Listener::push (float mid)
{
    for (size_t b = 0; b < filters.size(); ++b)
    {
        const float y = filters[b].process (mid);
        energy[b] = y * y + envCoef * (energy[b] - y * y);
    }
}

void Listener::step (int stepsPerBar)
{
    auto& now = ring[(size_t) (written % ringFrames)];
    float loudest = -120.0f;
    for (size_t b = 0; b < bands; ++b)
    {
        now[b] = std::max (-80.0f, 10.0f * std::log10 (energy[b] + 1e-12f));
        loudest = std::max (loudest, now[b]);
    }
    ++written;

    // Is this sixteenth the same as the same sixteenth one, two or four bars ago?
    float best = 0.0f;
    for (int bars : { 1, 2, 4 })
    {
        const int lag = bars * stepsPerBar;
        if (lag >= ringFrames || written <= lag) continue;
        const auto& then = ring[(size_t) ((written - 1 - lag) % ringFrames)];
        float diff = 0.0f;
        for (size_t b = 0; b < bands; ++b) diff += std::abs (now[b] - then[b]);
        best = std::max (best, std::exp (-(diff / (float) bands) / 3.5f));
    }
    if (loudest < -62.0f) best = 0.0f;          // silence is not repetition

    score += 0.3f * (best - score);
}

// ------------------------------------------------------------------ heads

void Heads::prepare (double sampleRate)
{
    sr = sampleRate;
    flutter.prepare (sr, 0.45f, 0x81, 0.012f);    // fast, but never a raw step
    scarHeal = std::exp (-1.0f / (60.0f * (float) sr));
    anchorCatch.setTime (1.4f, sr);
    reset();
}

void Heads::reset()
{
    clock = -1.0; end = 0.0;
    lamps.fill (0.0f);
    for (auto* f : { &lowRe, &lowIm, &highRe, &highIm }) f->clear();
    haStart = -1.0; haLength = 0.0;
    scar = 0.0f; spinPhase = 0.0f; anchorCatch.value = 0.0f;
    settled = true;
}

void Heads::fire (float intensity, uint64_t seed, double gapSamples)
{
    dice.reseed (seed);
    strength = clamp01 (intensity);
    const double gap = std::clamp (gapSamples, 0.12 * sr, 4.0 * sr);
    const double second = sr;

    // A tape's heads are fixed; these drift a little, so no two itches line up the same.
    screamWin = { gap * dice.jitter (0.10f), std::clamp (1.6 * gap * dice.jitter (0.2f), 0.4 * second, 8.0 * second) };
    laughWin  = { 2.0 * gap * dice.jitter (0.07f), std::clamp (1.2 * gap * dice.jitter (0.2f), 0.45 * second, 3.0 * second) };
    manipWin  = { 3.0 * gap * dice.jitter (0.05f), std::clamp (3.0 * gap * dice.jitter (0.2f), 1.2 * second, 16.0 * second) };

    screamFrom = 520.0f * std::exp2 (dice.range (0.0f, 0.6f));
    screamRise = dice.range (0.5f, 1.0f);
    screamSide = dice.sign();
    for (auto* f : { &lowRe, &lowIm, &highRe, &highIm }) f->clear();
    screamTick = 0;

    haRate = dice.range (4.8f, 6.0f);
    haSlow = dice.range (0.90f, 0.95f);
    haDecay = dice.range (0.72f, 0.86f);
    haSign = dice.sign();
    haCount = 0;
    nextHa = laughWin.start;
    haStart = -1.0; haLength = 0.0;

    turnTotal = dice.sign() * dice.range (0.5f, 1.5f) * pi * (0.6f + 0.4f * strength);
    spinPeak = dice.sign() * dice.range (0.4f, 2.2f) * (0.5f + 0.5f * strength);
    spinPhase = 0.0f;
    bloom = dice.range (0.4f, 1.0f);
    settled = false;

    end = manipWin.start + manipWin.length;
    clock = 0.0;
}

float Heads::process (float re, float im, HeadMods& mods)
{
    scar *= scarHeal;
    lamps.fill (0.0f);
    float bodyTurn = scar, wholeTurn = 0.0f, screamSig = 0.0f;
    bool holdAnchor = false;

    if (busy())
    {
        const double t = clock;
        clock += 1.0;

        // ---- head 1: scream, a ghost only two ears can hear
        const float ps = screamWin.phase (t);
        const float e = swell (ps, 0.4f);
        if (e > 0.0f)
        {
            if (screamTick-- <= 0)
            {
                screamTick = 15;
                const float f = std::min (screamFrom * std::exp2 (screamRise * clamp01 (ps)), 0.2f * (float) sr);
                lowRe.bandpass (f, 6.0f, sr);  lowIm.copyCoefficients (lowRe);
                highRe.bandpass (2.0f * f, 6.0f, sr); highIm.copyCoefficients (highRe);
            }
            // k > 1 flips the band's phase in the far ear: one ear gets it (k + 1) times
            // louder, the other (k - 1) times, upside down. In mono it is not there at all.
            const float k = e * (2.2f + 2.8f * strength);
            const float a = screamSide * 0.5f * pi + 0.6f * flutter.next();
            const float c = std::cos (a), s = std::sin (a);
            const float low = turn (lowRe.process (re), lowIm.process (im), c, s);
            const float high = turn (highRe.process (re), highIm.process (im), c, -s);   // the octave tears the other way
            screamSig = k * (low + 0.5f * high);
            mods.near *= 1.0f - 0.6f * e;            // the space contracts toward the ghost
            mods.diffuse *= 1.0f - 0.7f * e;
            lamps[0] = e * (0.5f + 0.5f * strength);
        }
        else
        {
            flutter.next();
        }

        // ---- head 2: laugh, the whole image giggles
        if (t >= laughWin.start && t < laughWin.start + laughWin.length && t >= nextHa)
        {
            haStart = t;
            haLength = 0.62 * sr / haRate;
            haAmp = std::pow (haDecay, (float) haCount) * dice.range (0.7f, 1.0f) * (0.5f + 0.5f * strength);
            if (dice.unit() < 0.75f) haSign = -haSign;       // mostly alternating, never a pattern
            nextHa = t + sr / haRate * dice.jitter (0.08f);
            haRate *= haSlow;
            ++haCount;
        }
        if (haStart >= 0.0 && t >= haStart && t < haStart + haLength)
        {
            const float b = swell ((float) ((t - haStart) / haLength), 0.22f) * haAmp;
            wholeTurn += haSign * 0.5f * pi * b;     // a quarter turn: from one ear past the middle
            mods.near *= 1.0f + 0.6f * b;
            lamps[1] = b;
        }

        // ---- head 3: manipulate, the body moves and the attacks stay
        const float pm = manipWin.phase (t);
        if (pm > 0.0f && pm < 1.0f)
        {
            const float hump = std::sin (pi * pm) * std::sin (pi * pm);
            spinPhase += tau * spinPeak * hump / (float) sr;
            bodyTurn += turnTotal * ease (pm) + spinPhase;
            mods.distance *= 1.0f + bloom * hump;
            mods.diffuse *= 1.0f + 0.35f * hump;
            lamps[2] = hump * (0.4f + 0.6f * strength);
            holdAnchor = true;
        }

        if (clock >= end && ! settled)
        {
            // Wherever the body ended up becomes the new place; it heals back slowly.
            scar = wrapAngle (scar + turnTotal + spinPhase);
            spinPhase = 0.0f;
            settled = true;
            bodyTurn = scar;
        }
    }

    // The attacks keep the old place while the body travels, then catch up.
    if (! holdAnchor)
        anchorCatch.value += (1.0f - anchorCatch.coef) * wrapAngle (scar - anchorCatch.value);

    mods.body = bodyTurn + wholeTurn;
    mods.anchor = anchorCatch.value + wholeTurn;
    return screamSig;
}
} // namespace hyp
