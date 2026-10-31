#include "Echo.h"

namespace dendrite
{
namespace
{
    // feed: off -> whisper -> dub -> drown -> endless. Each hidden setting turns at its
    // own place, so the regimes blur into each other instead of stepping.
    const Path<6> sendPath { { { { 0.0f, 0.0f }, { 0.08f, 0.2f }, { 0.25f, 0.42f }, { 0.5f, 0.62f }, { 0.75f, 0.78f }, { 1.0f, 0.88f } } } };
    const Path<6> dottedPath { { { { 0.0f, 0.3f }, { 0.25f, 0.48f }, { 0.5f, 0.62f }, { 0.72f, 0.72f }, { 0.88f, 0.8f }, { 1.0f, 0.86f } } } };
    const Path<6> quarterPath { { { { 0.0f, 0.18f }, { 0.25f, 0.32f }, { 0.5f, 0.42f }, { 0.72f, 0.5f }, { 0.88f, 0.57f }, { 1.0f, 0.62f } } } };
    const Path<6> returnPath { { { { 0.0f, 0.0f }, { 0.06f, 0.3f }, { 0.25f, 0.95f }, { 0.5f, 1.6f }, { 0.75f, 1.9f }, { 1.0f, 2.0f } } } };
    const Path<4> duckPath { { { { 0.0f, 0.65f }, { 0.5f, 0.6f }, { 0.75f, 0.45f }, { 1.0f, 0.3f } } } };
    const Path<4> ceilingPath { { { { 0.0f, 0.45f }, { 0.5f, 0.5f }, { 0.8f, 0.62f }, { 1.0f, 0.8f } } } };
    const Path<4> drivePath { { { { 0.0f, 1.1f }, { 0.5f, 1.16f }, { 0.85f, 1.4f }, { 1.0f, 1.9f } } } };

    // space: inside -> around -> envelop -> beyond.
    const Path<3> inWidthPath { { { { 0.0f, 0.3f }, { 0.25f, 1.0f }, { 1.0f, 1.0f } } } };
    const Path<5> panPath { { { { 0.0f, 0.0f }, { 0.1f, 0.08f }, { 0.3f, 0.17f }, { 0.6f, 0.22f }, { 1.0f, 0.26f } } } };
    const Path<5> spreadPath { { { { 0.0f, 0.0f }, { 0.1f, 0.05f }, { 0.3f, 0.16f }, { 0.6f, 0.27f }, { 1.0f, 0.34f } } } };
    const Path<5> diffusePath { { { { 0.0f, 0.0f }, { 0.35f, 0.0f }, { 0.6f, 0.45f }, { 0.8f, 0.62f }, { 1.0f, 0.7f } } } };
    const Path<3> diffuseMixPath { { { { 0.0f, 0.0f }, { 0.3f, 0.0f }, { 0.7f, 1.0f } } } };
    const Path<4> shiftPath { { { { 0.0f, 0.0f }, { 0.74f, 0.0f }, { 0.86f, 0.09f }, { 1.0f, 0.35f } } } };
    const Path<5> sideOutPath { { { { 0.0f, 0.55f }, { 0.25f, 1.0f }, { 0.45f, 1.5f }, { 0.7f, 1.8f }, { 1.0f, 1.85f } } } };
    // How the quarter head treats the Side: -1 crosses it (the 0.5 ping-pong), toward +1 it
    // keeps it, so the Side loop sustains like the Mid and every pass adds to how far out
    // the repeat sits.
    const Path<5> crossPath { { { { 0.0f, -1.0f }, { 0.1f, -0.5f }, { 0.3f, 0.35f }, { 0.6f, 0.8f }, { 1.0f, 0.92f } } } };
    const Path<5> trimPath { { { { 0.0f, 1.0f }, { 0.3f, 0.97f }, { 0.6f, 0.9f }, { 0.8f, 0.78f }, { 1.0f, 0.66f } } } };

