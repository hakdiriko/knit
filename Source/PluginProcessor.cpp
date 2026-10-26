#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::String percent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

juce::String ecologyText (float v, int)
{
    const char* regime = v < 0.3f ? "colour" : v < 0.6f ? "space" : v < 0.8f ? "ringing" : "life";
    return percent (v, 0) + juce::String::fromUTF8 (" \xc2\xb7 ") + regime;
}

juce::String itchText (float v, int)
{
    const char* regime = v < 0.02f ? "still" : v < 0.35f ? "breathing" : v < 0.6f ? "restless" : v < 0.85f ? "itchy" : "compulsive";
    return percent (v, 0) + juce::String::fromUTF8 (" \xc2\xb7 ") + regime;
}

juce::String decibels (float v, int)
{
    return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
}

float fromPercent (const juce::String& text) { return text.retainCharacters ("0123456789.-").getFloatValue() / 100.0f; }
} // namespace

DendriteProcessor::DendriteProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      // The state's type stays "viber": sets saved before the rename still recall.
      parameters (*this, nullptr, "viber", createParameterLayout())
{
    ecologyParam = parameters.getRawParameterValue (ParamIDs::ecology);
    sizeParam = parameters.getRawParameterValue (ParamIDs::size);
    toneParam = parameters.getRawParameterValue (ParamIDs::tone);
    mixParam = parameters.getRawParameterValue (ParamIDs::mix);
    outputParam = parameters.getRawParameterValue (ParamIDs::output);
    syncParam = parameters.getRawParameterValue (ParamIDs::sync);
    itchParam = parameters.getRawParameterValue (ParamIDs::itch);
    scratchParam = parameters.getRawParameterValue (ParamIDs::scratch);
    jumpParam = parameters.getRawParameterValue (ParamIDs::jump);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (ParamIDs::bypass));
    jassert (bypassParam != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout DendriteProcessor::createParameterLayout()
{
    using Float = juce::AudioParameterFloat;
    using Attributes = juce::AudioParameterFloatAttributes;
    const juce::NormalisableRange<float> unit (0.0f, 1.0f);

    auto macro = [&] (const char* id, const char* name, float defaultValue, auto toText)
    {
        return std::make_unique<Float> (juce::ParameterID { id, 1 }, name, unit, defaultValue,
                                        Attributes().withStringFromValueFunction (toText)
                                                    .withValueFromStringFunction (fromPercent));
    };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (macro (ParamIDs::ecology, "Ecology", 0.42f, ecologyText),
                macro (ParamIDs::size, "Size", 0.5f, percent),
                macro (ParamIDs::tone, "Tone", 0.55f, percent),
                macro (ParamIDs::mix, "Mix", 0.4f, percent),
                std::make_unique<Float> (juce::ParameterID { ParamIDs::output, 1 }, "Output",
                                         juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                         Attributes().withStringFromValueFunction (decibels)
                                                     .withLabel ("dB")),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::bypass, 1 }, "Bypass", false),
                // Added in 0.1.1, so version hint 2 (AU keeps parameter order by it).
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::sync, 2 }, "Sync", false),
                // Added in 0.2.0, version hint 3.
                std::make_unique<Float> (juce::ParameterID { ParamIDs::itch, 3 }, "Itch", unit, 0.45f,
                                         Attributes().withStringFromValueFunction (itchText)
                                                     .withValueFromStringFunction (fromPercent)),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::scratch, 3 }, "Scratch", false),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::jump, 3 }, "Sync jump", false));
    return layout;
}

void DendriteProcessor::prepareToPlay (double sampleRate, int)
{
    const auto sr = (float) sampleRate;

    network.prepare (sampleRate);
    autoGain.prepare (sr);
    limiter.prepare (sr);

    mixSm.reset (sampleRate, 0.03);
    trimSm.reset (sampleRate, 0.03);
    bypassSm.reset (sampleRate, 0.03);

    setLatencySamples (0);
    resetDsp();
}

void DendriteProcessor::reset()
{
    resetDsp();
}

double DendriteProcessor::hostBpm() const
{
    // Real-time safe: getPosition() only copies the host's transport state.
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto bpm = position->getBpm())
                return *bpm;

    return 0.0;
}

void DendriteProcessor::resetDsp()
{
    network.setSync (syncParam->load() >= 0.5f, hostBpm());
    network.setSyncJump (jumpParam->load() >= 0.5f);
    network.setMacros (ecologyParam->load(), sizeParam->load(), toneParam->load());
    network.setItch (itchParam->load(), false);
    network.reset();
    lastScratch = scratchParam->load() >= 0.5f;
    lastKnobs = { ecologyParam->load(), sizeParam->load(), toneParam->load(), mixParam->load(), itchParam->load() };
    autoGain.reset();
    limiter.reset();
    mixSm.setCurrentAndTargetValue (mixParam->load());
    trimSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    bypassSm.setCurrentAndTargetValue (bypassParam->get() ? 1.0f : 0.0f);
}

