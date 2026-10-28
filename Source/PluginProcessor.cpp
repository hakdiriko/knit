#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::String percent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

juce::String withWord (float v, const char* word) { return percent (v) + juce::String::fromUTF8 (" \xc2\xb7 ") + word; }

// Wear is told in bars, so you know what to expect: about when an unchanged loop has
// audibly worn. Measured at 0.4.0 on drum, pad, voice and field loops: drums wear in
// about half this, long sustained sounds in about 1.5 times this.
juce::String wearText (float v, int)
{
    if (v < 0.005f)
        return withWord (v, "never");
    const int bars = juce::roundToInt (8.0f + 64.0f * std::pow (1.5f / 24.0f, v));
    return withWord (v, ("~" + juce::String (bars) + " bars").toRawUTF8());
}

juce::String frictionText (float v, int)
{
    return withWord (v, v < 0.02f ? "clean" : v < 0.35f ? "warm" : v < 0.7f ? "worn" : "rough");
}

juce::String roomText (float v, int)
{
    return withWord (v, v < 0.02f ? "none" : v < 0.35f ? "wood" : v < 0.7f ? "hall" : "afterlife");
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
    wearParam = parameters.getRawParameterValue (ParamIDs::wear);
    frictionParam = parameters.getRawParameterValue (ParamIDs::friction);
    roomParam = parameters.getRawParameterValue (ParamIDs::room);
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (ParamIDs::bypass));
    jassert (bypassParam != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout DendriteProcessor::createParameterLayout()
{
    using Float = juce::AudioParameterFloat;
    using Attributes = juce::AudioParameterFloatAttributes;
    const juce::NormalisableRange<float> unit (0.0f, 1.0f);

    // 0.4.0 replaced every earlier knob, so these carry version hint 5.
    auto knob = [&] (const char* id, const char* name, float defaultValue, auto toText)
    {
        return std::make_unique<Float> (juce::ParameterID { id, 5 }, name, unit, defaultValue,
                                        Attributes().withStringFromValueFunction (toText)
                                                    .withValueFromStringFunction (fromPercent));
    };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (knob (ParamIDs::wear, "wear", 0.3f, wearText),
                knob (ParamIDs::friction, "friction", 0.3f, frictionText),
                knob (ParamIDs::room, "room", 0.3f, roomText),
                std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::bypass, 1 }, "bypass", false));
    return layout;
}

void DendriteProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = (float) newSampleRate;

    tape.prepare (sampleRate, seed);
    room.prepare (sampleRate, seed);
    limiter.prepare (sampleRate);
    for (auto& d : dryDelay)
        d.prepare (tape.getLatencySamples() + 4);

    roomReturnSm.reset (newSampleRate, 0.05);
    directSm.reset (newSampleRate, 0.05);
    bypassSm.reset (newSampleRate, 0.03);
    knobCoef = 1.0f - std::exp (-(float) subBlock / (0.15f * sampleRate)); // knobs glide over ~150 ms

    setLatencySamples (tape.getLatencySamples());
    resetDsp();
}

void DendriteProcessor::reset()
{
    resetDsp();
}

void DendriteProcessor::resetDsp()
{
    tape.reset();
    room.reset();
    limiter.reset();
    for (auto& d : dryDelay)
        d.reset();

    roomAmount = roomParam->load();
    frictionAmount = frictionParam->load();
    tape.setWear (wearParam->load());
    tape.setFriction (frictionAmount);
    setRoomAmount = -1.0f;
    setRoom (roomAmount);
    roomReturnSm.setCurrentAndTargetValue (dendrite::dsp::Room::returnGain (roomAmount));
    directSm.setCurrentAndTargetValue (dendrite::dsp::Room::directGain (roomAmount));
    bypassSm.setCurrentAndTargetValue (bypassParam->get() ? 1.0f : 0.0f);
    internalPpq = 0.0;
}

