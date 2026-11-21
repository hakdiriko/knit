#include "Instruments.h"
#include "rig/Recording.h"
#include "rig/Identities.h"
#include "dsp/FlyData.h"

namespace dendrite::ui
{
namespace
{
    // one piece of a readout: a number (bright) or its unit (a step dimmer)
    struct Part
    {
        juce::String text;
        bool unit = false;
    };

    juce::String signedText (float v, int decimals) { return (v >= 0.0f ? "+" : "-") + juce::String (std::abs (v), decimals); }

    const char* moveName (int move)
    {
        static const char* names[] = { "none", "hop", "leap", "duck", "dive", "rush", "hold", "sink" };
        return move >= 0 && move < 8 ? names[move] : "none";
    }
} // namespace

Instruments::Instruments (DendriteProcessor& p, const Specimen& s) : processor (p), specimen (s)
{
    setOpaque (true);
}

void Instruments::paint (juce::Graphics& g)
{
    g.fillAll (tone::black);
    const Hairline line (g);
    g.setColour (tone::grey (tone::dim));
    line.v (0.0f, 0.0f, (float) getHeight());

    const auto& d = processor.display;
    const auto& rec = processor.recording;
    constexpr auto relaxed = std::memory_order_relaxed;
    const auto data = fonts->at (type::data), small = fonts->at (type::small);
    auto textWidth = [] (const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth (f, s); };
    const float x0 = 16.0f, x1 = (float) getWidth() - 14.0f, pitch = 15.0f;
    float y = 6.0f;
    rows.clear();

    auto heading = [&] (const juce::String& name) {
        y += 6.0f;
        g.setFont (small);
        g.setColour (tone::grey (tone::mid));
        const float w = textWidth (small, name);
        g.drawText (name, juce::Rectangle<float> (x0, y, w + 2.0f, 14.0f), juce::Justification::centredLeft);
        g.setColour (tone::grey (tone::dim));
        line.h (x0 + w + 8.0f, x1, y + 7.0f);
        y += 18.0f;
    };
    // a line: its name on the left, its value right-aligned (units a step dimmer), and what hovering it says
    auto row = [&] (const juce::String& label, std::initializer_list<Part> parts, const juce::String& tip, float level = tone::bright) {
        g.setFont (data);
        g.setColour (tone::grey (tone::mid));
        g.drawText (label, juce::Rectangle<float> (x0, y, 90.0f, pitch), juce::Justification::centredLeft);
        float total = 0.0f;
        for (const auto& p : parts)
            total += textWidth (data, p.text);
        float x = x1 - total;
        for (const auto& p : parts)
        {
            const float w = textWidth (data, p.text);
            g.setColour (tone::grey (p.unit ? std::min (level, tone::mid) : level));
            g.drawText (p.text, juce::Rectangle<float> (x, y, w + 2.0f, pitch), juce::Justification::centredLeft);
            x += w;
        }
        rows.push_back ({ y, tip });
        y += pitch;
    };
    // ten minutes of one of the specimen's quantities (0..1), drawn between the name and the value
    auto history = [&] (int quantity) {
        const float left = x0 + 70.0f, right = left + 84.0f, top = y - pitch + 3.0f, bottom = y - 3.0f;
        g.setColour (tone::grey (tone::floor));
        line.h (left, right, bottom);
        const int n = rec.historyLength();
        if (n < 2)
            return;
        juce::Path p;
        for (int k = n - 1; k >= 0; --k) // oldest first; now sits at the right edge
        {
            const float x = right - (right - left) * (float) k / (float) (rig::Recording::historySeconds - 1);
            const float v = bottom - (bottom - top) * juce::jlimit (0.0f, 1.0f, rec.history (quantity, k));
            if (k == n - 1)
                p.startNewSubPath (x, v);
            else
                p.lineTo (x, v);
        }
        g.setColour (tone::grey (tone::mid));
        g.strokePath (p, juce::PathStrokeType (line.width()));
    };

    const bool bypassed = processor.params.getRawParameterValue ("bypass")->load() > 0.5f;
    const int itch = juce::roundToInt (processor.params.getRawParameterValue ("frames")->load());
    const auto dot = utf8 (" \xc2\xb7 ");
    const auto um = utf8 (" \xc2\xb5m");

    heading ("stimulus");
    row ("input", { { decibels (d.inLevel.load (relaxed)) }, { " dBFS", true } }, "the input's level (rms, 300 ms)");
    row ("output", { { decibels (d.outLevel.load (relaxed)) }, { " dBFS", true } }, "the output's level (rms, 300 ms)");
    const bool playing = d.hostPlaying.load (relaxed);
    row ("tempo", { { playing ? juce::String() : juce::String ("stopped  "), true }, { juce::String (d.bpm.load (relaxed), 1) }, { " bpm", true } },
         "the host's tempo; the delay line's heads follow it", playing ? tone::bright : tone::mid);

    heading ("electrode");
    const auto& tip = specimen.recorded();
    row ("tip", { { "x ", true }, { juce::String (juce::roundToInt (tip.xMicrons)) }, { "  y ", true }, { juce::String (juce::roundToInt (tip.yMicrons)) }, { um, true } },
         "the tip's place over the specimen, from the box's left and bottom edges: right is space, up is feed");
    row ("units", { { juce::String (tip.units) }, { " in r 25" + um, true } }, "neurons within the tip's recording radius");
    row ("mean rate", { { juce::String (tip.meanHz, 1) }, { " Hz", true } }, "their mean firing rate over the last 300 ms");
    if (tip.busiest >= 0 && tip.busiestHz > 0.0f)
    {
        namespace id = rig::identities;
        const juce::String type (id::typeName[id::type[tip.busiest]]);
        row ("busiest", { { type.isNotEmpty() ? type : juce::String ("untyped") }, { "  ", true }, { juce::String (tip.busiestHz, 1) }, { " Hz", true } },
             "the busiest neuron under the tip: " + juce::String (flydata::idSource) + " " + juce::String ((juce::int64) id::rootId[tip.busiest]) + ", its cell type and rate");
    }
    else
        row ("busiest", { { "-" } }, tip.units > 0 ? "none of the neurons under the tip is firing" : "no neuron under the tip", tone::mid);

    heading ("delay line");
    const float level = bypassed ? tone::dim : tone::bright;
    const auto timeName = processor.params.getParameter ("time")->getCurrentValueAsText();
    const float ta = d.dotted.load (relaxed), tb = d.quarter.load (relaxed), ga = d.gainDotted.load (relaxed), gb = d.gainQuarter.load (relaxed);
    row ("head a", { { juce::String (ta * 1000.0f, 1) }, { " ms  ", true }, { decibels (ga) }, { " dB", true } },
         timeName + " head: its delay, and its gain on each pass round the loop", level);
    row ("head b", { { juce::String (tb * 1000.0f, 1) }, { " ms  ", true }, { decibels (gb) }, { " dB", true } },
         "quarter head: its delay, and its gain on each pass round the loop", level);
    row ("return", { { decibels (d.returnLevel.load (relaxed)) }, { " dB", true } }, "the echo's return into the output now, with its ducking and breath", level);
    row ("spread", { { juce::String (d.spread.load (relaxed), 2) } }, "set by space: how far each pass round the loop is pushed to the sides, 0 to 0.34", level);
    row ("diffuse", { { juce::String (d.diffuse.load (relaxed), 2) } }, "set by space: how much each pass is smeared, 0 to 0.7", level);
    if (specimen.stimulating())
        row ("throw", { { "held ", true }, { juce::String (specimen.stimulusSeconds(), 2) }, { " s  ", true }, { juce::String (d.throwing.load (relaxed), 2) } },
             "the throw: your press, held this long; its envelope in the loop, 0 to 1", level);
    else
        row ("throw", { { juce::String (d.throwing.load (relaxed), 2) } }, "the throw's envelope in the loop, 0 to 1: a press, the host's automation, or the fly's escape", level);
    row ("afterglow", { { juce::String (d.glow.load (relaxed), 2) } }, "how much of the last itch's afterglow is still sounding, 0 to 1", level);
    row ("correlation", { { signedText (d.correlation.load (relaxed), 2) } },
         "the output's left/right correlation over 300 ms: +1 mono, 0 wide, below 0 out of phase", level);

    heading ("specimen");
    const float awake = Engine::awakeFor (itch);
    const float alive = itch > 0 ? tone::bright : tone::mid;
    if (itch > 0)
        row ("awake", { { juce::String (awake, 2) } }, "how awake the itch selector keeps the fly: 1/4 bar wide awake, 8 bars half asleep");
    else
        row ("awake", { { "at rest" } }, "the itch selector is off: the fly rests and its brain does not step", tone::mid);
    row ("firing", { { grouped ((juce::int64) std::llround (rec.spikesPerSecond())) }, { " spikes/s", true } }, "the whole cut brain's spikes per second, now", alive);
    row ("arousal", { { juce::String (d.flyArousal.load (relaxed), 2) } }, "how wound up it is, 0 to 1; each escape winds it, it calms over about 40 s; the trace is the last ten minutes", alive);
    history (rig::Recording::arousal);
    row ("fatigue", { { juce::String (d.flyFatigue.load (relaxed), 2) } }, "how tired it is, 0 to 1: how busy its brain has been over about two minutes; ten minutes shown", alive);
    history (rig::Recording::fatigue);
    row ("fear", { { juce::String (d.flyFear.load (relaxed), 2) } }, "how much of what it smells now once hurt it, 0 to 1; ten minutes shown", alive);
    history (rig::Recording::fear);
    row ("memory", { { juce::String (d.flyMemory.load (relaxed), 3) } },
         "its 28,761 learning synapses (Kenyon cell to MBON) against their strength at birth; ten minutes shown", alive);
    history (rig::Recording::memory);
    {
        // the traces' time scale, once, under the last of them
        const float left = x0 + 70.0f, right = left + 84.0f;
        g.setFont (small);
        g.setColour (tone::grey (tone::dim));
        g.drawText ("-10 min", juce::Rectangle<float> (left, y - 1.0f, 60.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText ("now", juce::Rectangle<float> (right - 40.0f, y - 1.0f, 40.0f, 12.0f), juce::Justification::centredRight);
        y += 12.0f;
    }
    const float jump = d.flyJump.load (relaxed);
    if (jump > 0.01f)
        row ("escape", { { moveName (d.flyMove.load (relaxed)) }, { "  ", true }, { signedText (d.flySemitones.load (relaxed), 1) }, { " st", true } },
             "the giant fiber fired: the tape's lurch, and its speed now in semitones", tone::live);
    else
        row ("escape", { { "-" } }, "the giant fiber (DNp01) has not fired: no lurch now", tone::mid);
    if (d.flyScratch.load (relaxed) > 0.02f)
        row ("groom", { { juce::String (d.flyScratchHz.load (relaxed) / 1000.0f, 2) }, { " kHz", true } }, "grooming (DNg11): the band it scratches", tone::live);
    else
        row ("groom", { { "-" } }, "grooming (DNg11) is quiet", tone::mid);
    const float turn = d.flyTurn.load (relaxed);
    row ("steer", { { juce::String (std::abs (turn), 2) }, { turn >= 0.0f ? " left" : " right", true } },
         "steering (DNa01, DNa02): how far it leans the space, and which way", alive);
}

juce::String Instruments::getTooltip()
{
    for (const auto& r : rows)
        if (hoverY >= r.y && hoverY < r.y + 15.0f)
            return r.tip;
    return {};
}
} // namespace dendrite::ui
