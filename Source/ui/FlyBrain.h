// The fly, seen: its brain from the front (the whole brain as a faint grey shape, every neuron
// that answers music at its own place), each neuron flashing red as it fires and drying to grey.
// Small, it lives where the heads' lamps were. Click it and the window opens a drawer under the
// strip with the brain large, where its senses and reflexes sit, and what it is doing in one line.
#pragma once

#include "../Processor.h"
#include "../dsp/FlyData.h"
#include "Theme.h"

namespace dendrite::ui
{
class FlyBrain : public juce::Component, public juce::TooltipClient
{
public:
    explicit FlyBrain (DendriteProcessor&);
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override { backdropScaled = {}; }
    void mouseUp (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    void setOpen (bool o) { open = o; repaint(); }
    bool isOpen() const noexcept { return open; }
    std::function<void()> onToggle;
    // first light: the brain glows as the ghost's itch plays
    void setGhostGlow (float g) { ghostGlow = g; }

private:
    juce::Rectangle<float> brainArea() const;
    juce::String doingLine() const;

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    std::array<float, flydata::neurons> fresh {}, dry {};
    juce::Image backdrop, backdropScaled;
    bool open = false, awake = false;
    float ghostGlow = 0.0f, quiet = 1.0f;
};
} // namespace dendrite::ui
