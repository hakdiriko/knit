#include "Keep.h"

namespace dendrite::keep
{
namespace
{
    constexpr float quietRatio = 1.0e-3f;   // an itch's tail has ended 60 dB under its peak...
    constexpr double quietSeconds = 0.3;    // ...held this long
    constexpr double throwTailBars = 2.0;   // a throw's echoes: two bars after you let go
    constexpr double safetySeconds = 0.25;  // never read this close to where the tape is writing over

    juce::String bpmText (double bpm)
    {
        const double r = std::round (bpm * 10.0) / 10.0;
        return std::abs (r - std::round (r)) < 1.0e-6 ? juce::String ((int) std::round (r)) : juce::String (r, 1);
    }
} // namespace

juce::String headNames (uint8_t mask)
{
    juce::StringArray names;
    for (int k = 0; k < Heads::count; ++k)
        if ((mask >> k) & 1u)
            names.add (Heads::names[k]);
    return names.joinIntoString (" ");
}

void Keep::prepare (double sampleRate, int maxBlock)
{
    const juce::ScopedLock lock (storage);
    const auto wanted = std::min<int64_t> ((int64_t) std::ceil (seconds * sampleRate), maxFrames);
    if (wanted != frames || std::abs (sampleRate - sr) > 0.5)
    {
        sr = sampleRate;
        frames = wanted;
        for (auto* v : { &allL, &allR, &itchL, &itchR })
            v->assign ((size_t) frames, 0.0f);
        writtenFrames.store (0, std::memory_order_release);
        epoch.fetch_add (1, std::memory_order_release);
        itchState = ItchState::idle;
        throwOn = runawayOn = false;
    }
    const auto block = (size_t) std::max (maxBlock, 1);
    if (tapL.size() < block)
    {
        tapL.assign (block, 0.0f);
        tapR.assign (block, 0.0f);
    }
}

void Keep::push (const Mark& m) noexcept
{
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)
        pending[(size_t) scope.startIndex1] = m;
}

