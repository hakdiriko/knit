// The fly's material matrix (Phase 1 of the plan): every case a host can throw at it, driven
// straight through the Engine with a fake transport, no JUCE. Each case checks that the output
// stays finite and bounded, that the fly's moves start on the song's 16th grid, that it moves at
// all where there is music and lets silence be silent, and how long the slowest block took (its
// thread's CPU time against the block's own length; with blocks of 32 the brain's 10 ms
// bookkeeping lands in one block, so read the cost from the larger blocks).
//
//   g++ -O3 -march=x86-64-v2 -std=c++20 -I Source tools/fly/matrix.cpp Source/dsp/*.cpp Source/flies/flywire/FlyData.cpp -o build/flymatrix
//   build/flymatrix [--quick] [--only name]
//
// The material is made here: a kick on the beat (and the bar's accents), hats on the 16ths, a
// bass, and chords; or the chords alone with rests (the bare keys). Exit code 1 if any case fails.
#include "dsp/Engine.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif
#include <string>
#include <vector>

using namespace dendrite;

namespace
{
// the time this thread spent working (not the wall clock: a shared machine pauses us now and then,
// and that is the machine's slowness, not the plug-in's)
double cpuNow()
{
    timespec ts {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double) ts.tv_sec + 1.0e-9 * (double) ts.tv_nsec;
}

struct Part
{
    double seconds = 60.0, bpm = 120.0, barBeats = 4.0;
    bool eighths = false;  // 6/8: the beat is a dotted quarter (accent every three 8ths)
    bool keysOnly = false; // chords with rests, no drums
    bool silent = false;
    bool playing = true;
    double loopBars = 0.0; // > 0: the song position jumps back every so many bars (a loop brace)
};

struct Case
{
    std::string name;
    float sampleRate = 48000.0f;
    std::vector<Part> parts;
    bool mono = false, oneSide = false;
    bool ragged = false; // block sizes change every block, as some hosts do
    int block = 32;
};

struct Result
{
    bool finite = true;
    float peak = 0.0f;
    int moves = 0, offGrid = 0, movesInSilence = 0;
    float silenceTail = 0.0f; // the output's RMS over the last 5 s of the last silent part, dBFS
    double worstBlockShare = 0.0, p999Share = 0.0, meanShare = 0.0; // block's time against the block's own length
    std::vector<int> movesPerPart;
};

// the music, one sample at a time; t in seconds into the part, beat in quarter notes
struct Synth
{
    float sr = 48000.0f;
    std::mt19937 rng { 7 };
    std::uniform_real_distribution<float> noise { -1.0f, 1.0f };
    float hp = 0.0f, lastN = 0.0f;
    void sample (const Part& p, double beat, float& l, float& r)
    {
        l = r = 0.0f;
        if (p.silent)
            return;
        const double per = p.eighths ? 0.5 : 1.0; // the pulse, in quarter notes (6/8 counts 8ths)
        const double bar = std::fmod (beat, p.barBeats);
        const double sixteenth = std::fmod (beat, 0.25) * 60.0 / p.bpm; // seconds since the last 16th
        const double sincePulse = std::fmod (beat, per) * 60.0 / p.bpm;
        const int pulseInBar = (int) std::floor (bar / per);
        const int pulsesPerBar = (int) std::lround (p.barBeats / per);
        const bool accent = p.eighths ? pulseInBar % 3 == 0 : pulseInBar == 0;
        float x = 0.0f;
        if (! p.keysOnly)
        {
            // kick: on the accents (and every beat in 4/4 and 3/4)
            if (accent || (! p.eighths && pulseInBar < pulsesPerBar))
            {
                const double tk = sincePulse;
                if (tk < 0.25)
                    x += 0.7f * (float) (std::sin (2.0 * M_PI * (50.0 * tk + 60.0 * (1.0 - std::exp (-tk * 30.0)) / 30.0)) * std::exp (-tk * 12.0));
            }
            // hats on every 16th, the off-beats louder
            const float n = noise (rng);
            hp = 0.9f * (hp + n - lastN);
            lastN = n;
            const bool off = std::fmod (beat, 0.5) >= 0.25;
            x += (off ? 0.18f : 0.08f) * hp * (float) std::exp (-sixteenth * 60.0);
            // bass on the pulse
            x += 0.2f * (float) (std::sin (2.0 * M_PI * 55.0 * sincePulse) * std::exp (-sincePulse * 4.0));
        }
        // chords: one per bar, held for half of it, then a rest (A minor, F, C, G)
        static const float roots[4] = { 220.0f, 174.61f, 261.63f, 196.0f };
        const int barNo = (int) std::floor (beat / p.barBeats);
        const double inBar = bar * 60.0 / p.bpm, barSec = p.barBeats * 60.0 / p.bpm;
        if (inBar < 0.5 * barSec)
        {
            const float f = roots[barNo % 4];
            const float env = (float) (std::min (1.0, inBar * 40.0) * std::exp (-inBar * 1.5));
            for (const float ratio : { 1.0f, 1.189f, 1.498f })
                x += 0.08f * env * (float) std::sin (2.0 * M_PI * f * ratio * inBar);
        }
        l = x;
        r = x * 0.9f + 0.1f * (float) std::sin (2.0 * M_PI * 0.3 * beat); // a little width
    }
};

Result run (const Case& c)
{
    Result res;
    Engine engine;
    engine.setControls (0.5f, 0.4f, false, Engine::defaultFrame, false);
    engine.prepare (c.sampleRate);
    Synth synth;
    synth.sr = c.sampleRate;
    std::mt19937 blocks { 11 };
    std::vector<float> L (4096), R (4096);
    int lastMove = 0;
    double silentSum = 0.0;
    long silentCount = 0;
    double shareSum = 0.0;
    long blocksCount = 0;
    std::vector<float> shares;
    for (size_t pi = 0; pi < c.parts.size(); ++pi)
    {
        const Part& p = c.parts[pi];
        const long total = (long) (p.seconds * c.sampleRate);
        int movesHere = 0;
        double beat = 0.0; // the song position in this part
        silentSum = 0.0;
        silentCount = 0;
        for (long pos = 0; pos < total;)
        {
            int n = c.block;
            if (c.ragged)
                n = 1 + (int) (blocks() % 1024);
            n = (int) std::min<long> (n, total - pos);
            // the loop brace: the song position jumps back
            if (p.loopBars > 0.0 && beat >= p.loopBars * p.barBeats)
                beat -= p.loopBars * p.barBeats;
            const double blockBeat = beat;
            const double perSample = p.bpm / 60.0 / c.sampleRate;
            for (int i = 0; i < n; ++i)
            {
                float l, r;
                synth.sample (p, beat + perSample * i, l, r);
                if (c.mono)
                    r = l;
                if (c.oneSide)
                    r = 0.0f;
                L[(size_t) i] = l;
                R[(size_t) i] = r;
            }
            HostTime time;
            time.bpm = p.bpm;
            time.barBeats = p.barBeats;
            time.playing = p.playing;
            time.ppq = blockBeat;
            const double t0 = cpuNow();
            engine.process (L.data(), R.data(), n, time);
            const double took = cpuNow() - t0;
            const double share = took / ((double) n / c.sampleRate);
            if (pos > (long) c.sampleRate) // after the first second (allocation-free warm-up)
            {
                res.worstBlockShare = std::max (res.worstBlockShare, share);
                shares.push_back ((float) share);
            }
            shareSum += took;
            blocksCount += n;
            for (int i = 0; i < n; ++i)
            {
                const float a = L[(size_t) i], b = R[(size_t) i];
                if (! std::isfinite (a) || ! std::isfinite (b))
                    res.finite = false;
                res.peak = std::max (res.peak, std::max (std::abs (a), std::abs (b)));
                if (p.silent && pos + i > total - (long) (5.0f * c.sampleRate))
                {
                    silentSum += (double) (a * a + b * b) * 0.5;
                    ++silentCount;
                }
            }
            // a move starts: where in the 16th is the song now? (the block is short, so its start
            // is the move's start within a block's length)
            const int move = engine.view().fly.move;
            if (move != 0 && move != lastMove)
            {
                ++res.moves;
                ++movesHere;
                if (p.silent)
                    ++res.movesInSilence;
                if (p.playing)
                {
                    // the move began somewhere in this block; the nearest 16th line to any
                    // sample of the block must be within the block plus 10 ms
                    const double sixteenths = blockBeat * 4.0, span = perSample * n * 4.0;
                    const double line = std::ceil (sixteenths - 1e-9);
                    const double slack = 0.010 * p.bpm / 60.0 * 4.0;
                    if (line > sixteenths + span + slack && sixteenths - std::floor (sixteenths) > slack)
                        ++res.offGrid;
                }
            }
            lastMove = move;
            beat += perSample * n;
            pos += n;
        }
        res.movesPerPart.push_back (movesHere);
        if (p.silent && silentCount > 0)
            res.silenceTail = (float) (10.0 * std::log10 (silentSum / (double) silentCount + 1e-20));
    }
    res.meanShare = shareSum / ((double) blocksCount / c.sampleRate);
    if (! shares.empty())
    {
        std::sort (shares.begin(), shares.end());
        res.p999Share = shares[(size_t) (0.999 * (double) (shares.size() - 1))];
    }
    return res;
}
} // namespace

