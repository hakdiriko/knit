#include "Processor.h"
#include "Editor.h"

const juce::StringArray DendriteProcessor::itchNames { "off",       "1/4 bar",   "1/2 bar", "3/4 bar", "1 bar",  "1.25 bars",
                                                       "1.5 bars",  "1.75 bars", "2 bars",  "3 bars",  "4 bars", "8 bars" };

namespace
{
    // IDs and the state type are what hosts store; they never change once shipped.
    // "viber" is the state type every dendrite build has used since 0.0.
    constexpr int paramVersion = 8;

    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "feed", paramVersion }, "feed", NormalisableRange<float> (0.0f, 1.0f), DendriteProcessor::defaultFeed,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                return DendriteProcessor::feedName (v) + " " + String (roundToInt (v * 100.0f)) + "%"; })));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "space", paramVersion }, "space", NormalisableRange<float> (0.0f, 1.0f), DendriteProcessor::defaultSpace,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                return DendriteProcessor::spaceName (v) + " " + String (roundToInt (v * 100.0f)) + "%"; })));
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { "throw", paramVersion }, "throw", false,
            AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int) { return v ? String ("throwing") : String ("-"); })));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "frames", paramVersion }, "itch",
                                                            DendriteProcessor::itchNames, DendriteProcessor::defaultItch));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", paramVersion }, "bypass", false));
        return layout;
    }
} // namespace

juce::String DendriteProcessor::feedName (float v)
{
    if (v < 0.02f) return "off";
    if (v < 0.3f) return "whisper";
    if (v < 0.62f) return "dub";
    if (v < 0.87f) return "drown";
    return "endless";
}

juce::String DendriteProcessor::spaceName (float v)
{
    if (v < 0.2f) return "inside";
    if (v < 0.5f) return "around";
    if (v < 0.78f) return "envelop";
    return "beyond";
}

DendriteProcessor::DendriteProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "viber", makeLayout())
{
    feedParam = params.getRawParameterValue ("feed");
    spaceParam = params.getRawParameterValue ("space");
    throwParam = params.getRawParameterValue ("throw");
    itchParam = params.getRawParameterValue ("frames");
    bypassParam = params.getRawParameterValue ("bypass");
}

juce::AudioProcessorParameter* DendriteProcessor::getBypassParameter() const { return params.getParameter ("bypass"); }

bool DendriteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // The space lives between two ears, so the output is always stereo.
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void DendriteProcessor::prepareToPlay (double sampleRate, int)
{
    setLatencySamples (0);
    engine.setControls (feedParam->load(), spaceParam->load(), false, (int) itchParam->load(), bypassParam->load() > 0.5f);
    engine.prepare ((float) sampleRate);
}

void DendriteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { run (buffer, false); }

void DendriteProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { run (buffer, true); }

void DendriteProcessor::run (juce::AudioBuffer<float>& buffer, bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inChannels = getTotalNumInputChannels();
    for (int c = inChannels; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
    if (buffer.getNumChannels() < 2 || n == 0)
        return;
    if (inChannels < 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    // The clock: Live's tempo and position while it plays.
    hostTime.playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                hostTime.bpm = juce::jlimit (30.0, 300.0, *bpm);
            if (auto sig = pos->getTimeSignature())
                if (sig->numerator > 0 && sig->denominator > 0)
                    hostTime.barBeats = sig->numerator * 4.0 / sig->denominator;
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition())
                {
                    hostTime.ppq = *ppq;
                    hostTime.playing = true;
                }
        }

    engine.setControls (feedParam->load(), spaceParam->load(), throwParam->load() > 0.5f, (int) itchParam->load(),
                        forceBypass || bypassParam->load() > 0.5f);
    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, hostTime);

    // Never hand a host a NaN or an infinity.
    for (int c = 0; c < 2; ++c)
    {
        const auto* p = buffer.getReadPointer (c);
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (p[i]))
            {
                buffer.clear();
                engine.reset();
                return;
            }
    }
    publish();
}

void DendriteProcessor::publish()
{
    const auto& v = engine.view();
    auto& d = display;
    constexpr auto relaxed = std::memory_order_relaxed;
    d.dotted.store (v.echo.dottedSeconds, relaxed);
    d.quarter.store (v.echo.quarterSeconds, relaxed);
    d.gainDotted.store (v.echo.gainDotted, relaxed);
    d.gainQuarter.store (v.echo.gainQuarter, relaxed);
    d.pan.store (v.echo.pan, relaxed);
    d.spread.store (v.echo.spread, relaxed);
    d.diffuse.store (v.echo.diffuse, relaxed);
    d.rotate.store (v.echo.rotate, relaxed);
    d.theta.store (v.echo.theta, relaxed);
    d.returnLevel.store (v.echo.returnLevel, relaxed);
    d.fill.store (v.echo.fill, relaxed);
    d.throwing.store (v.echo.throwing, relaxed);
    d.breath.store (v.echo.breath, relaxed);
    d.repetition.store (v.repetition, relaxed);
    d.frameProgress.store (v.frameProgress, relaxed);
    d.boredom.store (v.boredom, relaxed);
    for (size_t h = 0; h < d.lamps.size(); ++h)
        d.lamps[h].store (v.lamps[h], relaxed);
    d.correlation.store (v.correlation, relaxed);
    d.inLevel.store (v.inLevel, relaxed);
    d.outLevel.store (v.outLevel, relaxed);
    // hit is held at its highest until the window takes it (d.hit.exchange (0.0f))
    for (float prev = d.hit.load (relaxed); v.hit > prev && ! d.hit.compare_exchange_weak (prev, v.hit, relaxed);)
        ;
    d.body.store (v.body, relaxed);
    d.echoBody.store (v.echoBody, relaxed);
    d.bpm.store ((float) v.bpm, relaxed);
    d.hostPlaying.store (v.hostPlaying, relaxed);
    d.itching.store (v.itching, relaxed);

    float level = 0.0f;
    bool thrown = false;
    if (engine.takeOnset (level, thrown))
    {
        const auto scope = d.fifo.write (1);
        if (scope.blockSize1 > 0)
            d.onsets[(size_t) scope.startIndex1] = { level, thrown };
    }
}

void DendriteProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void DendriteProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (params.state.getType()))
            params.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* DendriteProcessor::createEditor() { return new DendriteEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DendriteProcessor(); }
