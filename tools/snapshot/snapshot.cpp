// Renders dendrite's window to a PNG while audio plays through it, so the look can be
// checked without a host. Local tool, not built in CI.
//   dendrite_snapshot material.wav out.png [feed] [space] [itch] [seconds] [scale] [throwAt]
// Optional environment:
//   DENDRITE_THROW_LEN=0.6             how long the throw is held, in seconds
//   DENDRITE_TOUCH=x,y                 where the throw touches the field (0..1), for the picture
//   DENDRITE_DRAG=t0                   from t0 seconds on, the light is dragged around a loop
//   DENDRITE_HOVER=x,y                 the mouse rests at x,y (0..1) without pressing
//   DENDRITE_SYNAPSES=0                strands never join (for with/without clips)
//   DENDRITE_FRAMES=dir                also write frames at 30 fps (frame_00000.png ...)
//   DENDRITE_FRAMES_FROM=t             ... starting at t seconds
//   DENDRITE_AUDIO=out.wav             also write what came out of the plug-in
//   DENDRITE_FIRST_LIGHT=1             play first light from the start
//   DENDRITE_STRIP_HOVER=t0,t1         the mouse rests in the strip from t0 to t1 (the tape shows)
//   DENDRITE_KEEP_HOLD=t0,t1           the last itch's ember lamp is held from t0 to t1 (the ruler opens)
//   DENDRITE_KEEP_OUT=dir              at the end, write the last finished itch as keep would drag it
#include <juce_audio_formats/juce_audio_formats.h>
#include "Processor.h"
#include "Editor.h"

namespace
{
juce::String env (const char* name)
{
    const char* v = std::getenv (name);
    return v != nullptr ? juce::String (v) : juce::String();
}

bool writePng (juce::Component& c, const juce::File& out, float scale)
{
    auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    return png.writeImageToStream (image, stream);
}
} // namespace