int main (int argc, char** argv)
{
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr (_mm_getcsr() | 0x8040); // flush denormals to zero, as the plug-in does (JUCE's ScopedNoDenormals)
#endif
    bool quick = false;
    std::string only;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--quick") == 0)
            quick = true;
        else if (std::strcmp (argv[i], "--only") == 0 && i + 1 < argc)
            only = argv[++i];
    }
    const double len = quick ? 40.0 : 90.0;
    auto groove = [&] (double bpm, double bar = 4.0, bool eighths = false) {
        Part p;
        p.seconds = len;
        p.bpm = bpm;
        p.barBeats = bar;
        p.eighths = eighths;
        return p;
    };
    auto keys = [&] (double bpm) {
        Part p = groove (bpm);
        p.keysOnly = true;
        return p;
    };
    auto silence = [&] (double s) {
        Part p;
        p.seconds = s;
        p.silent = true;
        return p;
    };
    std::vector<Case> cases;
    for (const double bpm : { 70.0, 100.0, 127.0, 140.0, 174.0 })
        cases.push_back ({ "groove " + std::to_string ((int) bpm) + " bpm", 48000.0f, { groove (bpm) } });
    cases.push_back ({ "keys 70 bpm", 48000.0f, { keys (70.0) } });
    cases.push_back ({ "keys 127 bpm", 48000.0f, { keys (127.0) } });
    cases.push_back ({ "keys 174 bpm", 48000.0f, { keys (174.0) } });
    cases.push_back ({ "3/4 at 120", 48000.0f, { groove (120.0, 3.0) } });
    cases.push_back ({ "6/8 at 120", 48000.0f, { groove (120.0, 3.0, true) } });
    cases.push_back ({ "7/8 at 140", 48000.0f, { groove (140.0, 3.5, true) } });
    {
        Part a = groove (120.0), b = groove (90.0), c2 = groove (160.0);
        a.seconds = b.seconds = c2.seconds = len / 2.0;
        cases.push_back ({ "tempo change 120 > 90 > 160", 48000.0f, { a, b, c2 } });
    }
    {
        Part p = groove (127.0);
        p.loopBars = 4.0;
        cases.push_back ({ "loop jump every 4 bars", 48000.0f, { p } });
        Part q = groove (127.0);
        q.loopBars = 1.5;
        cases.push_back ({ "loop jump off the bar (1.5 bars)", 48000.0f, { q } });
    }
    {
        Part p = groove (127.0);
        p.playing = false;
        cases.push_back ({ "Live stopped", 48000.0f, { p } });
    }
    cases.push_back ({ "silence, music, silence", 48000.0f, { silence (20.0), groove (127.0), silence (30.0) } });
    {
        Case m { "mono", 48000.0f, { groove (127.0) } };
        m.mono = true;
        cases.push_back (m);
        Case o { "one side only", 48000.0f, { groove (127.0) } };
        o.oneSide = true;
        cases.push_back (o);
    }
    for (const float sr : { 44100.0f, 88200.0f, 96000.0f, 192000.0f })
        cases.push_back ({ "groove 127 at " + std::to_string ((int) sr) + " Hz", sr, { groove (127.0) } });
    {
        Case r { "ragged blocks (1 to 1024)", 48000.0f, { groove (127.0) } };
        r.ragged = true;
        cases.push_back (r);
        for (const int size : { 128, 512, 1024 })
        {
            Case b { "blocks of " + std::to_string (size), 48000.0f, { groove (127.0) } };
            b.block = size;
            cases.push_back (b);
        }
        // the high rates as a host runs them: a 256-sample buffer is 2.7 ms at 96 kHz, 1.3 ms at 192 kHz
        for (const float sr : { 96000.0f, 192000.0f })
        {
            Case h { "blocks of 256 at " + std::to_string ((int) sr) + " Hz", sr, { groove (127.0) } };
            h.block = 256;
            cases.push_back (h);
        }
    }

    int failed = 0;
    std::printf ("%-36s %6s %6s %5s %5s %8s %8s %8s %8s  %s\n", "case", "peak", "moves", "off", "quiet", "tail dB", "worst %", "99.9 %", "mean %", "verdict");
    for (const auto& c : cases)
    {
        if (! only.empty() && c.name.find (only) == std::string::npos)
            continue;
        const Result r = run (c);
        std::string why;
        if (! r.finite)
            why += " not finite;";
        if (r.peak > 1.0f)
            why += " peak over 0 dBFS;";
        if (r.offGrid > 0 && c.block <= 64 && ! c.ragged)
            why += " moves off the grid;";
        for (size_t k = 0; k < c.parts.size(); ++k)
            if (! c.parts[k].silent && c.parts[k].seconds >= 30.0 && r.movesPerPart[k] == 0)
                why += " no move in part " + std::to_string (k + 1) + ";";
        if (r.movesInSilence > 0)
            why += " moves in silence;";
        bool hasSilence = false;
        for (const auto& p : c.parts)
            hasSilence = hasSilence || p.silent;
        if (hasSilence && r.silenceTail > -90.0f)
            why += " silence not silent;";
        std::printf ("%-36s %6.2f %6d %5d %5d %8.1f %8.1f %8.1f %8.1f  %s\n", c.name.c_str(), r.peak, r.moves, r.offGrid, r.movesInSilence,
                     hasSilence ? r.silenceTail : 0.0f, 100.0 * r.worstBlockShare, 100.0 * r.p999Share, 100.0 * r.meanShare, why.empty() ? "ok" : ("FAIL" + why).c_str());
        failed += why.empty() ? 0 : 1;
    }
    std::printf ("%s\n", failed == 0 ? "PASS" : (std::to_string (failed) + " FAIL").c_str());
    return failed == 0 ? 0 : 1;
}
