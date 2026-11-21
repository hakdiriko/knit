// The specimen (3.0): the fly's 11,705 neurons at the places FlyWire mapped them, seen from the
// front, each one a point. A point is as bright as its own firing over the last 300 ms; under
// that, every spike of this session leaves it a little brighter for good (the exposure). A
// neuron that has not fired sits at the floor. Nothing moves unless a neuron fired.
//
// The electrode is the old field's light: where it sits over the brain is feed (up) and space
// (right), the same two parameters. Its tip records the neurons within 25 um of it. Pressing
// anywhere else on the specimen holds the throw (the stimulus), as pressing the dark did.
#pragma once

#include "../Processor.h"
#include "Rig.h"

namespace dendrite::ui
{
class Specimen : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Specimen (DendriteProcessor&);
    ~Specimen() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (double nowSeconds); // the window's clock: a press becomes a throw after 80 ms

    // the brain's box in this component, kept at its true aspect (the atlas's, FlyWire's is 816 x 392.8 um)
    juce::Rectangle<float> brainArea() const noexcept { return brain; }
    static constexpr float widthMicrons = flydata::widthMicrons, heightMicrons = flydata::heightMicrons; // the cut's box
    static constexpr float tipMicrons = 25.0f;                            // the electrode's recording radius

    // what the electrode's tip records, measured at the last paint
    struct Recorded
    {
        float xMicrons = 0.0f, yMicrons = 0.0f; // the tip, from the box's left and bottom edges
        int units = 0;                          // neurons within the radius
        float meanHz = 0.0f;                    // their mean rate (each one's 300 ms trace)
        int busiest = -1;                       // the busiest of them (its index in the cut), and its rate
        float busiestHz = 0.0f;
    };
    const Recorded& recorded() const noexcept { return tip; }
    bool stimulating() const noexcept { return touching; }
    double stimulusSeconds() const noexcept { return touching ? clockNow - touchStarted : 0.0; }

    juce::String getTooltip() override;
    void hoverForTools (juce::Point<float> p) noexcept { hoverAt = p; } // the snapshot tool's hand
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void renderPoints (float px);
    void paintScale (juce::Graphics&, const Hairline&);
    void paintRegions (juce::Graphics&, const Hairline&);
    void paintElectrode (juce::Graphics&, const Hairline&);
    void measureTip();
    int findHover() const;
    void paintHover (juce::Graphics&, const Hairline&);
    juce::Point<float> neuronPoint (int n) const noexcept;
    juce::Point<float> tipPoint() const noexcept;
    float tipRadius() const noexcept { return tipMicrons * brain.getWidth() / widthMicrons; }
    bool nearTip (juce::Point<float> p) const noexcept { return p.getDistanceFrom (tipPoint()) < tipRadius() + 8.0f; }
    void setThrow (bool on);

    DendriteProcessor& processor;
    juce::SharedResourcePointer<Fonts> fonts;
    juce::Rectangle<float> brain;
    juce::Image points; // the neurons, at the screen's own pixels
    Recorded tip;
    int hovered = -1; // the neuron under the hand, at the last paint

    // the parameters, as the window last heard them (the host's value, or the hand's while it drags)
    float feed = DendriteProcessor::defaultFeed, space = DendriteProcessor::defaultSpace;
    bool throwOn = false;
    juce::ParameterAttachment feedLink, spaceLink, throwLink;

    // the hand
    bool hoverTip = false, hoverPlate = false, dragging = false, pending = false, touching = false, fine = false;
    juce::Point<float> grabMouse, grabUnit, pressAt, hoverAt { -1.0f, -1.0f };
    double clockNow = 0.0, pressStarted = 0.0, touchStarted = 0.0;
};
} // namespace dendrite::ui
