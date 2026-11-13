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
        // 1.2: the echo's rhythm, and dry off for return tracks
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "time", 9 }, "time",
                                                            StringArray { "dotted", "triplet", "half" }, 0));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "dry", 9 }, "dry", true));
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
    timeParam = params.getRawParameterValue ("time");
    dryParam = params.getRawParameterValue ("dry");

    // Every dendrite is born a creature of its own: a genome from the system's randomness.
    auto genome = (uint64_t) juce::Random::getSystemRandom().nextInt64();
    genome ^= (uint64_t) juce::Time::getHighResolutionTicks() * 0x9E3779B97F4A7C15ull;
    genome ^= (uint64_t) (juce::pointer_sized_uint) this;
    engine.creature().beBorn (genome);
    vault.life = engine.creature().saved();
}

juce::AudioProcessorParameter* DendriteProcessor::getBypassParameter() const { return params.getParameter ("bypass"); }

bool DendriteProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // The space lives between two ears, so the output is always stereo.
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void DendriteProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    setLatencySamples (0);
    engine.setControls (feedParam->load(), spaceParam->load(), false, (int) itchParam->load(), bypassParam->load() > 0.5f);
    engine.setStyle ((int) timeParam->load(), dryParam->load() > 0.5f);
    engine.prepare ((float) sampleRate);
    keep.prepare (sampleRate, std::max (samplesPerBlock, 512));
}

void DendriteProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { run (buffer, false); }

void DendriteProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { run (buffer, true); }

void DendriteProcessor::run (juce::AudioBuffer<float>& buffer, bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;

    // a creature loaded from the set takes over here, between two blocks
    int ready = 2;
    if (arrivingState.compare_exchange_strong (ready, 3, std::memory_order_acquire))
    {
        engine.creature().adopt (arriving);
        arrivingState.store (0, std::memory_order_release);
        keptAge = 0;
    }

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
    engine.setStyle ((int) timeParam->load(), dryParam->load() > 0.5f);
    // in pieces no longer than keep's tap, so the heads can be recorded whatever block the host sends
    {
        auto* left = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        const int frameChoice = (int) itchParam->load();
        engine.setItchTap (keep.itchTapLeft(), keep.itchTapRight());
        auto piece = hostTime;
        for (int done = 0; done < n;)
        {
            const int m = std::min (n - done, keep.tapSize());
            engine.process (left + done, right + done, m, piece);
            keep.capture (left + done, right + done, m, engine.view(), piece, frameChoice);
            piece.ppq += m * piece.bpm / 60.0 / getSampleRate();
            done += m;
        }
    }

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
    keepLife();
}

void DendriteProcessor::keepLife() noexcept
{
    const auto& life = engine.creature().life();
    if (life.age < keptAge + 16 && life.itches == keptItches && keptAge != 0)
        return;
    keptAge = std::max<uint64_t> (1, life.age);
    keptItches = life.itches;
    vault.seq.fetch_add (1, std::memory_order_acq_rel); // odd: writing
    std::atomic_thread_fence (std::memory_order_release);
    vault.life = engine.creature().saved();
    vault.seq.fetch_add (1, std::memory_order_release); // even: done
}

dendrite::Creature::Life DendriteProcessor::readLife() const
{
    if (arrivingState.load (std::memory_order_acquire) == 2)
        return arriving;
    dendrite::Creature::Life copy;
    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        const auto before = vault.seq.load (std::memory_order_acquire);
        if ((before & 1u) != 0)
        {
            std::this_thread::yield();
            continue;
        }
        std::memcpy (&copy, &vault.life, sizeof (copy));
        std::atomic_thread_fence (std::memory_order_acquire);
        if (vault.seq.load (std::memory_order_relaxed) == before)
            return copy;
    }
    return copy;
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
    d.runaway.store (v.runaway, relaxed);
    d.bpm.store ((float) v.bpm, relaxed);
    d.hostPlaying.store (v.hostPlaying, relaxed);
    d.itching.store (v.itching, relaxed);
    d.flyJump.store (v.fly.jump, relaxed);
    d.flySemitones.store (v.fly.semitones, relaxed);
    d.flyScratch.store (v.fly.scratch, relaxed);
    d.flyScratchHz.store (v.fly.scratchHz, relaxed);
    d.flyTurn.store (v.fly.turn, relaxed);
    d.flyArousal.store (v.fly.arousal, relaxed);
    d.flyFatigue.store (v.fly.fatigue, relaxed);
    d.flyFear.store (v.fly.fear, relaxed);
    d.flyMemory.store (v.fly.memory, relaxed);
    d.flyThrowing.store (v.fly.throwing, relaxed);
    d.flyFrozen.store (v.fly.frozen, relaxed);
    d.flyAwake.store (v.flyAwake, relaxed);

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
    auto state = params.copyState();
    state.removeChild (state.getChildWithName ("creature"), nullptr);

    // the creature's life, sealed (about 5.5 KB), next to the parameters
    const auto life = readLife();
    juce::ValueTree creature ("creature");
    creature.setProperty ("genome", juce::String::toHexString ((juce::int64) life.genome), nullptr);
    creature.setProperty ("age", (juce::int64) life.age, nullptr);
    creature.setProperty ("itches", (int) life.itches, nullptr);
    creature.setProperty ("life", juce::Base64::toBase64 (&life, sizeof (life)), nullptr);
    state.appendChild (creature, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void DendriteProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (params.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml (*xml);

    // A set saved with a creature brings it back; a set from before 2.0 (or a broken life)
    // keeps the newborn this instance already is.
    const auto creature = state.getChildWithName ("creature");
    if (creature.isValid())
    {
        juce::MemoryOutputStream bytes;
        if (juce::Base64::convertFromBase64 (bytes, creature.getProperty ("life").toString())
            && bytes.getDataSize() == sizeof (dendrite::Creature::Life))
        {
            dendrite::Creature::Life life;
            std::memcpy (&life, bytes.getData(), sizeof (life));
            if (life.valid())
            {
                for (int spins = 0;; ++spins)
                {
                    int expected = arrivingState.load (std::memory_order_acquire);
                    if ((expected == 0 || expected == 2)
                        && arrivingState.compare_exchange_weak (expected, 1, std::memory_order_acquire))
                        break;
                    if (spins > 100000)
                        return; // the audio thread is stuck adopting: keep what we have
                    std::this_thread::yield();
                }
                arriving = life;
                arrivingState.store (2, std::memory_order_release);
            }
        }
        state.removeChild (creature, nullptr);
    }
    params.replaceState (state);
}

juce::AudioProcessorEditor* DendriteProcessor::createEditor() { return new DendriteEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DendriteProcessor(); }
