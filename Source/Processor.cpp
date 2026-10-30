#include "Processor.h"
#include "Editor.h"

const juce::StringArray HypnagogicProcessor::frameNames { "off", "1 bar", "1.25 bars", "1.5 bars", "1.75 bars",
                                                          "2 bars", "3 bars", "4 bars", "8 bars" };

namespace
{
    // IDs and the state type are what hosts store; they never change.
    // "viber" is the state type every dendrite build has used since 0.0.
    constexpr int paramVersion = 7;

    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "orbit", paramVersion }, "orbit", NormalisableRange<float> (0.0f, 1.0f),
            HypnagogicProcessor::defaultOrbit,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                const int degrees = juce::roundToInt (hyp::wrapUnit (v - 0.5f) * 360.0f) % 360;
                return String (hyp::Field::placeName (v)) + " " + String (degrees) + String (CharPointer_UTF8 ("\xc2\xb0")); })));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "depth", paramVersion }, "depth", NormalisableRange<float> (0.0f, 1.0f),
            HypnagogicProcessor::defaultDepth,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
                return String (hyp::Field::regimeName (v)) + " " + String (juce::roundToInt (v * 100.0f)) + "%"; })));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "frame", paramVersion }, "itch frame",
                                                            HypnagogicProcessor::frameNames, 5));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", paramVersion }, "bypass", false));
        return layout;
    }

    uint64_t mixSeed (int64_t frame, int choice)
    {
        uint64_t z = (uint64_t) (frame + 0x100000) * 0x9E3779B97F4A7C15ull + 0xBF58476D1CE4E5B9ull * (unsigned) choice;
        z ^= z >> 31; z *= 0x94D049BB133111EBull; z ^= z >> 29;
        return z;
    }

    float coefFor (float seconds, double sr) { return std::exp (-1.0f / (seconds * (float) sr)); }
}

HypnagogicProcessor::HypnagogicProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "viber", makeLayout())
{
    orbitParam = params.getRawParameterValue ("orbit");
    depthParam = params.getRawParameterValue ("depth");
    frameParam = params.getRawParameterValue ("frame");
    bypassParam = params.getRawParameterValue ("bypass");
}

juce::AudioProcessorParameter* HypnagogicProcessor::getBypassParameter() const
{
    return params.getParameter ("bypass");
}

bool HypnagogicProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // The space lives between two ears, so the output is always stereo.
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void HypnagogicProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;

    // 5 ms of look-ahead buys an exact 90-degree twin of every band above 150 Hz.
    analytic.prepare (sr, 5.0f, 150.0f, 18000.0f, 6.0f);
    setLatencySamples (analytic.latency);
    dryLeft.prepare (analytic.latency + 2);
    dryRight.prepare (analytic.latency + 2);

    field.prepare (sr);
    listener.prepare (sr);
    heads.prepare (sr);

    attackFilter.highpass (150.0f, 0.7071f, sr);
    fastUp = coefFor (0.0003f, sr); fastDown = coefFor (0.008f, sr);
    slowUp = coefFor (0.012f, sr);  slowDown = coefFor (0.06f, sr);
    attackUp = coefFor (0.0005f, sr); attackDown = coefFor (0.03f, sr);

    bandPower.setTime (2.0f, sr);
    sidePower.setTime (2.0f, sr);
    balance.setTime (1.0f, sr);
    wet.setTime (0.01f, sr);              // about 30 ms to settle: a click-free bypass
    limiterRelease = coefFor (0.12f, sr);
    resetEngine();
}

void HypnagogicProcessor::resetEngine()
{
    analytic.clear();
    dryLeft.clear(); dryRight.clear();
    field.setKnobs (orbitParam->load(), depthParam->load());
    field.reset();
    listener.reset();
    heads.reset();
    attackFilter.clear();
    attackFast = attackSlow = attack = 0.0f;
    beat = 0.0;
    lastStep = -1; lastFrame = std::numeric_limits<int64_t>::min();
    frameSum = 0.0f; frameSteps = 0; boredFrames = 0; frameStartedIdle = true;
    bandPower.value = sidePower.value = 0.0f;
    balance.value = 1.0f;
    wet.value = bypassParam->load() > 0.5f ? 0.0f : 1.0f;
    limiterEnv = 0.0f;
    corrLR = corrLL = corrRR = 0.0f;
}

void HypnagogicProcessor::tickClock (double beatsPerSample, double frameBeats, int stepsPerBar)
{
    const auto step = (int64_t) std::floor (beat * 4.0);
    if (step != lastStep)
    {
        lastStep = step;
        listener.step (stepsPerBar);
        frameSum += listener.repetition();
        ++frameSteps;
    }

    const auto frame = (int64_t) std::floor (beat / frameBeats);
    if (frame != lastFrame)
    {
        const int choice = (int) frameParam->load();
        // A frame counts only if it was heard whole and the heads were quiet when it began:
        // after an itch is scratched there is always a full frame of relief.
        const bool counted = lastFrame != std::numeric_limits<int64_t>::min() && frameStartedIdle
                          && frameSteps >= (int) (0.6 * frameBeats * 4.0);
        if (counted)
        {
            const float mean = frameSum / (float) frameSteps;
            if (mean > 0.72f)
            {
                ++boredFrames;
                if (choice > 0)
                {
                    // The longer the loop goes on, the harder it itches.
                    const float intensity = 0.45f + 0.18f * (float) (boredFrames - 1) + 1.2f * (mean - 0.72f);
                    heads.fire (intensity, mixSeed (frame, choice), frameBeats / 4.0 / beatsPerSample);
                }
            }
            else if (mean < 0.5f)
            {
                boredFrames = 0;
            }
        }
        lastFrame = frame;
        frameSum = 0.0f; frameSteps = 0;
        frameStartedIdle = ! heads.busy();
    }
    beat += beatsPerSample;
}

void HypnagogicProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    run (buffer, false);
}

void HypnagogicProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Same latency when bypassed, so the track never jumps in time.
    run (buffer, true);
}

void HypnagogicProcessor::run (juce::AudioBuffer<float>& buffer, bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inChannels = getTotalNumInputChannels();
    for (int c = inChannels; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (buffer.getNumChannels() < 2 || n == 0) return;

    field.setKnobs (orbitParam->load(), depthParam->load());
    const int choice = juce::jlimit (0, 8, (int) frameParam->load());
    const float wetTarget = forceBypass || bypassParam->load() > 0.5f ? 0.0f : 1.0f;

    // The clock: Live's when it plays, otherwise a free 120 BPM.
    double bpm = 120.0, barBeats = 4.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto t = pos->getBpm()) bpm = juce::jlimit (20.0, 400.0, *t);
            if (auto sig = pos->getTimeSignature()) if (sig->numerator > 0 && sig->denominator > 0)
                barBeats = sig->numerator * 4.0 / sig->denominator;
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition()) { beat = *ppq; hostPlaying = true; }
        }
    display.hostTempo.store (hostPlaying, std::memory_order_relaxed);

    const double beatsPerSample = bpm / 60.0 / sr;
    const int stepsPerBar = juce::jlimit (4, 64, juce::roundToInt (barBeats * 4.0));
    const double frameBeats = (choice > 0 ? (double) frameBars[choice] : 4.0) * barBeats;
    const int lag = analytic.latency;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    const bool monoIn = inChannels < 2;

    for (int i = 0; i < n; ++i)
    {
        const float l = left[i], r = monoIn ? left[i] : right[i];
        const float mid = 0.5f * (l + r);

        listener.push (mid);
        tickClock (beatsPerSample, frameBeats, stepsPerBar);

        // Onsets, measured on the live input, act on the delayed signal: a 5 ms head start.
        const float level = std::abs (attackFilter.process (mid));
        attackFast = level + (level > attackFast ? fastUp : fastDown) * (attackFast - level);
        attackSlow = level + (level > attackSlow ? slowUp : slowDown) * (attackSlow - level);
        const float onset = hyp::clamp01 ((attackFast / (attackSlow + 1e-6f) - 1.3f) * 1.25f);
        attack = onset + (onset > attack ? attackUp : attackDown) * (attack - onset);

        // Everything from here on is `lag` samples behind the input.
        float re = 0.0f, im = 0.0f;
        analytic.push (mid, re, im);
        dryLeft.push (l); dryRight.push (r);
        const float dl = dryLeft.tap (lag), dr = dryRight.tap (lag);
        const float dMid = 0.5f * (dl + dr), dSide = 0.5f * (dl - dr);

        hyp::HeadMods mods;
        const float scream = heads.process (re, im, mods);
        const float made = field.process (re, im, attack, mods);

        // Adding Side makes the stereo louder while the mono sum stays the dry Mid.
        // Pull both back by half the added power in dB: mono moves level, never colour.
        const float ratio = std::min (1.5f, sidePower.step (made * made) / (bandPower.step (re * re) + 1e-9f));
        const float g = balance.step (1.0f / std::sqrt (std::sqrt (1.0f + ratio)));

        const float side = dSide + made + scream;
        const float w = wet.step (wetTarget);
        float outL = dl + w * (g * (dMid + side) - dl);
        float outR = dr + w * (g * (dMid - side) - dr);

        // Safety limiter: instant attack, 120 ms release, -0.3 dBFS.
        const float peak = std::max (std::abs (outL), std::abs (outR));
        limiterEnv = peak > limiterEnv ? peak : peak + limiterRelease * (limiterEnv - peak);
        if (limiterEnv > 0.966f) { const float lg = 0.966f / limiterEnv; outL *= lg; outR *= lg; }

        left[i] = outL; right[i] = outR;

        corrLR = 0.9995f * corrLR + outL * outR;
        corrLL = 0.9995f * corrLL + outL * outL;
        corrRR = 0.9995f * corrRR + outR * outR;
        if (++scopeDecimate >= 3)
        {
            scopeDecimate = 0;
            const int h = display.scopeHead.load (std::memory_order_relaxed);
            display.scope[(size_t) (2 * h)].store (outL, std::memory_order_relaxed);
            display.scope[(size_t) (2 * h + 1)].store (outR, std::memory_order_relaxed);
            display.scopeHead.store ((h + 1) % Display::points, std::memory_order_relaxed);
        }
    }

    // Never hand a host a NaN or an infinity.
    bool finite = true;
    for (int c = 0; c < 2 && finite; ++c)
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (buffer.getReadPointer (c)[i])) { finite = false; break; }
    if (! finite)
    {
        buffer.clear();
        resetEngine();
        return;
    }

    const float denom = std::sqrt (corrLL * corrRR);
    display.correlation.store (denom > 1e-9f ? corrLR / denom : 1.0f, std::memory_order_relaxed);
    display.repetition.store (listener.repetition(), std::memory_order_relaxed);
    display.frameProgress.store ((float) (std::fmod (std::max (0.0, beat), frameBeats) / frameBeats), std::memory_order_relaxed);
    for (int h = 0; h < 3; ++h) display.lamps[(size_t) h].store (heads.lamp (h), std::memory_order_relaxed);
}

void HypnagogicProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void HypnagogicProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (params.state.getType()))
            params.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* HypnagogicProcessor::createEditor() { return new HypnagogicEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HypnagogicProcessor(); }
