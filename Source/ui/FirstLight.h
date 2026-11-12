// first light: the window shows how it works, once, the first time it opens on a computer
// (and again whenever a version adds a lesson). A ghost hand moves a puppet of the light,
// presses the dark, lets the itch fire and pulls an ember out, one deadpan line per step.
// It is silent: the sound and every parameter stay yours. Any click ends it; clicking the
// wordmark plays it again.
#pragma once

#include "Controls.h"
#include "Field.h"
#include "Tape.h"

namespace dendrite::ui
{
class FirstLight : public juce::Component
{
public:
    FirstLight (Field&, ItchStrip&, KeepTape&);
    ~FirstLight() override;

    // Bump when a version adds something first light must teach: it then plays once more.
    static constexpr int lesson = 1;

    // Plays it if this computer hasn't seen this lesson (DENDRITE_FIRST_LIGHT=0 never, =1 always).
    void startIfNew (double now);
    void start (double now);
    void stop();
    bool isRunning() const noexcept { return running; }

    void tick (double now);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool hitTest (int, int) override { return running; }

    static constexpr double length = 24.0;

private:
    struct Line { double from, to; const char* text; };
    juce::Point<float> handAt (double t) const;

    Field& field;
    ItchStrip& strip;
    KeepTape& tape;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::Image glow;

    bool running = false, ending = false;
    double started = 0.0, endedAt = 0.0, now = 0.0, nextHit = 0.0;
    float fade = 0.0f;
    juce::Point<float> lightHome { 0.4f, 0.5f }; // where your light is, in field units
    juce::Point<float> hand;                     // in window units
    bool pressing = false, carrying = false;
    float pressGlow = 0.0f;
};
} // namespace dendrite::ui
