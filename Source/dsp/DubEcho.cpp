#include "DubEcho.h"

namespace dendrite::dsp
{
namespace
{
float coefficient (float sampleRate, float seconds) { return 1.0f - std::exp (-1.0f / (seconds * sampleRate)); }

float toDb (float x) noexcept { return 20.0f * std::log10 (std::max (x, 1.0e-6f)); }

// Roughly unit-variance noise from three uniforms: cheap, and plenty for a wander.
float gaussian (Rng& rng) noexcept { return rng.nextBipolar() + rng.nextBipolar() + rng.nextBipolar(); }

// The tape's warmth, chosen by ear on the 0.5 sketches (Hakan: "a bit too much" at the
// first setting): highs lost gently, mild saturation, wow and flutter about half depth.
constexpr float lp1Hz = 5450.0f, lp2Hz = 8500.0f, hpHz = 100.0f, emphasisHz = 2500.0f;
constexpr float drive = 1.16f, emphasisAmount = 0.24f, diffusion = 0.38f;
constexpr float wowDepth = 0.58f; // of 1.2 ms wow and 0.12 ms flutter
} // namespace

//==============================================================================
void DubEcho::Ring::prepare (int minSize)
{
    int64_t size = 1;
    while (size < minSize + 8)
        size <<= 1;
    buffer.assign ((size_t) size, 0.0f);
    mask = size - 1;
    writeIndex = 0;
}

void DubEcho::Ring::reset()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
}

