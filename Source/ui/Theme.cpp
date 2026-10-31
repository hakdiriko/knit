#include "Theme.h"
#include "BinaryData.h"

namespace dendrite::ui
{
Fonts::Fonts()
{
    light = juce::Typeface::createSystemTypefaceFor (BinaryData::InterLight_ttf, BinaryData::InterLight_ttfSize);
    regular = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    medium = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::TooltipWindow::backgroundColourId, colour::stone2);
    setColour (juce::TooltipWindow::textColourId, colour::ink);
    setColour (juce::TooltipWindow::outlineColourId, colour::line2);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return f.isBold() ? fonts->medium : fonts->regular;
    return juce::LookAndFeel_V4::getTypefaceForFont (f);
}

void LookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    // three small dots instead of the stock lines
    g.setColour ((isMouseOver || isMouseDragging ? colour::inkDim : colour::inkFaint).withAlpha (0.8f));
    const float s = (float) std::min (w, h);
    for (int k = 0; k < 3; ++k)
        for (int j = 0; j <= k; ++j)
            g.fillEllipse ((float) w - s * 0.25f * (float) (j + 1), (float) h - s * 0.25f * (float) (k - j + 1), 2.0f, 2.0f);
}

juce::Image makeGlow (juce::Colour c, int size)
{
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);
    const float r = 0.5f * (float) size;
    juce::ColourGradient grad (c, r, r, c.withAlpha (0.0f), r + r, r, true);
    grad.addColour (0.18, c.withMultipliedAlpha (0.55f));
    grad.addColour (0.45, c.withMultipliedAlpha (0.16f));
    g.setGradientFill (grad);
    g.fillEllipse (0.0f, 0.0f, (float) size, (float) size);
    return img;
}
} // namespace dendrite::ui