void Keep::capture (const float* outL, const float* outR, int n, const Engine::View& view, const HostTime& time,
                    int frameChoice) noexcept
{
    if (frames <= 0 || n <= 0)
        return;
    n = std::min (n, (int) tapL.size());
    const int64_t start = writtenFrames.load (std::memory_order_relaxed);

    // the tapes
    for (int i = 0; i < n;)
    {
        const auto at = (size_t) ((start + i) % frames);
        const int run = (int) std::min<int64_t> (n - i, frames - (int64_t) at);
        std::copy (outL + i, outL + i + run, allL.begin() + (std::ptrdiff_t) at);
        std::copy (outR + i, outR + i + run, allR.begin() + (std::ptrdiff_t) at);
        std::copy (tapL.data() + i, tapL.data() + i + run, itchL.begin() + (std::ptrdiff_t) at);
        std::copy (tapR.data() + i, tapR.data() + i + run, itchR.begin() + (std::ptrdiff_t) at);
        i += run;
    }

    // the clock: Live's position while it plays, otherwise our own, the way the engine counts
    const double bpm = std::clamp (time.bpm, 30.0, 300.0);
    const double barBeats = time.barBeats > 0.5 ? time.barBeats : 4.0;
    if (time.playing)
        freeBeat = time.ppq;
    const double beat0 = freeBeat, beatsPerFrame = bpm / 60.0 / sr;
    freeBeat += beatsPerFrame * n;

    auto stamp = [&] (Mark m, int offset) {
        m.sample = start + offset;
        m.ppq = beat0 + beatsPerFrame * offset;
        m.bpm = bpm;
        m.barBeats = barBeats;
        m.hostPlaying = time.playing;
        m.frameChoice = (uint8_t) std::clamp (frameChoice, 0, 255);
        return m;
    };
    auto fresh = [&] (Kind kind, int offset) {
        Mark m;
        m.id = nextId++;
        m.kind = kind;
        return stamp (m, offset);
    };

    // the itch: from its first sound to the end of its own tail
    float blockPeak = 0.0f;
    int firstSound = -1, lastLoud = -1;
    for (int i = 0; i < n; ++i)
    {
        const float a = std::max (std::abs (tapL[(size_t) i]), std::abs (tapR[(size_t) i]));
        if (a > 1.0e-7f && firstSound < 0)
            firstSound = i;
        blockPeak = std::max (blockPeak, a);
    }

    if (view.itching && (itchState == ItchState::idle || itchState == ItchState::tail))
    {
        if (itchState == ItchState::tail)
        {
            // a new itch while the last one still rings: the last one ends here
            itchMark.endSample = start;
            push (itchMark);
        }
        itchState = ItchState::armed;
        itchPeak = 0.0f;
    }
    if (itchState == ItchState::armed)
    {
        if (firstSound >= 0)
        {
            itchMark = fresh (Kind::itch, firstSound);
            itchMark.endSample = -1;
            itchState = ItchState::playing;
            push (itchMark);
        }
        else if (! view.itching)
        {
            itchState = ItchState::idle; // it was stopped before it made a sound
        }
    }
    if (itchState == ItchState::playing || itchState == ItchState::tail)
    {
        for (int k = 0; k < Heads::count; ++k)
            if (view.lamps[(size_t) k] > 0.05f)
                itchMark.headsMask |= (uint8_t) (1u << k);
        itchPeak = std::max (itchPeak, blockPeak);
        const float floor = itchPeak * quietRatio;
        for (int i = n - 1; i >= 0; --i)
            if (std::max (std::abs (tapL[(size_t) i]), std::abs (tapR[(size_t) i])) > floor)
            {
                lastLoud = i;
                break;
            }
        if (lastLoud >= 0)
            itchLastLoud = start + lastLoud + 1;
        if (itchState == ItchState::playing && ! view.itching)
            itchState = ItchState::tail;
        if (itchState == ItchState::tail && (double) (start + n - itchLastLoud) >= quietSeconds * sr)
        {
            itchMark.endSample = std::max (itchMark.sample + 1, itchLastLoud);
            push (itchMark);
            itchState = ItchState::idle;
        }
    }

    // a throw, from press to release
    const bool throwing = view.echo.throwing > 0.5f;
    if (throwing != throwOn)
    {
        throwOn = throwing;
        if (throwing)
        {
            throwMark = fresh (Kind::throwing, 0);
            push (throwMark);
        }
        else
        {
            throwMark.endSample = start;
            push (throwMark);
        }
    }

    // a runaway: the loop growing past what you played
    const bool running = view.runaway > 0.5f;
    if (running != runawayOn)
    {
        runawayOn = running;
        if (running)
        {
            runawayMark = fresh (Kind::runaway, 0);
            push (runawayMark);
        }
        else
        {
            runawayMark.endSample = start;
            push (runawayMark);
        }
    }

    // publish: the clock first (seqlock), then the tape's new end
    clockSeq.fetch_add (1, std::memory_order_acq_rel);
    clockFrame.store (start + n, std::memory_order_relaxed);
    clockBeat.store (freeBeat, std::memory_order_relaxed);
    clockBpm.store (bpm, std::memory_order_relaxed);
    clockBar.store (barBeats, std::memory_order_relaxed);
    clockPlaying.store (time.playing, std::memory_order_relaxed);
    clockSeq.fetch_add (1, std::memory_order_acq_rel);
    writtenFrames.store (start + n, std::memory_order_release);
}

Keep::Clock Keep::clock() const noexcept
{
    Clock c;
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        const auto a = clockSeq.load (std::memory_order_acquire);
        if (a & 1u)
            continue;
        c.frame = clockFrame.load (std::memory_order_relaxed);
        c.beat = clockBeat.load (std::memory_order_relaxed);
        c.bpm = clockBpm.load (std::memory_order_relaxed);
        c.barBeats = clockBar.load (std::memory_order_relaxed);
        c.playing = clockPlaying.load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (clockSeq.load (std::memory_order_relaxed) == a)
            break;
    }
    return c;
}