bool DendriteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void DendriteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();

    for (auto ch = numIn; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numIn == 0 || numSamples == 0)
        return;

    // Transport: tempo for Sync, song position and play state for the itch's phrases.
    dendrite::dsp::Itch::Transport transport;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            transport.bpm = position->getBpm().orFallback (0.0);
            transport.ppq = position->getPpqPosition().orFallback (0.0);
            transport.playing = position->getIsPlaying();
            if (const auto sig = position->getTimeSignature())
                if (sig->numerator > 0 && sig->denominator > 0)
                    transport.barQuarters = 4.0 * sig->numerator / sig->denominator;
        }
    const double bpm = transport.bpm;

    // Moving any knob is novelty: it drains the itch.
    const std::array<float, 5> knobs { ecologyParam->load(), sizeParam->load(), toneParam->load(), mixParam->load(), itchParam->load() };
    bool moving = false;
    for (size_t k = 0; k < knobs.size(); ++k)
        moving = moving || std::abs (knobs[k] - lastKnobs[k]) > 1.0e-4f;
    lastKnobs = knobs;

    const bool scratch = scratchParam->load() >= 0.5f;
    if (scratch != lastScratch)
        network.triggerScratch();
    lastScratch = scratch;

    if (scarsPending.exchange (false, std::memory_order_acquire))
    {
        std::array<float, 6> loaded {};
        for (size_t i = 0; i < loaded.size(); ++i)
            loaded[i] = scarsLoaded[i].load (std::memory_order_relaxed);
        network.getItch().setScars (loaded);
    }

    network.setSync (syncParam->load() >= 0.5f, bpm);
    network.setSyncJump (jumpParam->load() >= 0.5f);
    network.setMacros (ecologyParam->load(), sizeParam->load(), toneParam->load());
    network.setItch (itchParam->load(), moving);
    network.setTransport (transport);
    mixSm.setTargetValue (mixParam->load());
    trimSm.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    bypassSm.setTargetValue (bypassParam->get() ? 1.0f : 0.0f);

    float* left = buffer.getWritePointer (0);
    float* right = numIn > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int start = 0; start < numSamples; start += subBlock)
    {
        const int n = std::min (subBlock, numSamples - start);

        for (int i = 0; i < n; ++i)
        {
            dryL[(size_t) i] = left[start + i];
            dryR[(size_t) i] = right != nullptr ? right[start + i] : dryL[(size_t) i];
        }

        network.process (dryL.data(), dryR.data(), wetL.data(), wetR.data(), dryDuck.data(), n);
        autoGain.setHold (network.wantsGainHold());
        autoGain.process (dryL.data(), dryR.data(), wetL.data(), wetR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const size_t s = (size_t) i;

            // Equal-power crossfade: perceived loudness stays level across the Mix travel.
            const float mix = mixSm.getNextValue();
            const float m = mix * 0.5f * dendrite::dsp::pi;
            // The dub drop ducks the dry. Near Mix 0% there are no echoes to hear in the
            // hole, so it fades out of the way.
            const float duck = 1.0f - (1.0f - dryDuck[s]) * std::min (1.0f, 2.5f * mix);
            const float dryGain = std::cos (m) * duck, wetGain = std::sin (m);
            const float trim = trimSm.getNextValue();

            float l = (dryGain * dryL[s] + wetGain * wetL[s]) * trim;
            float r = (dryGain * dryR[s] + wetGain * wetR[s]) * trim;
            limiter.process (l, r);

            const float b = bypassSm.getNextValue();
            l += b * (dryL[s] - l);
            r += b * (dryR[s] - r);

            if (right != nullptr)
            {
                left[start + i] = l;
                right[start + i] = r;
            }
            else
            {
                left[start + i] = 0.5f * (l + r);
            }
        }
    }

    echoNoteForUi.store (network.getEchoNote(), std::memory_order_relaxed);
    hostHasTempo.store (bpm > 0.0, std::memory_order_relaxed);

    const auto& itch = network.getItch();
    itchLevelForUi.store (itch.getLevel(), std::memory_order_relaxed);
    gestureForUi.store (itch.getActiveGesture(), std::memory_order_relaxed);
    scratchCountForUi.store (itch.getScratchCount(), std::memory_order_relaxed);
    const auto scars = itch.getScars();
    for (size_t i = 0; i < scars.size(); ++i)
        scarsSaved[i].store (scars[i], std::memory_order_relaxed);

    // NaN/Inf guard: never hand a non-finite sample to the host. Silence this block
    // and start the network from scratch.
    for (int ch = 0; ch < numIn; ++ch)
    {
        const float* data = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (data[i]))
            {
                for (int c = 0; c < numIn; ++c)
                    buffer.clear (c, 0, numSamples);
                resetDsp();
                return;
            }
    }
}

juce::AudioProcessorEditor* DendriteProcessor::createEditor()
{
    return new DendriteEditor (*this);
}

void DendriteProcessor::pressScratch()
{
    if (auto* p = parameters.getParameter (ParamIDs::scratch))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() < 0.5f ? 1.0f : 0.0f);
        p->endChangeGesture();
    }
}

void DendriteProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();

    // The scars travel with the set, so a reopened set is the same creature.
    juce::StringArray scars;
    for (auto& v : scarsSaved)
        scars.add (juce::String (v.load (std::memory_order_relaxed), 5));
    state.setProperty ("scars", scars.joinIntoString (" "), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DendriteProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);

            const auto scars = juce::StringArray::fromTokens (state.getProperty ("scars").toString(), " ", "");
            for (size_t i = 0; i < scarsLoaded.size(); ++i)
            {
                const float v = (int) i < scars.size() ? scars[(int) i].getFloatValue() : 0.0f;
                scarsLoaded[i].store (v, std::memory_order_relaxed);
                scarsSaved[i].store (v, std::memory_order_relaxed);
            }
            scarsPending.store (true, std::memory_order_release);

            parameters.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DendriteProcessor();
}