float DubEcho::Ring::read (float delay) const noexcept
{
    delay = std::clamp (delay, 2.0f, (float) (mask - 4));
    const auto whole = (int) delay;
    const float f = delay - (float) whole;
    const float xm1 = at (whole - 1), x0 = at (whole), x1 = at (whole + 1), x2 = at (whole + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

//==============================================================================
void DubEcho::Wander::prepare (float sampleRate, float baseHz)
{
    for (size_t i = 0; i < state.size(); ++i)
    {
        rate[i] = 1.0f - std::exp (-2.0f * pi * baseHz * std::pow (4.0f, (float) i) / sampleRate);
        kick[i] = std::sqrt (2.0f * rate[i]) / std::pow (2.0f, 0.5f * (float) i);
    }
    // Two poles well above the fastest component: the shape stays, the per-sample
    // random steps go. Without this a delay that follows the wander turns into hiss.
    smooth = 1.0f - std::exp (-2.0f * pi * baseHz * 32.0f / sampleRate);
    reset();
}

void DubEcho::Wander::reset() noexcept
{
    state.fill (0.0f);
    s1 = s2 = 0.0f;
}

float DubEcho::Wander::next (Rng& rng) noexcept
{
    float sum = 0.0f;
    for (size_t i = 0; i < state.size(); ++i)
    {
        state[i] += -rate[i] * state[i] + kick[i] * gaussian (rng);
        sum += state[i];
    }
    s1 += smooth * (0.55f * sum - s1);
    s2 += smooth * (s1 - s2);
    return s2;
}

//==============================================================================
void DubEcho::DriftAllpass::prepare (float sampleRate, float ms, float gain)
{
    line.prepare ((int) (sampleRate * 0.05f));
    base = ms * 0.001f * sampleRate;
    g = gain;
}

float DubEcho::DriftAllpass::process (float x, float modSamples) noexcept
{
    const float delayed = line.read (std::max (2.0f, base + modSamples));
    const float v = x + g * delayed;
    line.push (v);
    return delayed - g * v;
}

//==============================================================================
void DubEcho::TapePass::prepare (float newSampleRate, int side)
{
    sampleRate = newSampleRate;
    lp1.setCutoff (lp1Hz, sampleRate);
    lp2.setCutoff (lp2Hz, sampleRate);
    emphasis.setCutoff (emphasisHz, sampleRate);
    hp.setCutoff (hpHz, sampleRate);
    dc.prepare (sampleRate);

    // irregular lengths, different on each side
    constexpr float ms[2][4] = { { 4.7f, 7.9f, 11.3f, 16.9f }, { 5.3f, 8.6f, 12.4f, 17.8f } };
    for (size_t k = 0; k < diffusers.size(); ++k)
        diffusers[k].prepare (sampleRate, ms[side][k], diffusion);
    drift.prepare (sampleRate, 0.15f + 0.02f * (float) side);
}

void DubEcho::TapePass::reset()
{
    lp1.reset();
    lp2.reset();
    emphasis.reset();
    hp.reset();
    dc.reset();
    for (auto& d : diffusers)
        d.reset();
    drift.reset();
}

float DubEcho::TapePass::process (float x, Rng& rng) noexcept
{
    // pre-emphasis, soft saturation, de-emphasis: the highs saturate a little more
    const float hi = x - emphasis.process (x);
    float y = std::tanh (drive * (x + emphasisAmount * hi) + 0.05f) - std::tanh (0.05f);
    y = y / drive - emphasisAmount / 1.4f * hi;

    y = lp2.process (lp1.process (hp.process (y)));

    // every pass smears the repeat a little more
    const float mod = drift.next (rng) * 0.0004f * sampleRate;
    for (size_t k = 0; k < diffusers.size(); ++k)
        y = diffusers[k].process (y, mod * 0.5f * (float) (k + 1));

    return dc.process (y);
}

//==============================================================================
void DubEcho::prepare (float sampleRate, uint32_t seed)
{
    sr = sampleRate;
    seedValue = seed;

    for (auto& r : loop)
        r.prepare ((int) (sr * 5.0f)); // a quarter at 40 BPM is 1.5 s
    for (auto& r : history)
        r.prepare ((int) (sr * 4.0f));
    for (auto& r : pending)
        r.prepare ((int) (sr * 8.0f));
    for (int c = 0; c < 2; ++c)
    {
        tape[(size_t) c].prepare (sr, c);
        throwTape[(size_t) c].prepare (sr, c);
    }

    wowA.prepare (sr, 0.07f);
    wowB.prepare (sr, 0.09f);
    flutter.prepare (sr, 1.2f);
    breathWander.prepare (sr, 0.04f);

    aFast = coefficient (sr, 0.001f);
    rFast = coefficient (sr, 0.03f);
    aDuck = coefficient (sr, 0.008f);
    rDuck = coefficient (sr, 0.7f);
    cShort = coefficient (sr, 0.4f);
    cLong = coefficient (sr, 4.0f);
    cPeak = coefficient (sr, 6.0f);
    cLean = coefficient (sr, 0.9f);
    cFillUp = coefficient (sr, 0.6f);
    cFillDown = coefficient (sr, 0.12f);
    cAmount = coefficient (sr, 0.05f);
    cHeldUp = coefficient (sr, 0.005f);
    cHeldDown = coefficient (sr, 0.04f);
    cHead = coefficient (sr, 0.25f);
    cGlow = coefficient (sr, 0.6f);
    aLoop = coefficient (sr, 0.01f);
    rLoop = coefficient (sr, 0.3f);

    reset();
}

void DubEcho::reset()
{
    rng.seed (seedValue);
    for (auto* rings : { &loop, &history, &pending })
        for (auto& r : *rings)
            r.reset();
    for (int c = 0; c < 2; ++c)
    {
        tape[(size_t) c].reset();
        throwTape[(size_t) c].reset();
        breathTilt[(size_t) c].reset();
    }
    wowA.reset();
    wowB.reset();
    flutter.reset();
    breathWander.reset();

    amount = amountTarget;
    heldSend = 0.0f;
    dottedSm = quarterSm = 0.0f;
    eFast = eDuck = rmsShort = 0.0f;
    rmsLong = 1.0e-4f;
    peakLong = 1.0e-3f;
    lean = fillAmount = throwGlow = loopEnvelope = 0.0f;
    silentFor = 0;
    inEvent = copying = false;
    eventStart = eventEnd = quietFor = sampleCount = 0;
    lastThrow = -((int64_t) 1 << 40);
    eventPeak = 0.0f;
}

void DubEcho::detectThrow (int64_t now) noexcept
{
    const float sixteenth = dottedSm / 3.0f;
    const float level = toDb (eFast);

    if (! inEvent && level > toDb (peakLong) - 14.0f)
    {
        inEvent = true;
        eventStart = now - (int64_t) (0.004f * sr);
        eventPeak = eFast;
        quietFor = 0;
    }
    if (! inEvent)
        return;

    eventPeak = std::max (eventPeak, eFast);
    if (level < toDb (eventPeak) - 22.0f)
    {
        if (quietFor == 0)
            eventEnd = now;
        ++quietFor;
    }
    else
    {
        quietFor = 0;
    }

    // an accent followed by an eighth of near-silence: a phrase end, a stab before a gap
    if (quietFor <= (int64_t) (2.0f * sixteenth))
        return;
    inEvent = false;

    const bool accent = toDb (eventPeak) > toDb (peakLong) - 6.0f;
    const bool spaced = now - lastThrow > (int64_t) (16.0f * sixteenth); // at most one per bar
    if (! accent || ! spaced || copying || rng.next01() > 0.8f)            // and sometimes it lets one pass
        return;

    lastThrow = now;
    copySource = std::max (eventStart, eventEnd - (int64_t) (4.0f * sixteenth));
    copyLength = eventEnd + (int64_t) (0.03f * sr) - copySource;
    // Land it where the dotted head is still ahead of it; otherwise a beat later, so the
    // echo stays on the grid.
    copyShift = 0;
    while (now - (copySource + copyShift) > (int64_t) dottedSm - 64)
        copyShift += (int64_t) (4.0f * sixteenth);
    copyDone = 0;
    copying = copyLength > 0;
    throwGlow = 1.0f;
}

void DubEcho::copyThrow() noexcept
{
    // 32 samples per sample: fast enough to stay ahead of the heads, without a CPU spike
    const int64_t now = sampleCount;
    const float fadeIn = 0.003f * sr, fadeOut = 0.02f * sr;
    for (int step = 0; step < 32 && copyDone < copyLength; ++step, ++copyDone)
    {
        const int64_t source = copySource + copyDone;
        const int64_t destination = source + copyShift;
        const int backSource = (int) (now + 1 - source);
        const int backDestination = (int) (now - destination);
        const float g = 0.9f * std::min (1.0f, (float) copyDone / fadeIn)
                        * std::min (1.0f, (float) (copyLength - copyDone) / fadeOut);
        for (size_t c = 0; c < 2; ++c)
        {
            const float v = throwTape[c].process (g * history[c].at (backSource), rng);
            if (backDestination >= 1)
                loop[c].addAt (backDestination, v);
            else
                pending[c].slot (destination) += v;
        }
    }
    copying = copyDone < copyLength;
}

void DubEcho::process (const float* inL, const float* inR, float* wetL, float* wetR, int numSamples, double bpm) noexcept
{
    const float sixteenth = (float) (sr * 15.0 / std::clamp (bpm, 40.0, 300.0));
    if (dottedSm <= 0.0f)
    {
        dottedSm = 3.0f * sixteenth;
        quarterSm = 4.0f * sixteenth;
    }
    const float silentAfter = 2.0f * sixteenth, silentUntil = 64.0f * sixteenth; // fill: an eighth in, 4 bars at most

    for (int i = 0; i < numSamples; ++i)
    {
        const float x[2] = { inL[i], inR[i] };
        const float m = 0.5f * (std::abs (x[0]) + std::abs (x[1]));
        history[0].push (x[0]);
        history[1].push (x[1]);

        // the heads glide like tape when the tempo changes
        dottedSm += cHead * (3.0f * sixteenth - dottedSm);
        quarterSm += cHead * (4.0f * sixteenth - quarterSm);
        amount += cAmount * (amountTarget - amount);

        eFast += (m > eFast ? aFast : rFast) * (m - eFast);
        eDuck += (m > eDuck ? aDuck : rDuck) * (m - eDuck);
        rmsShort += cShort * (m * m - rmsShort);
        rmsLong += cLong * (m * m - rmsLong);
        peakLong = std::max (eFast, peakLong * (1.0f - cPeak));
        silentFor = eFast < peakLong * 0.02f ? silentFor + 1 : 0; // 34 dB under recent peaks

        detectThrow (sampleCount);
        if (copying)
            copyThrow();

        // breathing: lean late when the part is busy
        const float busy = std::clamp (std::sqrt (rmsShort / (rmsLong + 1.0e-9f)) - 0.7f, 0.0f, 1.0f);
        lean += cLean * (busy - lean);
        const float leanSamples = lean * 0.010f * sr;
        const float wobbleA = wowDepth * (wowA.next (rng) * 0.0012f + flutter.next (rng) * 0.00012f) * sr;
        const float wobbleB = wowDepth * wowB.next (rng) * 0.0012f * sr;

        // filling the silence: the feedback rises while nothing plays, falls back fast when it returns
        const float fillTarget = (silentFor > (int64_t) silentAfter && silentFor < (int64_t) silentUntil) ? 1.0f : 0.0f;
        fillAmount += (fillTarget > fillAmount ? cFillUp : cFillDown) * (fillTarget - fillAmount);
        const float gainDotted = 0.62f + 0.14f * fillAmount;  // same side
        const float gainQuarter = 0.42f + 0.12f * fillAmount; // crossed to the other side

        // the send: amount sets the constant part, a held throw opens it fully
        heldSend += (heldThrow ? cHeldUp : cHeldDown) * ((heldThrow ? 1.0f : 0.0f) - heldSend);
        const float send = std::max (amount, heldSend);
        if (heldSend > 0.5f)
            throwGlow = 1.0f;

        float dotted[2], quarter[2];
        for (size_t c = 0; c < 2; ++c)
        {
            dotted[c] = loop[c].read (dottedSm + leanSamples + wobbleA + 2.0f);
            quarter[c] = loop[c].read (quarterSm + 1.3f * leanSamples + wobbleB + 2.0f);
        }

        // The loop may never grow louder than what was played into it: past about half the
        // recent input peaks the feedback eases off. Tonal parts would otherwise build up on
        // the frequencies where both heads line up, and the fill would run away.
        float feedbackL = gainDotted * dotted[0] + gainQuarter * quarter[1];
        float feedbackR = gainDotted * dotted[1] + gainQuarter * quarter[0];
        const float loopPeak = std::max (std::abs (feedbackL), std::abs (feedbackR));
        loopEnvelope += (loopPeak > loopEnvelope ? aLoop : rLoop) * (loopPeak - loopEnvelope);
        const float ceiling = 0.5f * peakLong;
        const float ease = loopEnvelope > ceiling ? ceiling / loopEnvelope : 1.0f;
        feedbackL *= ease;
        feedbackR *= ease;

        float wet[2];
        for (size_t c = 0; c < 2; ++c)
        {
            const float feedback = c == 0 ? feedbackL : feedbackR;
            float& late = pending[c].slot (sampleCount);
            loop[c].push (tape[c].process (send * x[c] + feedback, rng) + late);
            late = 0.0f;
            wet[c] = 0.8f * dotted[c] + 0.55f * quarter[1 - c];
        }

        // ducks under what's playing, blooms in the gaps, and breathes on its own (5-10 s)
        float level = 1.0f - 0.6f * std::clamp ((toDb (eDuck) - toDb (peakLong) + 26.0f) / 18.0f, 0.0f, 1.0f);
        const float breath = std::clamp (breathWander.next (rng) * 1.6f, -1.0f, 1.0f);
        level *= std::pow (10.0f, 0.15f * breath);                     // +-3 dB
        level *= 3.2f * amount * (1.0f + 0.2f * fillAmount);           // fills come forward a little
        if ((sampleCount & 15) == 0)
            for (auto& t : breathTilt)
                t.setCutoff (6500.0f * std::pow (2.0f, 0.9f * breath), sr); // brighter on the in-breath

        wetL[i] = level * breathTilt[0].process (wet[0]);
        wetR[i] = level * breathTilt[1].process (wet[1]);

        throwGlow *= 1.0f - cGlow;
        ++sampleCount;
    }
}
} // namespace dendrite::dsp