void Keep::collect()
{
    if (const auto e = epoch.load (std::memory_order_acquire); e != seenEpoch)
    {
        seenEpoch = e;
        marks.clear();
        kept.clear();
        { const auto drop = fifo.read (fifo.getNumReady()); } // marks from the old tape
        return;
    }
    const int ready = fifo.getNumReady();
    if (ready > 0)
    {
        const auto scope = fifo.read (ready);
        auto take = [&] (int startIndex, int size) {
            for (int i = 0; i < size; ++i)
            {
                const auto& m = pending[(size_t) (startIndex + i)];
                auto it = std::find_if (marks.begin(), marks.end(), [&] (const Mark& x) { return x.id == m.id; });
                if (it != marks.end())
                    *it = m;
                else
                    marks.push_back (m);
            }
        };
        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);
    }

    // what fell off the tape is forgotten (its files stay on disk)
    const auto oldest = written() - frames;
    marks.erase (std::remove_if (marks.begin(), marks.end(),
                                 [&] (const Mark& m) {
                                     const auto end = m.endSample >= 0 ? m.endSample : written();
                                     return end < oldest;
                                 }),
                 marks.end());
    kept.erase (std::remove_if (kept.begin(), kept.end(), [&] (const Kept& k) { return find (k.id) == nullptr; }), kept.end());
}

const Mark* Keep::find (uint32_t id) const noexcept
{
    for (const auto& m : marks)
        if (m.id == id)
            return &m;
    return nullptr;
}

double Keep::beatAt (int64_t frame, const Mark& m) const noexcept
{
    return m.ppq + (double) (frame - m.sample) * m.bpm / 60.0 / sr;
}

std::optional<Take> Keep::barsAround (int64_t start, double startBeat, int64_t end, double bpm, double barBeats) const
{
    if (end <= start)
        end = start + 1;
    const double framesPerBeat = sr * 60.0 / bpm;
    // the bar line at or before the start (a hair of tolerance: an itch that begins on the bar
    // line, a sample late in the block, still begins on that bar)
    const double firstBar = std::floor ((startBeat + 1.0e-3) / barBeats) * barBeats;
    const double endBeat = startBeat + (double) (end - start) / framesPerBeat;
    const int bars = std::max (1, (int) std::ceil ((endBeat - firstBar) / barBeats - 1.0e-3));
    Take t;
    t.from = start - (int64_t) std::llround ((startBeat - firstBar) * framesPerBeat);
    t.length = (int64_t) std::llround (bars * barBeats * framesPerBeat);
    t.bpm = bpm;
    t.barBeats = barBeats;
    t.bars = bars;
    t.fromBeat = firstBar;
    return t;
}

std::optional<Take> Keep::takeMark (const Mark& m, bool everything) const
{
    int64_t end = m.endSample >= 0 ? m.endSample : written();
    if (m.kind != Kind::itch)
    {
        // a throw or a runaway keeps going after you let go: its echoes, two bars more
        const auto tail = (int64_t) (throwTailBars * m.barBeats * sr * 60.0 / m.bpm);
        end = std::min (end + tail, written());
    }
    auto t = barsAround (m.sample, m.ppq, end, m.bpm, m.barBeats);
    if (! t)
        return t;
    t->itchOnly = m.kind == Kind::itch && ! everything;
    t->markId = m.id;
    const juce::String bars = juce::String (t->bars) + (t->bars == 1 ? " bar" : " bars");
    juce::StringArray parts;
    if (m.kind == Kind::itch)
    {
        parts.add (t->itchOnly ? "dendrite itch" : "dendrite itch, all");
        if (m.headsMask != 0)
            parts.add (headNames (m.headsMask));
    }
    else
    {
        parts.add (m.kind == Kind::throwing ? "dendrite throw" : "dendrite runaway");
    }
    parts.add (bars);
    parts.add (bpmText (m.bpm) + " bpm");
    parts.add (stamp (&m));
    t->name = parts.joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")));
    return t;
}

std::optional<Take> Keep::takeFrom (int64_t frame) const
{
    const auto c = clock();
    if (c.frame <= 0)
        return std::nullopt;
    const double framesPerBeat = sr * 60.0 / c.bpm;
    // the last full bar line before now
    const double lastBar = std::floor ((c.beat + 1.0e-3) / c.barBeats) * c.barBeats;
    const int64_t end = c.frame - (int64_t) std::llround ((c.beat - lastBar) * framesPerBeat);
    frame = std::clamp<int64_t> (frame, std::max<int64_t> (0, c.frame - frames), c.frame);
    const double beat = c.beat - (double) (c.frame - frame) / framesPerBeat;
    const double firstBar = std::floor ((beat + 1.0e-3) / c.barBeats) * c.barBeats;
    const int bars = std::max (1, (int) std::llround ((lastBar - firstBar) / c.barBeats));
    Take t;
    t.length = (int64_t) std::llround (bars * c.barBeats * framesPerBeat);
    t.from = end - t.length;
    t.bpm = c.bpm;
    t.barBeats = c.barBeats;
    t.bars = bars;
    t.fromBeat = lastBar - bars * c.barBeats;
    const juce::String barsText = juce::String (bars) + (bars == 1 ? " bar" : " bars");
    t.name = juce::StringArray { "dendrite", barsText, bpmText (c.bpm) + " bpm", stamp (nullptr) }
                 .joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")));
    return t;
}

