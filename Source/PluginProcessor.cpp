#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::String amountText (float v, int)
{
    const auto percent = juce::String (juce::roundToInt (v * 100.0f)) + "%";
    const char* word = v < 0.005f ? "off" : v < 0.3f ? "a whisper" : v < 0.7f ? "dub" : "drowning";
    return percent + juce::String::fromUTF8 (" \xc2\xb7 ") + word;
}

float fromPercent (const juce::String& text) { return text.retainCharacters ("0123456789.-").getFloatValue() / 100.0f; }

constexpr uint32_t seed = 1;
} // namespace

DendriteProcessor::DendriteProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      // The state's type stays "viber": sets saved before the rename still recall.
      parameters (*this, nullptr, "viber", createParameterLayout())
{
    amountParam = parameters.getRawParameterValue (ParamIDs::amount);
    throwParam = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (ParamIDs::throwIt));
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (ParamIDs::bypass));
    jassert (throwParam != nullptr && bypassParam != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout DendriteProcessor::createParameterLayout()
{
    // 0.5.0 replaced every earlier control, so these carry version hint 6. On the window
    // they are a pad and a strip, not knobs; in Live they map to Push, MIDI or an LFO.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ParamIDs::amount, 6 }, "amount",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                             juce::AudioParameterFloatAttributes()
                                                                 .withStringFromValueFunction (amountText)
                                                                 .withValueFromStringFunction (fromPercent)),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::throwIt, 6 }, "throw", false),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::bypass, 1 }, "bypass", false));
    return layout;
}

void DendriteProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = (float) newSampleRate;
    echo.prepare (sampleRate, seed);
    limiter.prepare (sampleRate);
    bypassSm.reset (newSampleRate, 0.03);
    setLatencySamples (0);
    resetDsp();
}

void DendriteProcessor::reset()
{
    resetDsp();
}

void DendriteProcessor::resetDsp()
{
    echo.setAmount (amountParam->load());
    echo.setHeldThrow (false);
    echo.reset();
    limiter.reset();
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

    // The echo runs on the song's tempo; without one (an offline renderer, a validator) at 120 BPM.
    double bpm = 120.0;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto hostBpm = position->getBpm())
                if (*hostBpm > 0.0)
                    bpm = *hostBpm;

    echo.setAmount (amountParam->load());
    echo.setHeldThrow (throwParam->get());
    bypassSm.setTargetValue (bypassParam->get() ? 1.0f : 0.0f);

    float* left = buffer.getWritePointer (0);
    float* right = numIn > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int start = 0; start < numSamples; start += subBlock)
    {
        const int n = std::min (subBlock, numSamples - start);

        for (int i = 0; i < n; ++i)
        {
            const auto s = (size_t) i;
            dryL[s] = left[start + i];
            dryR[s] = right != nullptr ? right[start + i] : dryL[s];
        }

        echo.process (dryL.data(), dryR.data(), wetL.data(), wetR.data(), n, bpm);

        for (int i = 0; i < n; ++i)
        {
            const auto s = (size_t) i;
            float l = dryL[s] + wetL[s];
            float r = dryR[s] + wetR[s];
            limiter.process (l, r);

            // bypass crossfades to the input
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

    throwGlowForUi.store (echo.getThrowGlow(), std::memory_order_relaxed);

    // NaN/Inf guard: never hand a non-finite sample to the host. Silence this block
    // and start again from scratch.
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

void DendriteProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DendriteProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Sets from 0.4.0 and before load too: their old knobs are simply ignored, and
    // amount starts at its default.
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DendriteProcessor();
}
