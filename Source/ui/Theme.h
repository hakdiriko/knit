// Colours and type for dendrite's window: true black, warm ivory light, manganese-ochre
// growth, a cool tint for space and a dark red for the itch.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dendrite::ui
{
namespace colour
{
    const juce::Colour black { 0xff000000 };   // the ground: true black
    const juce::Colour tissue { 0xff8a7058 };  // the living strands behind everything
    const juce::Colour line { 0xff1e1b18 };
    const juce::Colour line2 { 0xff3a342e };
    const juce::Colour ink { 0xffefe8dc };
    const juce::Colour inkDim { 0xff9c9288 };
    const juce::Colour inkFaint { 0xff5a534c };
    const juce::Colour ochre { 0xffd9a35b };
    const juce::Colour ochreHot { 0xfff4cd8c };
    const juce::Colour cool { 0xff8cbccb };
    const juce::Colour red { 0xffb3241b };      // the itch: dark red
    const juce::Colour redText { 0xffcf3a2e };  // the same red, lifted for small type on black
} // namespace colour

// The embedded Inter faces (SIL Open Font License, see Resources/fonts/OFL.txt), so the
// window looks the same on every machine.
struct Fonts
{
    Fonts();
    juce::Typeface::Ptr light, regular, medium;

    juce::Font make (const juce::Typeface::Ptr& face, float height, float tracking = 0.0f) const
    {
        return juce::Font (juce::FontOptions (face).withHeight (height).withKerningFactor (tracking));
    }
};

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

private:
    juce::SharedResourcePointer<Fonts> fonts; // shared by every open window, freed with the last
};

// A soft round glow, drawn once and stamped wherever light is needed.
juce::Image makeGlow (juce::Colour c, int size);
} // namespace dendrite::ui
