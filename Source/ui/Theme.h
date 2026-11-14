// Colours and type for dendrite's window: monochrome (true black, greys, white) with one
// colour, a dark red.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace dendrite::ui
{
namespace colour
{
    // monochrome: black, greys and white, and one colour, a dark red
    const juce::Colour black { 0xff000000 };   // the ground: true black
    const juce::Colour tissue { 0xff767676 };  // the living strands behind everything
    const juce::Colour line { 0xff1c1c1c };
    const juce::Colour line2 { 0xff363636 };
    const juce::Colour ink { 0xffededed };
    const juce::Colour inkDim { 0xff979797 };
    const juce::Colour inkFaint { 0xff575757 };
    const juce::Colour red { 0xffb3241b };      // the one colour: dark red
    const juce::Colour redText { 0xffcf3a2e };  // the same red, lifted for small type and loud marks on black
} // namespace colour

// dendrite's two faces, embedded so the window looks the same on every machine (both SIL Open
// Font License, see Resources/fonts): Urbanist for words, Fragment Mono for numbers.
struct Fonts
{
    Fonts();
    juce::Typeface::Ptr light, regular, medium, mono;

    juce::Font make (const juce::Typeface::Ptr& face, float height, float tracking = 0.0f) const
    {
        return juce::Font (juce::FontOptions (face).withHeight (height).withKerningFactor (tracking));
    }

    // Small type that stays readable when the window is scaled down: never under 10 px on screen.
    juce::Font small (const juce::Component& c, const juce::Typeface::Ptr& face, float height, float tracking = 0.0f) const
    {
        const float scale = std::max (0.1f, juce::Component::getApproximateScaleFactorForComponent (&c));
        return make (face, std::max (height, 10.0f / scale), tracking);
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
