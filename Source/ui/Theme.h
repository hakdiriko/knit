// Colours and type for dendrite's window: dark stone, warm ivory ink, manganese-ochre
// branches, a cool tint for space and a coral for the itch.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dendrite::ui
{
namespace colour
{
    const juce::Colour stone0 { 0xff0c0b0a }; // the deepest ground
    const juce::Colour stone1 { 0xff141210 }; // panels
    const juce::Colour stone2 { 0xff1d1a17 }; // raised
    const juce::Colour line { 0xff2c2824 };
    const juce::Colour line2 { 0xff403a33 };
    const juce::Colour ink { 0xffefe8dc };
    const juce::Colour inkDim { 0xff9c9288 };
    const juce::Colour inkFaint { 0xff5f5850 };
    const juce::Colour ochre { 0xffd9a35b };
    const juce::Colour ochreHot { 0xfff4cd8c };
    const juce::Colour cool { 0xff8cbccb };
    const juce::Colour coral { 0xffeb6a50 };
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
