#include "Field.h"

namespace hyp
{
namespace
{
    // Depth regimes. Boundaries sit at different places for each quantity,
    // so moving between regimes is a blur, never a step.
    // Side gains are relative to the Mid: at 1 the near path alone can put the whole
    // band in one ear. Quiet near the centre of the disc, loud everywhere else.
    const Path<5> nearPath    {{{ { 0.0f, 0.30f }, { 0.20f, 0.75f }, { 0.45f, 1.00f }, { 0.75f, 0.90f }, { 1.0f, 0.65f } }}};
    const Path<5> diffusePath {{{ { 0.0f, 0.06f }, { 0.20f, 0.30f }, { 0.45f, 0.75f }, { 0.75f, 1.15f }, { 1.0f, 1.40f } }}};
    const Path<5> combPath    {{{ { 0.0f, 0.00f }, { 0.15f, 0.25f }, { 0.40f, 0.70f }, { 0.70f, 0.85f }, { 1.0f, 0.50f } }}};
    const Path<5> spreadPath  {{{ { 0.0f, 2.20f }, { 0.30f, 1.50f }, { 0.55f, 0.90f }, { 0.80f, 0.50f }, { 1.0f, 0.32f } }}};  // pole width in ERBs
    const Path<4> tauPath     {{{ { 0.0f, 0.04f }, { 0.40f, 0.12f }, { 0.75f, 0.30f }, { 1.0f, 0.42f } }}};                     // ms
    const Path<4> beatPath    {{{ { 0.0f, 0.0f }, { 0.72f, 0.0f }, { 0.86f, -0.04f }, { 1.0f, -0.30f } }}};                      // Hz; negative rises
    const Path<3> airPath     {{{ { 0.0f, -1.0f }, { 0.5f, -3.0f }, { 1.0f, -6.0f } }}};                                         // dB above 7 kHz

    // Glasberg and Moore: ERB-number scale and the width of one critical band.
    float erbNumber (float hz) { return 21.4f * std::log10 (1.0f + 0.00437f * hz); }
    float erbToHz (float e) { return (std::pow (10.0f, e / 21.4f) - 1.0f) / 0.00437f; }
    float erbWidth (float hz) { return 24.7f * (4.37f * hz * 0.001f + 1.0f); }

