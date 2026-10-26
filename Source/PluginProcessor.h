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
inline constexpr auto sync    = "sync";
inline constexpr auto itch    = "itch";
inline constexpr auto scratch = "scratch";
inline constexpr auto jump    = "jump";    // shown as "Steady" since 0.3.0 (it grew from Sync jump)
inline constexpr auto autoMoves = "auto";
inline constexpr auto bars    = "bars";
inline constexpr auto build   = "build";
} // namespace ParamIDs

// Signal flow:
//   input -> 4-node network (with the itch) -> wet auto-gain -> Mix (equal power)
//         -> Output trim -> safety limiter -> NaN guard -> bypass crossfade -> output
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

    // For the window: the Echo node's note value while Sync is on (-1 when free),
    // and whether the host reports a tempo at all.
    std::atomic<int> echoNoteForUi { -1 };
    std::atomic<bool> hostHasTempo { false };

    // For the window: how many bars of repetition it has counted, the move playing
    // (-1 for none), and a counter that ticks once per move.
    std::atomic<float> barsHeardForUi { 0.0f };
    std::atomic<int> moveForUi { -1 };
    std::atomic<unsigned> scratchCountForUi { 0 };

    // Every press flips the Scratch parameter, and any flip scratches, so a button,
    // automation or a MIDI-mapped toggle all work.
    void pressScratch();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void resetDsp();
    double hostBpm() const;

    static constexpr int subBlock = 64;

    std::atomic<float>* ecologyParam = nullptr;
    std::atomic<float>* sizeParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* itchParam = nullptr;
    std::atomic<float>* scratchParam = nullptr;
    std::atomic<float>* jumpParam = nullptr;
    std::atomic<float>* autoParam = nullptr;
    std::atomic<float>* barsParam = nullptr;
    std::atomic<float>* buildParam = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    dendrite::dsp::Network network;
    dendrite::dsp::AutoGain autoGain;
    dendrite::dsp::SafetyLimiter limiter;

    juce::SmoothedValue<float> mixSm, trimSm, bypassSm;

    std::array<float, subBlock> dryL {}, dryR {}, wetL {}, wetR {};

    bool lastScratch = false;
    std::array<float, 5> lastKnobs {};

    // Scars live in the network but are saved with the set: the audio thread mirrors
    // them here, and a loaded state hands new ones over through `scarsPending`.
    std::array<std::atomic<float>, 6> scarsSaved {};
    std::array<std::atomic<float>, 6> scarsLoaded {};
    std::atomic<bool> scarsPending { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DendriteProcessor)
};
