// keep in the window: the tape along the top of the strip, the ruler, and the drag into Live.
//
// The tape shows the last minute, a minute ago at the left, now at the right: a red stroke
// where each itch played, grey where you threw, white where it ran away. It shows while the
// mouse is in the strip. Press a stroke (or anywhere: from that bar to the last full bar) and
// a ruler opens above it with what you are taking, its waveform, bars and beats; pull and it
// leaves the window as a WAV.
#pragma once

#include "../Processor.h"
#include "Theme.h"

namespace dendrite::ui
{
// Writes the take to Documents/dendrite/kept/ and hands it to the OS as a file drag.
// Call from mouseDrag. Returns false if nothing could be written.
bool dragOut (dendrite::keep::Keep&, const dendrite::keep::Take&, juce::Component& source, std::function<void()> done = nullptr);

class KeepTape : public juce::Component, public juce::TooltipClient
{
public:
    explicit KeepTape (DendriteProcessor&);

    // reveal: 0..1, how much the strip under it is showing itself (the mouse is in the strip)
    void tick (float reveal);
    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    bool isActive() const noexcept { return hovering || pressed; }

    // a lamp below is held: show its take on the ruler as if the tape were pressed
    void hold (const dendrite::keep::Take& t, const dendrite::keep::Mark* m) { openRuler (t, m); held = true; }
    void release() { held = false; }
    void flashKept() { flash = 1.0f; }

    // first light: show the tape and a ruler over the last itch without a mouse
    void setGhost (bool on, float alpha = 0.0f);

    static constexpr float rulerHeight = 58.0f, bandHeight = 14.0f, left = 30.0f;
    float right() const { return std::max (left + 100.0f, (float) getWidth() - 30.0f); } // 850 at 880 wide: the tape spans the window

private:
    float lineY() const { return rulerHeight + 6.0f; }
    float xOf (int64_t frame) const;
    int64_t frameAt (float x) const;
    const dendrite::keep::Mark* markAt (juce::Point<float>) const;
    void openRuler (const dendrite::keep::Take&, const dendrite::keep::Mark*);
    void paintRuler (juce::Graphics&);

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    float show = 0.0f, reveal = 0.0f, ghostAlpha = 0.0f, flash = 0.0f;
    bool hovering = false, pressed = false, dragged = false, ghost = false, held = false;
    juce::Point<float> mouseAt;
    uint32_t hoverId = 0;
    int64_t written = 0, capacity = 1;

    // the ruler: what a press is about to take
    std::optional<dendrite::keep::Take> take;
    juce::String caption;
    bool takeIsItch = false;
    std::vector<float> peaks; // the take's waveform, max |x| per ruler pixel
    float rulerAlpha = 0.0f;
};
} // namespace dendrite::ui
