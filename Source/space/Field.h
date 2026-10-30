// The field: an artificial space that exists only between the ears.
//
// Everything this class makes is a Side signal built from the Mid. The processor
// outputs L = M + S and R = M - S, so the mono sum is the dry Mid, untouched,
// whatever happens here. The space is made of interaural cues alone:
//
//   near     the Mid's band, a fraction of a millisecond late, turned in phase.
//            The turn trades level cues (ILD) against time cues (IPD). Pointing
//            left or right, both cues agree; in front and behind they disagree,
//            and the image splits by frequency. A few degrees decide which wins.
//   diffuse  the Mid's body through 32 allpass sections spaced one per ERB band
//            (one per critical band of the ear). Flat in magnitude, no echoes, no
//            tail: only phase, scrambled inside each critical band, which the ear
//            hears as width, then envelopment. Narrower poles = farther away.
//
// Attacks skip the diffuser and keep their own angle (the anchor), so drums stay
// sharp and the ear localises by the onset while the body spreads (precedence and
// Franssen effect). Orbit is where you are around your head (one turn = one lap)
// plus a seeded terrain of hidden settings; depth walks through four regimes:
// inside, around, envelop, dissolve. Dissolve rotates the space endlessly, so the
// interleaved bands rise like a barberpole without ever resetting.
#pragma once

#include "Tools.h"

namespace hyp
{
struct HeadMods
{
    float body = 0.0f;      // radians added to the sustained part's angle
    float anchor = 0.0f;    // radians added to the attacks' angle
    float near = 1.0f;      // gain multipliers
    float diffuse = 1.0f;
    float distance = 1.0f;  // >1 narrows the decorrelator's poles: farther, longer
};

class Field
{
public:
    void prepare (double sampleRate);
    void reset();
    void setKnobs (float orbit, float depth) { orbitTarget = orbit; depthTarget = depth; }

    // re/im: the analytic Mid band. attack: 0..1, how much of this moment is an onset.
    float process (float re, float im, float attack, const HeadMods& mods);

    static const char* regimeName (float depth);
    static const char* placeName (float orbit);

private:
    void control (const HeadMods& mods);

    static constexpr int sections = 32;

    double sr = 48000.0;
    float orbitTarget = 0.5f, depthTarget = 0.45f;
    float orbitNow = 0.5f, orbitCoef = 0.0f;           // smoothed the short way round the circle
    Glide depthKnob;

    Biquad airRe, airIm;
    Line nearRe, nearIm;

    struct Section { float c = 0, d = 0; std::array<float, 2> re {}, im {}; float centre = 1000.0f, erb = 100.0f; };
    std::array<Section, sections> poles;

    Terrain tauLand, offsetLand, distanceLand, balanceLand, beatLand, poleLand;
    Wander thetaDrift, tauDrift, levelDrift, poleDrift;

    // control-rate targets, glided per sample
    Glide gNear, gDiffuse, tauSamples, offsetNow, beatNow;
    float nearTarget = 0, diffuseTarget = 0, tauTarget = 2, offsetTarget = 0, beatTarget = 0;
    float fieldPhase = 0.0f, poleDriftNow = 0.0f;
    int tick = 0;
};
} // namespace hyp