int main (int argc, char** argv)
{
    // first light only when asked for (DENDRITE_FIRST_LIGHT=1), so pictures show the window itself
    setenv ("DENDRITE_FIRST_LIGHT", "0", 0);
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3)
    {
        std::printf ("usage: dendrite_snapshot material.wav out.png [feed] [space] [itch] [seconds] [scale] [throwAt]\n");
        return 1;
    }
    const float feed = argc > 3 ? (float) std::atof (argv[3]) : DendriteProcessor::defaultFeed;
    const float space = argc > 4 ? (float) std::atof (argv[4]) : DendriteProcessor::defaultSpace;
    const int itch = argc > 5 ? std::atoi (argv[5]) : DendriteProcessor::defaultItch;
    const double seconds = argc > 6 ? std::atof (argv[6]) : 6.0;
    const float scale = argc > 7 ? (float) std::atof (argv[7]) : 1.0f;
    const double throwAt = argc > 8 ? std::atof (argv[8]) : -1.0;
    const double throwLen = env ("DENDRITE_THROW_LEN").isNotEmpty() ? env ("DENDRITE_THROW_LEN").getDoubleValue() : 0.6;
    const double dragFrom = env ("DENDRITE_DRAG").isNotEmpty() ? env ("DENDRITE_DRAG").getDoubleValue() : -1.0;
    const juce::File framesDir = env ("DENDRITE_FRAMES").isNotEmpty() ? juce::File (env ("DENDRITE_FRAMES")) : juce::File();
    const double framesFrom = env ("DENDRITE_FRAMES_FROM").getDoubleValue();
    juce::Point<float> touch { -1.0f, -1.0f };
    if (env ("DENDRITE_TOUCH").isNotEmpty())
        touch = { env ("DENDRITE_TOUCH").upToFirstOccurrenceOf (",", false, false).getFloatValue(),
                  env ("DENDRITE_TOUCH").fromFirstOccurrenceOf (",", false, false).getFloatValue() };

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File { juce::String (argv[1]) }));
    if (reader == nullptr)
        return 2;

    DendriteProcessor processor;
    processor.setPlayConfigDetails (2, 2, reader->sampleRate, 512);
    processor.prepareToPlay (reader->sampleRate, 512);
    processor.params.getParameter ("feed")->setValueNotifyingHost (feed);
    processor.params.getParameter ("space")->setValueNotifyingHost (space);
    processor.params.getParameter ("frames")->setValueNotifyingHost (processor.params.getParameter ("frames")->convertTo0to1 ((float) itch));

    std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditor());
    auto* editor = dynamic_cast<DendriteEditor*> (base.get());
    if (env ("DENDRITE_HOVER").isNotEmpty())
    {
        const auto xy = juce::StringArray::fromTokens (env ("DENDRITE_HOVER"), ",", "");
        editor->getField().simulateHover ({ xy[0].getFloatValue(), xy[1].getFloatValue() });
    }
    if (env ("DENDRITE_SYNAPSES") == "0")
        editor->getField().setSynapses (false);
    editor->setSize ((int) (DendriteEditor::designWidth * scale), (int) (DendriteEditor::designHeight * scale));
    if (const auto size = env ("DENDRITE_SIZE"); size.isNotEmpty()) // w,h in pixels: any window shape
        editor->setSize (size.upToFirstOccurrenceOf (",", false, false).getIntValue(), size.fromFirstOccurrenceOf (",", false, false).getIntValue());

    std::unique_ptr<juce::AudioFormatWriter> audioOut;
    if (env ("DENDRITE_AUDIO").isNotEmpty())
    {
        const juce::File f (env ("DENDRITE_AUDIO"));
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = f.createOutputStream();
        if (stream != nullptr)
            audioOut = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (reader->sampleRate)
                                                        .withNumChannels (2).withBitsPerSample (24));
    }
    if (framesDir != juce::File())
        framesDir.createDirectory();

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    const auto total = (juce::int64) (seconds * reader->sampleRate);
    double t = 0.0, nextFrame = framesFrom;
    int frameNumber = 0;
    for (juce::int64 pos = 0; pos < total; pos += 512)
    {
        buffer.clear();
        reader->read (&buffer, 0, 512, pos % std::max<juce::int64> (1, reader->lengthInSamples - 512), true, true);
        const double now = (double) pos / reader->sampleRate;
        const bool throwing = throwAt >= 0.0 && now >= throwAt && now < throwAt + throwLen;
        processor.params.getParameter ("throw")->setValueNotifyingHost (throwing ? 1.0f : 0.0f);
        if (touch.x >= 0.0f)
            editor->getField().simulateTouch (touch, throwing);
        if (dragFrom >= 0.0 && now >= dragFrom)
        {
            // a slow, wandering drag: out to the right and up, then back round
            const float u = (float) (now - dragFrom);
            editor->getField().simulateMove ({ space + 0.28f * std::sin (0.9f * u) + 0.06f * std::sin (2.3f * u),
                                               1.0f - feed - 0.22f * std::sin (0.6f * u) + 0.05f * std::cos (1.9f * u) });
        }
        auto within = [&] (const char* name) {
            const auto v = juce::StringArray::fromTokens (env (name), ",", "");
            return v.size() == 2 && now >= v[0].getDoubleValue() && now < v[1].getDoubleValue();
        };
        if (env ("DENDRITE_STRIP_HOVER").isNotEmpty())
            editor->getItchStrip().simulateHover (within ("DENDRITE_STRIP_HOVER"));
        if (env ("DENDRITE_KEEP_HOLD").isNotEmpty())
            editor->getItchStrip().simulateLampPress (within ("DENDRITE_KEEP_HOLD"));
        processor.processBlock (buffer, midi);
        if (audioOut != nullptr)
            audioOut->writeFromAudioSampleBuffer (buffer, 0, 512);
        if (now - t >= 1.0 / 60.0)
        {
            t = now;
            editor->animateTo (now);
        }
        if (framesDir != juce::File() && now >= nextFrame)
        {
            nextFrame += 1.0 / 30.0;
            writePng (*editor, framesDir.getChildFile (juce::String::formatted ("frame_%05d.png", frameNumber++)), 1.0f);
        }
    }
    editor->animateTo (t + 0.001);
    if (env ("DENDRITE_KEEP_OUT").isNotEmpty())
    {
        processor.keep.collect();
        const auto& d = processor.keep.diary();
        for (auto it = d.rbegin(); it != d.rend(); ++it)
            if (it->kind == dendrite::keep::Kind::itch && it->endSample >= 0)
            {
                if (auto take = processor.keep.takeMark (*it, false))
                    std::printf ("kept %s\n", processor.keep.write (*take, juce::File (env ("DENDRITE_KEEP_OUT"))).getFullPathName().toRawUTF8());
                break;
            }
    }
    if (env ("DENDRITE_TIME").isNotEmpty())
    {
        // how long one frame of the window takes to tick and paint, in software
        juce::Image img (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        const double start = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < 120; ++k)
        {
            editor->animateTo (t + 0.001 + k / 60.0);
            juce::Graphics g (img);
            editor->paintEntireComponent (g, true);
        }
        const double total = (juce::Time::getMillisecondCounterHiRes() - start) / 120.0;
        const double tickStart = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < 120; ++k)
            editor->animateTo (t + 2.001 + k / 60.0);
        std::printf ("%.2f ms per frame (%.2f ms of it simulation)\n", total, (juce::Time::getMillisecondCounterHiRes() - tickStart) / 120.0);
    }
    return writePng (*editor, juce::File { juce::String (argv[2]) }, 2.0f) ? 0 : 3;
}