juce::String Keep::stamp (const Mark* m) const
{
    // the clock time it happened: as long ago as it is on the tape
    auto when = juce::Time::getCurrentTime();
    if (m != nullptr)
        when -= juce::RelativeTime::seconds ((double) std::max<int64_t> (0, written() - m->sample) / sr);
    return when.formatted ("%H-%M-%S");
}

bool Keep::read (int64_t from, int n, bool itchOnly, juce::AudioBuffer<float>& dest, int destStart) const
{
    const juce::ScopedLock lock (storage);
    if (frames <= 0)
        return false;
    const auto& srcL = itchOnly ? itchL : allL;
    const auto& srcR = itchOnly ? itchR : allR;
    const auto margin = (int64_t) (safetySeconds * sr);
    dest.clear (destStart, n);

    const auto w = written();
    const int64_t lo = std::max (from, w - frames + margin), hi = std::min (from + n, w);
    for (int64_t f = lo; f < hi;)
    {
        const auto at = (size_t) (f % frames);
        const int run = (int) std::min<int64_t> (hi - f, frames - (int64_t) at);
        const int d = destStart + (int) (f - from);
        dest.copyFrom (0, d, srcL.data() + at, run);
        dest.copyFrom (1, d, srcR.data() + at, run);
        f += run;
    }
    // if the tape wrote over the start of what we copied meanwhile, that part is gone
    return written() - frames < lo;
}

juce::File Keep::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("dendrite").getChildFile ("kept");
}

juce::File Keep::write (const Take& t, juce::File folder)
{
    if (t.markId != 0)
        for (const auto& k : kept)
            if (k.id == t.markId && k.itchOnly == t.itchOnly && k.file.existsAsFile())
                return k.file;

    if (folder == juce::File())
        folder = defaultFolder();
    if (! folder.createDirectory())
        return {};

    juce::AudioBuffer<float> audio (2, (int) std::max<int64_t> (1, t.length));
    if (! read (t.from, (int) t.length, t.itchOnly, audio, 0))
        return {};

    for (int c = 0; c < 2; ++c)
    {
        auto* p = audio.getWritePointer (c);
        for (int i = 0; i < audio.getNumSamples(); ++i)
            if (! std::isfinite (p[i]))
                p[i] = 0.0f;
    }

    auto file = folder.getChildFile (juce::File::createLegalFileName (t.name) + ".wav");
    if (file.exists())
        file = file.getNonexistentSibling (false);

    // the tempo, the beats and the metre go into the ACID chunk, so a host can place it on its grid
    const int beats = (int) std::lround (t.bars * t.barBeats);
    const int numerator = std::max (1, (int) std::lround (t.barBeats));
    std::unordered_map<juce::String, juce::String> meta {
        { juce::WavAudioFormat::acidOneShot, "0" },   { juce::WavAudioFormat::acidRootSet, "0" },
        { juce::WavAudioFormat::acidStretch, "1" },   { juce::WavAudioFormat::acidDiskBased, "0" },
        { juce::WavAudioFormat::acidizerFlag, "1" },  { juce::WavAudioFormat::acidBeats, juce::String (beats) },
        { juce::WavAudioFormat::acidNumerator, juce::String (numerator) },
        { juce::WavAudioFormat::acidDenominator, "4" }, { juce::WavAudioFormat::acidTempo, juce::String (t.bpm, 3) },
    };

    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
            return {};
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                       .withSampleRate (sr)
                                                       .withNumChannels (2)
                                                       .withBitsPerSample (32)
                                                       .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint)
                                                       .withMetadataValues (meta));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
        {
            writer.reset();
            file.deleteFile();
            return {};
        }
    }
    if (t.markId != 0)
        kept.push_back ({ t.markId, t.itchOnly, file });
    return file;
}
} // namespace dendrite::keep
