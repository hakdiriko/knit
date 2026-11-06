// Renders dendrite's window to a PNG while audio plays through it, so the look can be
// checked without a host. Local tool, not built in CI.
//   dendrite_snapshot material.wav out.png [feed] [space] [itch] [seconds] [scale] [throwAt]
#include <juce_audio_formats/juce_audio_formats.h>
#include "Processor.h"
#include "Editor.h"

int main (int argc, char** argv)
{
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
    editor->setSize ((int) (DendriteEditor::designWidth * scale), (int) (DendriteEditor::designHeight * scale));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    const auto total = (juce::int64) (seconds * reader->sampleRate);
    double t = 0.0;
    for (juce::int64 pos = 0; pos < total; pos += 512)
    {
        buffer.clear();
        reader->read (&buffer, 0, 512, pos % std::max<juce::int64> (1, reader->lengthInSamples - 512), true, true);
        const double now = (double) pos / reader->sampleRate;
        processor.params.getParameter ("throw")->setValueNotifyingHost (throwAt >= 0.0 && now >= throwAt && now < throwAt + 0.6 ? 1.0f : 0.0f);
        processor.processBlock (buffer, midi);
        if (now - t >= 1.0 / 60.0)
        {
            t = now;
            editor->animateTo (now);
        }
    }
    editor->animateTo (t + 0.001);
    auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    const juce::File out { juce::String (argv[2]) };
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    return png.writeImageToStream (image, stream) ? 0 : 3;
}
