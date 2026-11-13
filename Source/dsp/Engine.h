// The whole of dendrite's sound, without JUCE: the echo, the itch and the output chain.
// The plug-in drives it from its parameters; the offline tools render it directly.
#pragma once

#include "Echo.h"
#include "Itch.h"
#include "Creature.h"
#include "Hand.h"
#include "Fly.h"

namespace dendrite
{
struct HostTime
{
    double bpm = 120.0;
    double barBeats = 4.0;   // quarter notes per bar
    bool playing = false;
    double ppq = 0.0;        // position in quarter notes when playing
};

class Engine
{
public:
    // Itch frame choices, in bars. Index 0 is off. frameLabels are the window's short
    // UTF-8 labels; the host sees DendriteProcessor::itchNames.
    static constexpr int frameChoices = 12;
    static constexpr float frameBars[frameChoices] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 3.0f, 4.0f, 8.0f };
    static constexpr const char* frameLabels[frameChoices] = { "off", "\xc2\xbc", "\xc2\xbd", "\xc2\xbe", "1", "1\xc2\xbc",
                                                               "1\xc2\xbd", "1\xc2\xbe", "2", "3", "4", "8" };
    static constexpr int defaultFrame = 10; // 4 bars

    void prepare (float sampleRate);
    void reset();
    void setControls (float feed, float space, bool throwHeld, int itchChoice, bool bypassed) noexcept;

    // time: 0 dotted (dotted 1/8 + 1/4), 1 triplet (1/4 triplet + 1/4), 2 half (dotted 1/4 + 1/2).
    // dry off: only the echo and the heads come out, for a return track.
    void setStyle (int time, bool dry) noexcept
    {
        echo.setTiming (time);
        dryTarget = dry ? 1.0f : 0.0f;
    }

    // In place, stereo. left/right may be the same signal for a mono input.
    void process (float* left, float* right, int numSamples, const HostTime& time) noexcept;

    struct View
    {
        Echo::View echo;
        float repetition = 0.0f, frameProgress = 0.0f, boredom = 0.0f;
        std::array<float, Heads::count> lamps {};
        bool itching = false;
        float correlation = 1.0f, inLevel = 0.0f, outLevel = 0.0f;
        // for the window, 0..1 against the track's own recent peaks: the strongest hit this
        // block, how loud the input is held, how loud the echo is
        float hit = 0.0f, body = 0.0f, echoBody = 0.0f, runaway = 0.0f;
        double bpm = 120.0;
        bool hostPlaying = false;
        // the creature, for the window: its neurons firing (-1..1), how wound up it is, how
        // well it knows this loop
        std::array<float, Creature::neurons> neurons {};
        float arousal = 0.0f, familiarity = 0.0f;
        // the fly: what it is doing (its neurons' spikes go through itchFly().takeSpikes)
        Fly::Doing fly;
        bool flyAwake = false;
    };
    const View& view() const noexcept { return state; }

    bool takeOnset (float& level, bool& thrown) noexcept { return echo.takeOnset (level, thrown); }

    // The itch's own mind. Its life survives prepare() and reset(); the processor saves it.
    Creature& creature() noexcept { return mind; }
    const Creature& creature() const noexcept { return mind; }
    // For A/B renders only: 1.2's dice itch instead of the creature.
    void useDice (bool dice) noexcept { diceItch = dice; }
    // 2.1 sketch: the itch as a motif (the hand moves the echo's own controls) instead of
    // the heads. Off by default until Hakan hears it.
    void useHand (bool on) noexcept { handItch = on; }
    // The fly is the itch (default). Off: the creature fires the heads, as in 2.0a.
    void useFly (bool on) noexcept { flyItch = on; }
    Fly& itchFly() noexcept { return fly; }
    // How awake the fly is for an itch choice: off sleeps, 1/4 bar is wide awake, 4 bars half.
    static float awakeFor (int choice) noexcept
    {
        return choice <= 0 ? 0.0f : clamp01 (1.0f - 0.125f * std::log2 (frameBars[choice] / 0.25f));
    }
    const Hand& itchHand() const noexcept { return hand; }
    // Tools only: something outside (the fly sketch) writes the touch on the echo itself.
    void touchFromOutside (bool on, const Touch& t = {}) noexcept { outsideOn = on; outside = t; }

    // The last itch, for the offline tools: how many so far, what it was, how bored the
    // frame was and the threshold it passed.
    struct ItchRecord
    {
        int count = 0;
        Gesture gesture;
        double ppq = 0.0;
        float intensity = 0.0f, boredom = 0.0f, threshold = 0.72f, familiarity = 0.0f, arousal = 0.0f, brightness = 0.5f, density = 0.0f;
    };
    const ItchRecord& lastItch() const noexcept { return record; }

    // keep: what the heads play (the itch alone, after bypass's fade), written per sample into
    // these, which must hold a whole block. nullptr: not recorded.
    void setItchTap (float* left, float* right) noexcept
    {
        tapLeft = left;
        tapRight = right;
    }

    // For the offline tools: what each head played this sample.
    const Heads& itchHeads() const noexcept { return heads; }

private:
    void tickClock (double beatsPerSample, double frameBeats, double barBeats, int stepsPerBar, double samplesPerBeat) noexcept;

    float sr = 48000.0f;
    Echo echo;
    Listener listener;
    Heads heads;
    Creature mind;
    bool diceItch = false, handItch = false;
    Hand hand;
    bool flyItch = true;
    Fly fly;
    bool outsideOn = false;
    Touch outside;
    uint64_t handGenome = 0;
    float emptyBandHz() const noexcept;
    ItchRecord record;
    float feedNow = 0.5f, spaceNow = 0.4f, stepOnset = 0.0f;
    bool throwWasHeld = false, throwStarted = false;

    float* tapLeft = nullptr;
    float* tapRight = nullptr;

    int itchChoice = defaultFrame;
    float wetTarget = 1.0f, wetNow = 1.0f, wetStep = 0.0f, dryTarget = 1.0f, dryNow = 1.0f;

    // attack detector
    Biquad attackFilter;
    float attackFast = 0.0f, attackSlow = 0.0f, attack = 0.0f;
    float fastUp = 0, fastDown = 0, slowUp = 0, slowDown = 0, attackUp = 0, attackDown = 0;

    // musical clock: Live's while it plays, otherwise free
    double beat = 0.0;
    int64_t lastStep = -1, lastFrame = INT64_MIN;
    float frameSum = 0.0f;
    int frameSteps = 0, boredFrames = 0;
    bool frameStartedIdle = true;
    int64_t quietFor = 1 << 30; // samples since the heads last played

    // output chain
    float limiterEnv = 1.0f, limiterRelease = 0.0f; // limiterEnv: the echo's gain (1 = untouched)
    float corrLR = 0.0f, corrLL = 0.0f, corrRR = 0.0f, inPower = 0.0f, outPower = 0.0f, meterCoef = 0.0f;
    View state;
};
} // namespace dendrite
