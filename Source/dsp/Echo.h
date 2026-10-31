// The dendrite echo: a dotted dub echo whose repeats grow into the space around your head.
//
// The trunk is the 0.5 "1a" echo that Hakan heard the dotted magic in: one tape loop,
// a dotted-eighth head that keeps its side and a quarter head that crosses to the other
// side, both feeding back, so every repeat branches in two (3, 4, 6, 7, 8 sixteenths
// and on) and the echo thickens into its own space without a reverb.
//
// The loop runs in Mid/Side. Keeping a side and crossing a side are, in M/S terms,
// "keep S" and "flip S", and neither touches M. So the Mid loop is the plain branching
// echo, and nothing the Side does ever reaches it: the mono sum is the dry plus a clean
// dub echo, whatever the space does. The space lives in the Side:
//
//   pan     every pass writes a little of the Mid into the Side again, so each
//           generation of repeats steps further out from the head, and the crossed
//           branch mirrors it (the dendrite grows)
//   spread  the same, turned 90 degrees by an allpass pair: width without a pan
//   diffuse allpasses in the Side's own loop, so each repeat dissolves further into a
//           space between the ears (compounding pass after pass)
//   beyond  a tiny frequency shift in the Side's loop: the space turns around the
//           head and rises like a barberpole, forever
//
// Around the loop (from 0.5): breathing (ducks under the music, blooms in the gaps,
// swells on its own), filling (feedback rises when the music stops), a ceiling that keeps
// the loop from outgrowing what was played into it, and a deep held throw.
#pragma once

#include "Tools.h"

namespace dendrite
{
class Echo
{
public:
    void prepare (float sampleRate);
    void reset();

    // feed: whisper -> dub -> drown -> endless. space: inside -> around -> envelop -> beyond.
    void setControls (float newFeed, float newSpace, bool throwHeld) noexcept
    {
        feedTarget = clamp01 (newFeed);
        spaceTarget = clamp01 (newSpace);
        held = throwHeld;
    }

    // The itch's third head turns the whole space; the turn heals over about a minute.
    void turnSpace (float radians) noexcept { turnTarget += radians; }

    // Per sample. xm/xs: the input as Mid/Side. im/is: extra signal written straight onto
    // the tape (the itch heads). Returns the echo as Mid/Side in wm/ws.
    void process (float xm, float xs, float im, float is, float& wm, float& ws) noexcept;

    void setTempo (double bpm) noexcept { sixteenth = (float) (sampleRate * 15.0 / std::clamp (bpm, 30.0, 300.0)); }

    // For the window.
    struct View
    {
        float dottedSeconds = 0.375f, quarterSeconds = 0.5f;
        float gainDotted = 0.62f, gainQuarter = 0.42f;
        float pan = 0.0f, spread = 0.0f, diffuse = 0.0f, rotate = 0.0f, theta = 0.0f;
        float returnLevel = 0.0f, fill = 0.0f, throwing = 0.0f, ease = 1.0f, breath = 0.0f;
    };
    View view() const noexcept;

    // An onset worth drawing: set when the input (or a throw) starts a new tree.
    bool takeOnset (float& level, bool& thrown) noexcept
    {
        if (onsetPending <= 0.0f)
            return false;
        level = onsetPending;
        thrown = onsetThrown;
        onsetPending = 0.0f;
        onsetThrown = false;
        return true;
    }

private:
    class TapePass
    {
    public:
        void prepare (float sampleRate, int lane);
        void clear();
        float process (float x, float drive, float diffusion) noexcept;

    private:
        OnePole lp1, lp2, emphasis, hp;
        DcBlocker dc;
        std::array<DriftAllpass, 4> diffusers;
        Wander drift;
        float sr = 48000.0f;
    };

    void updatePaths() noexcept;

    float sampleRate = 48000.0f, sixteenth = 6000.0f;
    Ring loopM, loopS;
    TapePass tapeM, tapeS;
    std::array<DriftAllpass, 5> sideDiffusers;
    PhasePair pairM, pairS;
    OnePole tiltM, tiltS;
    Wander wowA, wowB, flutter, breathWander, thetaWander, sideDrift;
    Dice dice;

    // controls
    float feedTarget = 0.5f, spaceTarget = 0.4f, feed = 0.5f, space = 0.4f;
    bool held = false, wasHeld = false;

    // hidden settings, from the paths (updated every 16 samples)
    float pSend = 0.6f, pDotted = 0.62f, pQuarter = 0.42f, pReturn = 1.6f, pDuck = 0.6f, pCeiling = 0.5f,
          pDrive = 1.16f, pInWidth = 1.0f, pPan = 0.1f, pSpread = 0.1f, pDiffuse = 0.0f, pDiffuseMix = 0.0f, pShift = 0.0f,
          pSideOut = 1.0f, pTrim = 1.0f, pCross = -1.0f;

    // state
    float dottedSm = 0.0f, quarterSm = 0.0f;
    float eFast = 0.0f, eSlow = 0.0f, eDuck = 0.0f, rmsShort = 0.0f, rmsLong = 1.0e-4f, peakLong = 1.0e-3f, outEnvM = 0.0f, outEnvS = 0.0f;
    float lean = 0.0f, fillAmount = 0.0f, heldSend = 0.0f, throwEnv = 0.0f, throwTail = 0.0f;
    float envM = 0.0f, envS = 0.0f, easeNow = 1.0f;
    float launch = 0.0f, launchTarget = 0.0f, turn = 0.0f, turnTarget = 0.0f, rotatePhase = 0.0f;
    float breathNow = 0.0f, levelNow = 0.0f, thetaNow = 0.0f;
    float gainDottedNow = 0.62f, gainQuarterNow = 0.42f;
    float onsetPending = 0.0f, onsetArmed = 1.0f;
    bool onsetThrown = false;
    int64_t silentFor = 0, count = 0;

    // coefficients
    float cFast = 0, rFast = 0, cSlow = 0, aDuck = 0, rDuck = 0, cShort = 0, cLong = 0, cPeak = 0, cLean = 0,
          cFillUp = 0, cFillDown = 0, cControl = 0, cHeldUp = 0, cHeldDown = 0, cThrowDown = 0, cThrowTail = 0, cHead = 0,
          aLoop = 0, rLoop = 0, aOut = 0, rOut = 0, cLaunch = 0, cTurnHeal = 0, cTurn = 0;
};
} // namespace dendrite