    const float lowestErb = erbNumber (150.0f), highestErb = erbNumber (16000.0f);
}

void Field::prepare (double sampleRate)
{
    sr = sampleRate;
    nearRe.prepare ((int) (0.004 * sr));
    nearIm.prepare ((int) (0.004 * sr));
    combRe.prepare ((int) (0.014 * sr));
    combIm.prepare ((int) (0.014 * sr));

    // Many cells per lap: about every 15 to 30 degrees the hidden settings are somewhere else.
    tauLand.build (0x51, 17);
    offsetLand.build (0x52, 13);
    distanceLand.build (0x53, 19);
    balanceLand.build (0x54, 11);
    beatLand.build (0x55, 7);
    poleLand.build (0x56, 23);
    combLand.build (0x57, 24);
    combAngleLand.build (0x58, 15);

    thetaDrift.prepare (sr, 23.0f, 0x61);
    tauDrift.prepare (sr, 17.0f, 0x62);
    levelDrift.prepare (sr, 13.0f, 0x63);
    poleDrift.prepare (sr, 19.0f, 0x64);

    // One pole per critical band, each nudged by a fixed seeded amount so the
    // scramble has no regular pattern.
    Dice d; d.reseed (0x71);
    const float step = (highestErb - lowestErb) / (float) sections;
    for (size_t i = 0; i < poles.size(); ++i)
        poles[i].erb = lowestErb + step * ((float) i + 0.5f + d.range (-0.35f, 0.35f));

    orbitCoef = std::exp (-1.0f / (0.12f * (float) sr));
    depthKnob.setTime (0.25f, sr);
    for (auto* g : { &gNear, &gDiffuse, &gComb, &tauSamples, &offsetNow, &combAngleNow }) g->setTime (0.03f, sr);
    combFadeStep = 1.0f / (0.06f * (float) sr);
    beatNow.setTime (0.2f, sr);
    reset();
}

void Field::reset()
{
    airRe.clear(); airIm.clear();
    nearRe.clear(); nearIm.clear();
    combRe.clear(); combIm.clear();
    for (auto& p : poles) { p.re.fill (0.0f); p.im.fill (0.0f); }
    orbitNow = orbitTarget; depthKnob.value = depthTarget;
    fieldPhase = 0.0f; tick = 0;
    control (HeadMods {});
    // A fresh instance starts in place, without gliding in.
    gNear.value = nearTarget; gDiffuse.value = diffuseTarget; tauSamples.value = tauTarget;
    offsetNow.value = offsetTarget; beatNow.value = beatTarget;
    gComb.value = combTarget; combAngleNow.value = combAngleTarget;
    combFrom = combTo = combDelayTarget; combFade = 1.0f;
    addedPower = nearTarget * nearTarget + diffuseTarget * diffuseTarget + combTarget * combTarget;
}

const char* Field::regimeName (float depth)
{
    if (depth < 0.25f) return "inside";
    if (depth < 0.55f) return "around";
    if (depth < 0.80f) return "envelop";
    return "dissolve";
}

const char* Field::placeName (float orbit)
{
    static const char* names[] = { "front", "front-right", "right", "back-right", "back", "back-left", "left", "front-left" };
    const float degrees = wrapUnit (orbit - 0.5f) * 360.0f;
    return names[(int) std::floor (wrapUnit ((degrees + 22.5f) / 360.0f) * 8.0f) % 8];
}

void Field::control (const HeadMods& mods)
{
    const float o = orbitNow, d = depthKnob.value;

    // The terrain: each hidden setting has its own seeded landscape around the circle.
    const float bal = balanceLand.at (o);
    nearTarget = nearPath (d) * (1.0f + 0.45f * bal);
    diffuseTarget = diffusePath (d) * (1.0f - 0.45f * bal);
    tauTarget = tauPath (d) * 0.001f * (float) sr * std::exp2 (1.2f * tauLand.at (o));
    offsetTarget = pi * offsetLand.at (o);
    beatTarget = beatPath (d) * std::exp2 (0.6f * beatLand.at (o));

    // The comb lives on plateaus: each stretch of the circle has its own delay, from
    // 0.6 ms (bands torn apart) to 12 ms (a room with no walls). Crossing to the next
    // plateau crossfades two read heads, so the delay never bends.
    const float c = combLand.at (o);
    combTarget = combPath (d) * (0.75f + 0.25f * std::abs (c));
    combAngleTarget = pi * combAngleLand.at (o);
    combDelayTarget = std::min (0.012f, 0.0006f * std::exp2 (4.3f * (0.5f + 0.5f * c))) * (float) sr;
    if (combFade >= 1.0f && std::abs (combDelayTarget - combTo) > 0.04f * combTo)
    {
        combFrom = combTo;
        combTo = combDelayTarget;
        combFade = 0.0f;
    }

    // Behind you is a little darker than in front: the pinna shades the back.
    const float behind = 0.5f - 0.5f * std::cos (tau * (o - 0.5f));
    airRe.highShelf (7000.0f, airPath (d) - 3.0f * behind, sr);
    airIm.copyCoefficients (airRe);

    // The decorrelator's poles: width from depth (narrow = far), positions sliding
    // along the terrain as you turn, so turning reshuffles the scramble smoothly.
    const float spread = spreadPath (d) / (std::exp2 (0.6f * distanceLand.at (o)) * mods.distance);
    const float step = (highestErb - lowestErb) / (float) sections;
    const float nyquistGuard = 0.45f * (float) sr;
    for (size_t i = 0; i < poles.size(); ++i)
    {
        auto& p = poles[i];
        const float e = p.erb + step * (0.45f * poleLand.at (o + 0.137f * (float) i) + 0.1f * poleDriftNow);
        p.centre = std::clamp (erbToHz (e), 60.0f, nyquistGuard);
        const float bw = std::max (20.0f, spread * erbWidth (p.centre));
        const float r = std::exp (-pi * bw / (float) sr);
        p.c = -2.0f * r * std::cos (tau * p.centre / (float) sr);
        p.d = r * r;
    }
}

float Field::process (float re, float im, float attack, const HeadMods& mods)
{
    orbitNow = wrapUnit (orbitNow + (1.0f - orbitCoef) * wrapHalf (orbitTarget - orbitNow));
    depthKnob.step (depthTarget);
    poleDriftNow = poleDrift.next();
    if (tick-- <= 0) { tick = 15; control (mods); }

    const float breath = std::exp2 (0.2f * levelDrift.next());            // about +-1.2 dB, slow
    const float gn = gNear.step (nearTarget) * mods.near * breath * (1.0f - 0.25f * attack);
    const float gd = gDiffuse.step (diffuseTarget) * mods.diffuse / breath;
    const float gc = gComb.step (combTarget) * mods.diffuse * (1.0f - 0.5f * attack);
    const float combAngle = combAngleNow.step (combAngleTarget);
    // Each path carries about the band's power. Only the knobs count: breath, attacks
    // and the heads are left out, so the mono level never moves on its own.
    addedPower = gNear.value * gNear.value + gDiffuse.value * gDiffuse.value + gComb.value * gComb.value;
    const float tauS = std::max (1.2f, tauSamples.step (tauTarget) * (1.0f + 0.06f * tauDrift.next()));
    const float offset = offsetNow.step (offsetTarget);

    fieldPhase += tau * beatNow.step (beatTarget) / (float) sr;
    fieldPhase -= tau * std::floor (fieldPhase / tau);

    // The angle between the Side and the Mid is where you are. 0 = all in the left ear,
    // pi = all in the right, +-pi/2 = both ears equally loud, 90 degrees apart in phase
    // (in front, or behind). Orbit 0.5 is straight ahead.
    const float base = tau * (orbitNow - 0.5f) + 0.5f * pi + 0.3f * thetaDrift.next() + fieldPhase;
    const float bodyAngle = base + mods.body;
    const float anchorAngle = base + mods.anchor;

    const float ar = airRe.process (re), ai = airIm.process (im);

    // near: a fraction of a millisecond late, then turned; attacks keep their own angle
    nearRe.push (ar);
    nearIm.push (ai);
    const float nr = nearRe.read (tauS), ni = nearIm.read (tauS);
    const float body = turn (nr, ni, std::cos (bodyAngle), std::sin (bodyAngle));
    const float anchor = turn (nr, ni, std::cos (anchorAngle), std::sin (anchorAngle));
    const float nearSig = lerp (body, anchor, attack);

    // diffuse: only the body goes in, through one allpass per critical band
    float vr = ar * (1.0f - 0.6f * attack), vi = ai * (1.0f - 0.6f * attack);
    for (auto& p : poles)
    {
        const float yr = p.d * vr + p.re[0];
        p.re[0] = p.c * vr - p.c * yr + p.re[1];
        p.re[1] = vr - p.d * yr;
        vr = yr;
        const float yi = p.d * vi + p.im[0];
        p.im[0] = p.c * vi - p.c * yi + p.im[1];
        p.im[1] = vi - p.d * yi;
        vi = yi;
    }
    const float a = bodyAngle + offset;
    const float diffuseSig = turn (vr, vi, std::cos (a), std::sin (a));

    // comb: the band late by a few milliseconds, two heads crossfading between plateaus
    combRe.push (ar);
    combIm.push (ai);
    float cr = combRe.read (combTo), ci = combIm.read (combTo);
    if (combFade < 1.0f)
    {
        combFade = std::min (1.0f, combFade + combFadeStep);
        const float w = std::sin (0.5f * pi * combFade);
        const float v = std::cos (0.5f * pi * combFade);
        cr = w * cr + v * combRe.read (combFrom);
        ci = w * ci + v * combIm.read (combFrom);
    }
    const float ca = bodyAngle + combAngle;
    const float combSig = turn (cr, ci, std::cos (ca), std::sin (ca));

    return gn * nearSig + gd * diffuseSig + gc * combSig;
}
} // namespace hyp
