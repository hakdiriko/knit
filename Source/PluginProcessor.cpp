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

juce::String decibels (float v, int)
{
    return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
}

float fromPercent (const juce::String& text) { return text.retainCharacters ("0123456789.-").getFloatValue() / 100.0f; }
} // namespace

ViberProcessor::ViberProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "viber", createParameterLayout())
{
    ecologyParam = parameters.getRawParameterValue (ParamIDs::ecology);
    sizeParam = parameters.getRawParameterValue (ParamIDs::size);
    toneParam = parameters.getRawParameterValue (ParamIDs::tone);
    mixParam = parameters.getRawParameterValue (ParamIDs::mix);
    outputParam = parameters.getRawParameterValue (ParamIDs::output);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (ParamIDs::bypass));
    jassert (bypassParam != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout ViberProcessor::createParameterLayout()
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
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));
    return layout;
}

void ViberProcessor::prepareToPlay (double sampleRate, int)
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

void ViberProcessor::reset()
{
    resetDsp();
}

void ViberProcessor::resetDsp()
{
    network.setMacros (ecologyParam->load(), sizeParam->load(), toneParam->load());
    network.reset();
    autoGain.reset();
    limiter.reset();
    mixSm.setCurrentAndTargetValue (mixParam->load());
    trimSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    bypassSm.setCurrentAndTargetValue (bypassParam->get() ? 1.0f : 0.0f);
}

bool ViberProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void ViberProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();

    for (auto ch = numIn; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numIn == 0 || numSamples == 0)
        return;

    network.setMacros (ecologyParam->load(), sizeParam->load(), toneParam->load());
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

        network.process (dryL.data(), dryR.data(), wetL.data(), wetR.data(), n);
        autoGain.process (dryL.data(), dryR.data(), wetL.data(), wetR.data(), n);

        for (int i = 0; i < n; ++i)
        {
            const size_t s = (size_t) i;

            // Equal-power crossfade: perceived loudness stays level across the Mix travel.
            const float m = mixSm.getNextValue() * 0.5f * viber::dsp::pi;
            const float dryGain = std::cos (m), wetGain = std::sin (m);
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

juce::AudioProcessorEditor* ViberProcessor::createEditor()
{
    return new ViberEditor (*this);
}

void ViberProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ViberProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ViberProcessor();
}