    // The tape's warmth, as chosen by ear on the 0.5 sketches: the highs give gently,
    // mild saturation, a little drifting diffusion.
    constexpr float lp1Hz = 5450.0f, lp2Hz = 8500.0f, hpHz = 100.0f, emphasisHz = 2500.0f, emphasisAmount = 0.24f;
    constexpr float tapeDiffusion = 0.38f;
    constexpr float wowSeconds = 0.0007f, flutterSeconds = 0.00007f;
} // namespace

//==============================================================================
void Echo::TapePass::prepare (float newSampleRate, int lane)
{
    sr = newSampleRate;
    lp1.setCutoff (lp1Hz, sr);
    lp2.setCutoff (lp2Hz, sr);
    emphasis.setCutoff (emphasisHz, sr);
    hp.setCutoff (hpHz, sr);
    dc.prepare (sr);
    // irregular lengths, different for each lane
    constexpr float ms[2][4] = { { 4.7f, 7.9f, 11.3f, 16.9f }, { 5.3f, 8.6f, 12.4f, 17.8f } };
    for (size_t k = 0; k < diffusers.size(); ++k)
        diffusers[k].prepare (sr, ms[lane][k]);
    drift.prepare (sr, 6.5f + 0.7f * (float) lane, 0x7a9e + (uint64_t) lane);
}

void Echo::TapePass::clear()
{
    for (auto* p : { &lp1, &lp2, &emphasis, &hp })
        p->clear();
    dc.clear();
    for (auto& d : diffusers)
        d.clear();
    drift.reset();
}

float Echo::TapePass::process (float x, float drive, float diffusion) noexcept
{
    // pre-emphasis, soft saturation, de-emphasis: the highs saturate a little more
    const float hi = x - emphasis.lowpass (x);
    float y = std::tanh (drive * (x + emphasisAmount * hi) + 0.05f) - std::tanh (0.05f);
    y = y / drive - emphasisAmount / 1.4f * hi;

    y = lp2.lowpass (lp1.lowpass (hp.highpass (y)));

    // every pass smears the repeat a little more
    const float mod = drift.next() * 0.0004f * sr;
    for (size_t k = 0; k < diffusers.size(); ++k)
        y = diffusers[k].process (y, diffusion, mod * 0.5f * (float) (k + 1));

    return dc.process (y);
}

//==============================================================================
void Echo::prepare (float newSampleRate)
{
    sampleRate = newSampleRate;
    setTempo (120.0);

    // A quarter at 30 BPM is 2 s; leave room for lean and wow.
    loopM.prepare ((int) (sampleRate * 2.2f));
    loopS.prepare ((int) (sampleRate * 2.2f));
    tapeM.prepare (sampleRate, 0);
    tapeS.prepare (sampleRate, 0); // the same tape: a Side that is a share of the Mid stays that share

    constexpr float sideMs[5] = { 1.13f, 2.31f, 3.67f, 5.53f, 8.29f };
    for (size_t k = 0; k < sideDiffusers.size(); ++k)
        sideDiffusers[k].prepare (sampleRate, sideMs[k]);

    wowA.prepare (sampleRate, 2.6f, 0x51);
    wowB.prepare (sampleRate, 3.3f, 0x52);
    flutter.prepare (sampleRate, 0.17f, 0x53);
    breathWander.prepare (sampleRate, 11.0f, 0x54);
    thetaWander.prepare (sampleRate, 13.0f, 0x55);
    sideDrift.prepare (sampleRate, 7.0f, 0x56);

    cFast = approach (0.001f, sampleRate);
    rFast = approach (0.03f, sampleRate);
    cSlow = approach (0.25f, sampleRate);
    aDuck = approach (0.008f, sampleRate);
    rDuck = approach (0.7f, sampleRate);
    cShort = approach (0.4f, sampleRate);
    cLong = approach (4.0f, sampleRate);
    cPeak = approach (6.0f, sampleRate);
    cLean = approach (0.9f, sampleRate);
    cFillUp = approach (0.6f, sampleRate);
    cFillDown = approach (0.12f, sampleRate);
    cControl = approach (0.04f, sampleRate);
    cHeldUp = approach (0.005f, sampleRate);
    cHeldDown = approach (0.04f, sampleRate);
    cHead = approach (0.25f, sampleRate);
    aLoop = approach (0.01f, sampleRate);
    rLoop = approach (0.3f, sampleRate);
    aOut = approach (0.001f, sampleRate);
    rOut = approach (0.25f, sampleRate);
    cLaunch = approach (0.08f, sampleRate);
    cTurn = approach (0.6f, sampleRate);
    cTurnHeal = approach (45.0f, sampleRate);

    reset();
}

void Echo::reset()
{
    loopM.clear();
    loopS.clear();
    tapeM.clear();
    tapeS.clear();
    for (auto& d : sideDiffusers)
        d.clear();
    pairM.clear();
    pairS.clear();
    tiltM.clear();
    tiltS.clear();
    for (auto* w : { &wowA, &wowB, &flutter, &breathWander, &thetaWander, &sideDrift })
        w->reset();
    dice.reseed (0xd3d217e);

    feed = feedTarget;
    space = spaceTarget;
    updatePaths();
    wasHeld = held;
    dottedSm = quarterSm = 0.0f;
    eFast = eSlow = eDuck = rmsShort = 0.0f;
    rmsLong = 1.0e-4f;
    peakLong = 1.0e-3f;
    outEnvM = outEnvS = 0.0f;
    lean = fillAmount = heldSend = throwEnv = throwTail = 0.0f;
    envM = envS = 0.0f;
    easeNow = 1.0f;
    launch = launchTarget = turn = turnTarget = rotatePhase = 0.0f;
    breathNow = levelNow = thetaNow = 0.0f;
    onsetPending = 0.0f;
    onsetArmed = 1.0f;
    onsetThrown = false;
    silentFor = count = 0;
}

void Echo::updatePaths() noexcept
{
    pSend = sendPath (feed);
    pDotted = dottedPath (feed);
    pQuarter = quarterPath (feed);
    pReturn = returnPath (feed);
    pDuck = duckPath (feed);
    pCeiling = ceilingPath (feed);
    pDrive = drivePath (feed);

    pInWidth = inWidthPath (space);
    pPan = panPath (space);
    pSpread = spreadPath (space);
    pDiffuse = diffusePath (space);
    pDiffuseMix = diffuseMixPath (space);
    pShift = shiftPath (space);
    pSideOut = sideOutPath (space);
    pTrim = trimPath (space);
    pCross = crossPath (space);

    // after a throw, the feedback ride falls back over about two bars
    cThrowDown = approach (std::max (0.3f, 32.0f * sixteenth / sampleRate) * 0.5f, sampleRate);
    cThrowTail = approach (std::max (0.6f, 32.0f * sixteenth / sampleRate), sampleRate); // a closed send's throw rings ~2 bars
}

Echo::View Echo::view() const noexcept
{
    View v;
    v.dottedSeconds = dottedSm / sampleRate;
    v.quarterSeconds = quarterSm / sampleRate;
    v.gainDotted = gainDottedNow * easeNow;
    v.gainQuarter = gainQuarterNow * easeNow;
    v.pan = pPan;
    v.spread = pSpread;
    v.diffuse = pDiffuse;
    v.rotate = pShift;
    v.theta = thetaNow;
    v.returnLevel = levelNow;
    v.fill = fillAmount;
    v.throwing = std::max (heldSend, throwEnv);
    v.ease = easeNow;
    v.breath = breathNow;
    return v;
}

void Echo::process (float xm, float xs, float im, float is, float& wm, float& ws) noexcept
{
    // controls glide; the paths follow every 16 samples
    feed += cControl * (feedTarget - feed);
    space += cControl * (spaceTarget - space);
    if ((count & 15) == 0)
        updatePaths();

    // the heads glide like tape when the tempo changes
    if (dottedSm <= 0.0f)
    {
        dottedSm = 3.0f * sixteenth;
        quarterSm = 4.0f * sixteenth;
    }
    dottedSm += cHead * (3.0f * sixteenth - dottedSm);
    quarterSm += cHead * (4.0f * sixteenth - quarterSm);

    // listening: max(|L|, |R|) = |M| + |S|
    const float m = std::abs (xm) + std::abs (xs);
    eFast += (m > eFast ? cFast : rFast) * (m - eFast);
    eSlow += cSlow * (eFast - eSlow);
    eDuck += (m > eDuck ? aDuck : rDuck) * (m - eDuck);
    rmsShort += cShort * (m * m - rmsShort);
    rmsLong += cLong * (m * m - rmsLong);
    peakLong = std::max (eFast, peakLong * (1.0f - cPeak));
    silentFor = eFast < peakLong * 0.02f ? silentFor + 1 : 0; // 34 dB under recent peaks

    // a new tree for the window: an onset that stands out from the recent level
    if (onsetArmed > 0.0f && eFast > 1.9f * eSlow + 1.0e-4f && eFast > 0.12f * peakLong)
    {
        onsetPending = std::max (onsetPending, clamp01 (eFast / peakLong));
        onsetArmed = 0.0f;
    }
    else if (eFast < 1.25f * eSlow)
    {
        onsetArmed = 1.0f;
    }

    // the throw: the send opens at once; the feedback ride and the launch linger
    heldSend += (held ? cHeldUp : cHeldDown) * ((held ? 1.0f : 0.0f) - heldSend);
    if (held)
    {
        throwEnv += cHeldUp * (1.0f - throwEnv);
        throwTail += cHeldUp * (1.0f - throwTail);
    }
    else
    {
        throwEnv -= cThrowDown * throwEnv;
        throwTail -= cThrowTail * throwTail;
    }
    if (held && ! wasHeld)
    {
        launchTarget = dice.sign() * dice.range (0.7f, 1.5f);
        onsetPending = 1.0f;
        onsetThrown = true;
    }
    wasHeld = held;
    launch += cLaunch * (launchTarget - launch);
    launchTarget -= cTurnHeal * 4.0f * launchTarget; // heals over ~10 s
    turn += cTurn * (turnTarget - turn);
    turnTarget -= cTurnHeal * turnTarget;            // heals over ~a minute
    const float send = std::max (pSend, 1.15f * heldSend);

    // breathing: lean late when the part is busy, and swell on its own
    const float busy = std::clamp (std::sqrt (rmsShort / (rmsLong + 1.0e-9f)) - 0.7f, 0.0f, 1.0f);
    lean += cLean * (busy - lean);
    const float leanSamples = lean * 0.008f * sampleRate;
    const float wobbleA = (wowA.next() * wowSeconds + flutter.next() * flutterSeconds) * sampleRate;
    const float wobbleB = wowB.next() * wowSeconds * sampleRate;
    breathNow = std::clamp (breathWander.next() * 1.4f, -1.0f, 1.0f);

    // filling the silence: an eighth in, four bars at most
    const bool silent = silentFor > (int64_t) (2.0f * sixteenth) && silentFor < (int64_t) (64.0f * sixteenth);
    fillAmount += (silent ? cFillUp : cFillDown) * ((silent ? 1.0f : 0.0f) - fillAmount);

    // a throw rides the feedback up, and with a low feed it lends the loop a dub's worth of it
    gainDottedNow = std::min (0.9f, std::max (pDotted + 0.14f * fillAmount + 0.12f * throwEnv, 0.72f * throwTail));
    gainQuarterNow = std::min (0.66f, std::max (pQuarter + 0.12f * fillAmount + 0.1f * throwEnv, 0.5f * throwTail));

    // the heads
    const float dDelay = dottedSm + leanSamples + wobbleA + 2.0f;
    const float qDelay = quarterSm + 1.3f * leanSamples + wobbleB + 2.0f;
    const float dM = loopM.read (dDelay), qM = loopM.read (qDelay);
    const float dS = loopS.read (dDelay), qS = loopS.read (qDelay);

    // dotted keeps its side, quarter crosses: in M/S, keep S and flip S. With more
    // space the quarter keeps the Side too, so the Side grows with every pass.
    float fbM = gainDottedNow * dM + gainQuarterNow * qM;
    float fbS = gainDottedNow * dS + pCross * gainQuarterNow * qS;

    // The loop may never grow louder than what was played into it: past a share of the
    // recent input peaks the feedback eases off (Mid and Side each on their own, so the
    // Side never moves the Mid).
    const float ceiling = (pCeiling + 0.25f * throwEnv) * peakLong;
    envM += (std::abs (fbM) > envM ? aLoop : rLoop) * (std::abs (fbM) - envM);
    envS += (std::abs (fbS) > envS ? aLoop : rLoop) * (std::abs (fbS) - envS);
    easeNow = envM > ceiling ? ceiling / envM : 1.0f;
    fbM *= easeNow;
    fbS *= envS > 1.3f * ceiling ? 1.3f * ceiling / envS : 1.0f;

    // the Side's own loop: each pass dissolves it further, and beyond, it turns
    const float sideMod = sideDrift.next() * 0.0003f * sampleRate;
    {
        // always running, faded in: a diffuser that switches on would jump the Side in time
        float diffused = fbS;
        for (size_t k = 0; k < sideDiffusers.size(); ++k)
            diffused = sideDiffusers[k].process (diffused, pDiffuse, sideMod * (0.4f + 0.3f * (float) k));
        fbS = lerp (fbS, diffused, pDiffuseMix);
    }
    {
        float re = 0.0f, imag = 0.0f;
        pairS.process (fbS, re, imag);
        if (pShift > 0.0005f)
        {
            rotatePhase = wrapAngle (rotatePhase + tau * pShift / sampleRate);
            const float blend = smooth01 (pShift / 0.04f);
            fbS = lerp (fbS, re * std::cos (rotatePhase) - imag * std::sin (rotatePhase), blend);
        }
    }

    // write the Mid
    const float writeM = tapeM.process (send * xm + fbM + im, pDrive, tapeDiffusion);

    // every pass writes a little of the Mid into the Side again: the repeats step outward
    thetaNow = 1.6f * thetaWander.next() + launch + turn;
    float reM = 0.0f, imM = 0.0f;
    pairM.process (writeM, reM, imM);
    const float breathSpace = 1.0f + 0.15f * breathNow;
    const float pan = (pPan + 0.12f * throwEnv) * breathSpace;
    const float inject = pan * std::sin (thetaNow) * writeM + pSpread * breathSpace * std::cos (thetaNow) * imM;
    const float writeS = tapeS.process (send * pInWidth * xs + fbS + is, pDrive, tapeDiffusion) + inject;

    loopM.push (writeM);
    loopS.push (writeS);

    // ducks under what's playing, blooms in the gaps, swells on its own (-3..+3 dB)
    const float duck = pDuck * (1.0f - 0.8f * throwEnv);
    float level = 1.0f - duck * std::clamp ((toDb (eDuck) - toDb (peakLong) + 26.0f) / 18.0f, 0.0f, 1.0f);
    level *= std::pow (10.0f, 0.15f * breathNow);
    // a throw always comes back, even with feed all the way down: the dub engineer's closed send
    const float returnNow = std::max (pReturn, 1.2f * std::sqrt (throwTail));
    level *= returnNow * (1.0f + 0.2f * fillAmount) * (1.0f + 0.35f * throwEnv);
    levelNow = level;
    if ((count & 15) == 0)
    {
        const float cutoff = 6500.0f * std::pow (2.0f, 0.9f * breathNow); // brighter on the in-breath
        tiltM.setCutoff (cutoff, sampleRate);
        tiltS.setCutoff (cutoff, sampleRate);
    }

    wm = level * tiltM.lowpass (0.8f * dM + 0.55f * qM);
    ws = level * pSideOut * tiltS.lowpass (0.8f * dS + 0.55f * pCross * qS);

    // The echo may be as loud as what was played, not louder: a ceiling under the safety
    // limiter, for the Mid and the Side each on its own (so space never moves the Mid).
    const float wetCeiling = 0.85f * peakLong;
    outEnvM += (std::abs (wm) > outEnvM ? aOut : rOut) * (std::abs (wm) - outEnvM);
    outEnvS += (std::abs (ws) > outEnvS ? aOut : rOut) * (std::abs (ws) - outEnvS);
    if (outEnvM > wetCeiling)
        wm *= wetCeiling / outEnvM;
    if (outEnvS > wetCeiling)
        ws *= wetCeiling / outEnvS;

    // space's loudness trim comes last, so the ceiling above never sees it and the Mid
    // changes with space by level only
    wm *= pTrim;
    ws *= pTrim;
    ++count;
}
} // namespace dendrite
