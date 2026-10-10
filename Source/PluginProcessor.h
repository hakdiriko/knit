#pragma once

#include "dsp/Network.h"
#include "dsp/OutputChain.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace ParamIDs
{
// Never rename these: hosts store automation and saved state by ID.
inline constexpr auto ecology = "ecology";
inline constexpr auto size    = "size";
inline constexpr auto tone    = "tone";
inline constexpr auto mix     = "mix";
inline constexpr auto output  = "output";
inline constexpr auto bypass  = "bypass";
} // namespace ParamIDs

// Phase 1: the network. Signal flow:
//   input -> 4-node network -> wet auto-gain -> Mix (equal power) -> Output trim
//         -> safety limiter -> NaN guard -> bypass crossfade -> output
class ViberProcessor final : public juce::AudioProcessor
{
public:
    ViberProcessor();
    ~ViberProcessor() override = default;

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void resetDsp();

    static constexpr int subBlock = 64;

    std::atomic<float>* ecologyParam = nullptr;
    std::atomic<float>* sizeParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    viber::dsp::Network network;
    viber::dsp::AutoGain autoGain;
    viber::dsp::SafetyLimiter limiter;

    juce::SmoothedValue<float> mixSm, trimSm, bypassSm;

    std::array<float, subBlock> dryL {}, dryR {}, wetL {}, wetR {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViberProcessor)
};
