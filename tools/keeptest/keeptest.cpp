// keep, tested without a host: plays a loop through dendrite with a fake Live transport,
// throws once, then takes every mark in the diary (and a stretch of the tape) as WAVs and
// checks that each lands on bar lines, is a whole number of bars long, and holds what it should.
//   dendrite_keeptest loop.wav outdir [seconds] [itch choice] [start ppq] [bpm] [block]
#include <juce_audio_formats/juce_audio_formats.h>
#include "Processor.h"

namespace
{
struct FakeLive final : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setTimeSignature (TimeSignature { 4, 4 });
        p.setPpqPosition (ppq);
        p.setIsPlaying (true);
        return p;
    }
};

int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    failures += ok ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3)
    {
        std::printf ("usage: dendrite_keeptest loop.wav outdir [seconds] [itch] [startPpq] [bpm] [block]\n");
        return 1;
    }
    const double seconds = argc > 3 ? std::atof (argv[3]) : 50.0;
    const int itch = argc > 4 ? std::atoi (argv[4]) : 4;
    const double startPpq = argc > 5 ? std::atof (argv[5]) : 2.0;
    const double bpm = argc > 6 ? std::atof (argv[6]) : 120.0;
    const int block = argc > 7 ? std::atoi (argv[7]) : 512;
    const juce::File outDir { juce::String (argv[2]) };
    outDir.deleteRecursively();
    outDir.createDirectory();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File { juce::String (argv[1]) }));
    if (reader == nullptr)
        return 2;
    const double sr = reader->sampleRate;

    DendriteProcessor processor;
    FakeLive live;
    live.bpm = bpm;
    live.ppq = startPpq;
    processor.setPlayHead (&live);
    processor.setPlayConfigDetails (2, 2, sr, block);
    // a smaller block than the host will send: the processor has to cut it into pieces
    processor.prepareToPlay (sr, block / 2);
    processor.params.getParameter ("frames")->setValueNotifyingHost (processor.params.getParameter ("frames")->convertTo0to1 ((float) itch));

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    const auto total = (juce::int64) (seconds * sr);
    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        buffer.clear();
        reader->read (&buffer, 0, block, pos % std::max<juce::int64> (1, reader->lengthInSamples - block), true, true);
        const double now = (double) pos / sr;
        processor.params.getParameter ("throw")->setValueNotifyingHost (now >= 20.0 && now < 21.0 ? 1.0f : 0.0f);
        processor.processBlock (buffer, midi);
        live.ppq += block * bpm / 60.0 / sr;
        if ((pos / block) % 64 == 0)
            processor.keep.collect();
    }
    auto& keep = processor.keep;
    keep.collect();

    const double framesPerBeat = sr * 60.0 / bpm;
    const char* kinds[] = { "itch", "throw", "runaway" };
    std::printf ("diary: %d marks\n", (int) keep.diary().size());
    int itches = 0;
    auto verify = [&] (const dendrite::keep::Take& t, const juce::File& f, const juce::String& label) {
        std::printf ("%s -> %s\n", label.toRawUTF8(), f.getFileName().toRawUTF8());
        check (f.existsAsFile(), "file written");
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        check (r != nullptr, "file reads back");
        if (r == nullptr)
            return;
        // the tape frame of a song position: frame 0 played at startPpq
        const double fromBeat = startPpq + (double) t.from / framesPerBeat;
        const double phase = std::fmod (fromBeat + 1.0e-6, 4.0);
        check (phase < 1.0e-3 || phase > 4.0 - 1.0e-3, juce::String::formatted ("starts on a bar line (beat %.4f)", fromBeat));
        const double expect = t.bars * 4.0 * framesPerBeat;
        check (std::abs ((double) r->lengthInSamples - expect) <= 1.0,
               juce::String::formatted ("%d bars = %lld frames (want %.1f)", t.bars, (long long) r->lengthInSamples, expect));
        check (r->metadataValues[juce::WavAudioFormat::acidTempo].getDoubleValue() == bpm, "ACID tempo " + r->metadataValues[juce::WavAudioFormat::acidTempo]);
        juce::AudioBuffer<float> a (2, (int) r->lengthInSamples);
        r->read (&a, 0, (int) r->lengthInSamples, 0, true, true);
        const float peak = a.getMagnitude (0, a.getNumSamples());
        check (peak > 1.0e-4f, juce::String::formatted ("not silent (peak %.1f dBFS)", juce::Decibels::gainToDecibels (peak)));
        check (peak < 4.0f, "no blow-up");
    };

    for (const auto& m : keep.diary())
    {
        std::printf ("\n%s #%u: %.3f s .. %.3f s, ppq %.3f, heads [%s], frame %d\n", kinds[(int) m.kind], m.id, m.sample / sr,
                     m.endSample / sr, m.ppq, dendrite::keep::headNames (m.headsMask).toRawUTF8(), m.frameChoice);
        check (std::abs (m.ppq - (startPpq + (double) m.sample / framesPerBeat)) < 1.0e-3, "song position matches the transport");
        check (m.endSample > m.sample, "it ended");
        auto t = keep.takeMark (m, false);
        if (! t)
            continue;
        auto f = keep.write (*t, outDir);
        verify (*t, f, "take");
        if (m.kind == dendrite::keep::Kind::itch)
        {
            ++itches;
            check (m.headsMask != 0, "heads recorded");
            // nothing before the itch's first sound in the itch-only file
            std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
            if (r != nullptr)
            {
                const int lead = (int) (m.sample - t->from);
                juce::AudioBuffer<float> a (2, std::max (1, lead));
                r->read (&a, 0, std::max (1, lead), 0, true, true);
                check (lead >= 0 && a.getMagnitude (0, a.getNumSamples()) < 1.0e-6f,
                       juce::String::formatted ("silent for its %.3f s before the first sound", lead / sr));
            }
            check (keep.write (*t, outDir) == f, "the same itch taken twice reuses its file");
        }
    }
    check (itches >= 2, juce::String (itches) + " itches in the diary");

    auto range = keep.takeFrom (keep.written() - (juce::int64) (30.0 * sr));
    if (range)
        verify (*range, keep.write (*range, outDir), "\nthe last 30 s, to the last full bar");

    std::printf ("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
