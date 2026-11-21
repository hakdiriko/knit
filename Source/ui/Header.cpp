#include "Header.h"
#include "dsp/FlyData.h"

namespace dendrite::ui
{
Header::Header (DendriteProcessor& p) : processor (p)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
}

void Header::paint (juce::Graphics& g)
{
    g.fillAll (tone::black);
    const Hairline line (g);
    const auto font = fonts->at (type::header);
    g.setFont (font);
    auto width = [&] (const juce::String& s) { return juce::GlyphArrangement::getStringWidth (font, s); };
    const float y = 0.0f, h = (float) getHeight() - 1.0f;
    const auto dot = juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "));

    // right: the session clock, then the audio settings as the host prepared them
    const auto& d = processor.display;
    const float sr = d.sampleRate.load (std::memory_order_relaxed);
    const int block = d.blockSize.load (std::memory_order_relaxed);
    const auto clockText = clock (processor.recording.sessionSeconds());
    const auto settings = sr > 0.0f ? juce::String (sr / 1000.0f, 1) + " kHz" + dot + juce::String (block) + " smp" : juce::String ("not prepared");
    float right = (float) getWidth() - 14.0f;
    g.setColour (tone::grey (tone::mid));
    const float sw = width (settings);
    g.drawText (settings, juce::Rectangle<float> (right - sw, y, sw + 2.0f, h), juce::Justification::centredLeft);
    right -= sw + width (dot);
    g.drawText (dot, juce::Rectangle<float> (right, y, width (dot), h), juce::Justification::centredLeft);
    const float cw = width (clockText);
    g.setColour (tone::grey (tone::bright));
    g.drawText (clockText, juce::Rectangle<float> (right - cw, y, cw + 2.0f, h), juce::Justification::centredLeft);
    right -= cw + width (dot);

    // what the rig is doing: the output can be bypassed while the specimen still records, and the
    // specimen can rest (its brain does not step) while the clock runs on
    const bool bypassed = processor.params.getRawParameterValue ("bypass")->load() > 0.5f;
    const bool resting = juce::roundToInt (processor.params.getRawParameterValue ("frames")->load()) == 0;
    const bool silent = sr > 0.0f && d.inLevel.load (std::memory_order_relaxed) < 1.0e-4f;
    juce::String state = resting ? "at rest" : silent ? "no input" : "recording";
    if (bypassed)
        state = "output bypassed" + dot + state;
    g.setColour (tone::grey (tone::mid));
    g.drawText (dot, juce::Rectangle<float> (right, y, width (dot), h), juce::Justification::centredLeft);
    const float stw = width (state);
    g.setColour (tone::grey (bypassed || resting || silent ? tone::bright : tone::mid));
    g.drawText (state, juce::Rectangle<float> (right - stw, y, stw + 2.0f, h), juce::Justification::centredLeft);
    right -= stw + 24.0f;

    // left: the name, then the specimen, as much of it as fits (least needed last)
    float x = 14.0f;
    g.setColour (tone::grey (tone::bright));
    g.drawText ("dendrite", juce::Rectangle<float> (x, y, 80.0f, h), juce::Justification::centredLeft);
    x += width ("dendrite") + 24.0f;
    const juce::String parts[] = { "drosophila melanogaster", flydata::specimenSex, flydata::atlasName };
    juce::String specimen;
    for (const auto& part : parts)
    {
        const auto next = specimen.isEmpty() ? part : specimen + dot + part;
        if (x + width (next) > right)
            break;
        specimen = next;
    }
    g.setColour (tone::grey (tone::mid));
    g.drawText (specimen, juce::Rectangle<float> (x, y, right - x, h), juce::Justification::centredLeft);

    g.setColour (tone::grey (tone::dim));
    line.h (0.0f, (float) getWidth(), (float) getHeight() - 0.5f);
}
} // namespace dendrite::ui
