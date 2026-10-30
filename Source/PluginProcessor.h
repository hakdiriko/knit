#pragma once

#include "dsp/DubEcho.h"
#include "dsp/OutputChain.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace ParamIDs
{
// Never rename these: hosts store automation and saved state by ID.
inline constexpr auto amount  = "amount";
inline constexpr auto throwIt = "throw";
inline constexpr auto bypass  = "bypass";
} // namespace ParamIDs

// Signal flow:
//   input -> + dotted dub echo (its own tape loop, branching, breathing, throws)
//         -> safety limiter -> NaN guard -> bypass crossfade -> output
class DendriteProcessor final : public juce::AudioProcessor
{
public:
    DendriteProcessor();
    ~DendriteProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioParameterBool* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState parameters;

    // For the window: glows when the echo takes a throw.
    std::atomic<float> throwGlowForUi { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void resetDsp();

    static constexpr int subBlock = 64;

    std::atomic<float>* amountParam = nullptr;
    juce::AudioParameterBool* throwParam = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    dendrite::dsp::DubEcho echo;
    dendrite::dsp::SafetyLimiter limiter;

    juce::SmoothedValue<float> bypassSm;
    float sampleRate = 48000.0f;

    std::array<float, subBlock> dryL {}, dryR {}, wetL {}, wetR {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteProcessor)
};