void DendriteProcessor::setRoom (float amount)
{
    if (std::abs (amount - setRoomAmount) < 1.0e-4f)
        return;
    setRoomAmount = amount;
    room.set (amount);
    roomReturnSm.setTargetValue (dendrite::dsp::Room::returnGain (amount));
    directSm.setTargetValue (dendrite::dsp::Room::directGain (amount));
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

    // The tape runs on the song's clock. Without one (an offline renderer, a validator)
    // it runs on its own at 120 BPM, always playing.
    dendrite::dsp::Tape::Transport transport;
    bool hostClock = false;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto bpm = position->getBpm())
                if (const auto ppq = position->getPpqPosition())
                    if (*bpm > 0.0)
                    {
                        transport.bpm = *bpm;
                        transport.ppq = *ppq;
                        transport.playing = position->getIsPlaying();
                        hostClock = true;
                    }
    if (! hostClock)
    {
        transport.bpm = 120.0;
        transport.ppq = internalPpq;
        transport.playing = true;
        internalPpq += numSamples * 120.0 / (60.0 * sampleRate);
    }
    const double quartersPerSample = transport.bpm / (60.0 * sampleRate);

    tape.setWear (wearParam->load());
    bypassSm.setTargetValue (bypassParam->get() ? 1.0f : 0.0f);
    const int latency = tape.getLatencySamples();

    float* left = buffer.getWritePointer (0);
    float* right = numIn > 1 ? buffer.getWritePointer (1) : nullptr;

    std::array<float, subBlock> wetL {}, wetR {};

    for (int start = 0; start < numSamples; start += subBlock)
    {
        const int n = std::min (subBlock, numSamples - start);

        roomAmount += knobCoef * (roomParam->load() - roomAmount);
        frictionAmount += knobCoef * (frictionParam->load() - frictionAmount);
        tape.setFriction (frictionAmount);
        setRoom (roomAmount);

        for (int i = 0; i < n; ++i)
        {
            const auto s = (size_t) i;
            dryL[s] = left[start + i];
            dryR[s] = right != nullptr ? right[start + i] : dryL[s];
            wetL[s] = dryL[s];
            wetR[s] = dryR[s];
        }

        auto blockTransport = transport;
        blockTransport.ppq += start * quartersPerSample;
        tape.process (wetL.data(), wetR.data(), lostL.data(), lostR.data(), n, blockTransport);

        for (int i = 0; i < n; ++i)
        {
            const auto s = (size_t) i;

            // the room hears the tape, and catches what wore off it
            float roomL, roomR;
            using dendrite::dsp::Room;
            room.process (Room::tapeSend * wetL[s] + Room::lostSend * lostL[s],
                          Room::tapeSend * wetR[s] + Room::lostSend * lostR[s], roomL, roomR);

            const float direct = directSm.getNextValue();
            const float back = roomReturnSm.getNextValue();
            float l = direct * wetL[s] + back * roomL;
            float r = direct * wetR[s] + back * roomR;
            limiter.process (l, r);

            // bypass crossfades to the input, delayed by the same latency as the tape
            dryDelay[0].push (dryL[s]);
            dryDelay[1].push (dryR[s]);
            const float b = bypassSm.getNextValue();
            l += b * (dryDelay[0].read ((float) latency + 1.0f) - l);
            r += b * (dryDelay[1].read ((float) latency + 1.0f) - r);

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

    for (int c = 0; c < dendrite::dsp::Tape::cellsPerTape; ++c)
        wearForUi[(size_t) c].store (tape.getCellWear (c), std::memory_order_relaxed);
    headForUi.store (tape.getTapePosition(), std::memory_order_relaxed);
    playingForUi.store (transport.playing, std::memory_order_relaxed);

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
    // Sets from 0.3.0 and before load too: their old knobs are simply ignored, and
    // wear, friction and room start at their defaults.
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DendriteProcessor();
}
