// Offline renderer for dendrite's engine (no JUCE, no host): reads a WAV, runs it through
// the same DSP the plug-in uses, with a playing transport at a given tempo, and writes a
// 32-bit float WAV. Used for sketches and measurements.
//
//   g++ -O2 -std=c++20 -I Source tools/render/render.cpp Source/dsp/*.cpp Source/flies/flywire/FlyData.cpp -o build/render
//   build/render in.wav out.wav [--feed 0.5] [--space 0.4] [--itch 10] [--bpm 120]
//                [--throw start:end (seconds, repeatable)] [--stopped] [--tail seconds]
//                [--sweep feed|space from to]  (moves a control linearly over the file)
//                [--itch-off seconds]  (turns the itch off at that moment)
//                [--time 0|1|2]  (dotted, triplet, half)  [--dry 0|1]
//                [--probe heads.raw]  (also writes what each itch head played: float32,
//                                      interleaved left/right per head, for levelling them)
//                [--repeat N]  (plays the input N times in a row: a loop that goes on)
//                [--dice]  (1.2's itch: dice by song position, instead of the creature)
//                [--heads]  (2.0a: the creature fires the heads instead of the fly)
//                [--glow 0|1|2]  (what the itch throws: 2.5's ride on the echo, the afterglow, the spiral)
//                [--fly-log fly.csv]  (every 256 samples: what the fly does, its moods, memory, feeling)
//                [--still]  (the 2.1.0 fly: no restlessness, moods or learning)
//                [--loose]  (the 2.3 fly: moves off the grid, one sine lurch of the tape)
//                [--taste learn=0.06,forget=600,restless=10,jumpy=1,scratch=0.85,lurch=0.4,lean=0.35,calm=40]
//                         (the fly's temperament, for taste training; any subset)
//                [--hand]  (2.1 sketch: the itch as a motif on the echo's controls, not the heads)
//                [--touch touch.csv]  (sketch: seconds,speed,feed,space,band,bandHz,freeze,throwing,lift drive the echo; itch off)
//                [--hand-log hand.csv]  (every 256 samples: the hand's touch on the echo)
//                [--genome N]  (which creature; the plug-in draws one at random)
//                [--loop-transport]  (with --repeat: the song position jumps back at each
//                                     repeat, like Live's loop brace)
//                [--itch-log itches.csv]  (one row per itch: when, heads, the 14 numbers)
//                [--bar beats]  (quarter notes per bar, as the host says: 3 for 3/4, 3 for 6/8)
//                [--fly-dump dir]  (tools/fly/probe: every 50 ms, every neuron's spikes, the senses and
//                                   the outputs, as .npy files with a meta.json)
//                [--scramble seed]  (the fly with every non-plastic synapse strength shuffled)
//                [--seed N]  (the same fly with other chance: its kicks, readiness and sense spikes)
//                [--surrogate standin.txt]  (no brain: a linear filter from the senses to the outputs,
//                                            fitted by tools/fly/probe/brain_share.py fit)
//                [--denormals]  (let tiny numbers stay denormal, as renders did before 2026-11-16; the plug-in
//                                flushes them to zero, and so does the renderer by default now)
//                [--stats stats.json]  (the slowest audio block, against the block's own length)
//                out.wav may be "-" to write nothing
#include <sstream>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif
#include <array>
#include "dsp/Engine.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
struct Audio
{
    int sampleRate = 48000;
    std::vector<float> left, right;
};

bool readWav (const char* path, Audio& a)
{
    std::ifstream f (path, std::ios::binary);
    if (! f)
        return false;
    char riff[12];
    f.read (riff, 12);
    if (std::strncmp (riff, "RIFF", 4) != 0 || std::strncmp (riff + 8, "WAVE", 4) != 0)
        return false;
    int channels = 0, bits = 0, format = 0;
    while (f)
    {
        char id[4];
        uint32_t size = 0;
        f.read (id, 4);
        f.read ((char*) &size, 4);
        if (! f)
            break;
        if (std::strncmp (id, "fmt ", 4) == 0)
        {
            std::vector<char> b (size);
            f.read (b.data(), size);
            format = *(uint16_t*) &b[0];
            channels = *(uint16_t*) &b[2];
            a.sampleRate = (int) *(uint32_t*) &b[4];
            bits = *(uint16_t*) &b[14];
            if (format == 0xFFFE && size >= 26)
                format = *(uint16_t*) &b[24];
        }
        else if (std::strncmp (id, "data", 4) == 0)
        {
            std::vector<char> b (size);
            f.read (b.data(), size);
            const int bytes = bits / 8;
            const size_t frames = size / (size_t) (bytes * channels);
            a.left.resize (frames);
            a.right.resize (frames);
            for (size_t i = 0; i < frames; ++i)
                for (int c = 0; c < std::min (channels, 2); ++c)
                {
                    const char* p = &b[(i * (size_t) channels + (size_t) c) * (size_t) bytes];
                    float v = 0.0f;
                    if (format == 3 && bits == 32)
                        v = *(const float*) p;
                    else if (bits == 16)
                        v = (float) *(const int16_t*) p / 32768.0f;
                    else if (bits == 24)
                        v = (float) ((int32_t) ((uint32_t) (uint8_t) p[0] << 8 | (uint32_t) (uint8_t) p[1] << 16 | (uint32_t) (uint8_t) p[2] << 24) >> 8) / 8388608.0f;
                    else if (bits == 32)
                        v = (float) *(const int32_t*) p / 2147483648.0f;
                    (c == 0 ? a.left : a.right)[i] = v;
                }
            if (channels == 1)
                a.right = a.left;
            return true;
        }
        else
        {
            f.seekg (size + (size & 1), std::ios::cur);
        }
    }
    return false;
}

// a .npy file whose row count is written when it is closed
struct Npy
{
    std::FILE* f = nullptr;
    std::string descr;
    int cols = 0;
    long rows = 0;
    bool open (const std::string& path, const char* type, int columns)
    {
        f = std::fopen (path.c_str(), "wb");
        descr = type;
        cols = columns;
        header();
        return f != nullptr;
    }
    void header()
    {
        if (f == nullptr)
            return;
        std::string h = "{'descr': '" + descr + "', 'fortran_order': False, 'shape': (" + std::to_string (rows) + ", "
                      + std::to_string (cols) + "), }";
        h.resize (128 - 10 - 1, ' ');
        h += '\n';
        std::fwrite ("\x93NUMPY\x01\x00", 1, 8, f);
        const uint16_t len = (uint16_t) h.size();
        std::fwrite (&len, 2, 1, f);
        std::fwrite (h.data(), 1, h.size(), f);
    }
    void write (const void* data, size_t bytes)
    {
        std::fwrite (data, 1, bytes, f);
        ++rows;
    }
    void close()
    {
        if (f == nullptr)
            return;
        std::fseek (f, 0, SEEK_SET);
        header();
        std::fclose (f);
        f = nullptr;
    }
};

template <typename T, size_t N>
std::string jsonList (const T (&a)[N])
{
    std::string s = "[";
    for (size_t i = 0; i < N; ++i)
        s += (i ? "," : "") + std::to_string ((long long) a[i]);
    return s + "]";
}

bool writeWav (const char* path, const Audio& a)
{
    std::ofstream f (path, std::ios::binary);
    if (! f)
        return false;
    const uint32_t frames = (uint32_t) a.left.size(), dataBytes = frames * 8;
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    f.write ("RIFF", 4);
    u32 (36 + dataBytes);
    f.write ("WAVEfmt ", 8);
    u32 (16);
    u16 (3);
    u16 (2);
    u32 ((uint32_t) a.sampleRate);
    u32 ((uint32_t) a.sampleRate * 8);
    u16 (8);
    u16 (32);
    f.write ("data", 4);
    u32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
    {
        f.write ((const char*) &a.left[i], 4);
        f.write ((const char*) &a.right[i], 4);
    }
    return true;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: render in.wav out.wav [options]\n");
        return 1;
    }
    float feed = 0.5f, space = 0.4f, tail = 0.0f, sweepFrom = 0.0f, sweepTo = 0.0f;
    int itch = dendrite::Engine::defaultFrame;
    float itchOff = 1.0e9f;
    int timing = 0;
    bool dry = true;
    double bpm = 120.0;
    bool playing = true;
    std::string sweep, probe, itchLog, handLog, touchFile;
    int repeat = 1;
    bool dice = false, loopTransport = false, handMode = false, headsMode = false;
    std::string flyLog;
    int glowMode = -1;
    bool still = false;
    std::string tasteSpec;
    bool loose = false;
    size_t onePass = 0;
    unsigned long long genome = 1;
    std::vector<std::pair<double, double>> throws;
    double barBeats = 4.0;
    std::string dumpDir, surrogateFile, statsFile;
    long long scrambleSeed = -1, flySeed = -1;
    bool denormals = false;
    for (int i = 3; i < argc; ++i)
    {
        const std::string k = argv[i];
        auto next = [&] { return i + 1 < argc ? argv[++i] : (char*) "0"; };
        if (k == "--feed") feed = std::stof (next());
        else if (k == "--space") space = std::stof (next());
        else if (k == "--itch") itch = std::stoi (next());
        else if (k == "--itch-off") itchOff = std::stof (next());
        else if (k == "--time") timing = std::stoi (next());
        else if (k == "--dry") dry = std::stoi (next()) != 0;
        else if (k == "--bpm") bpm = std::stod (next());
        else if (k == "--tail") tail = std::stof (next());
        else if (k == "--stopped") playing = false;
        else if (k == "--probe") probe = next();
        else if (k == "--repeat") repeat = std::max (1, std::stoi (next()));
        else if (k == "--dice") dice = true;
        else if (k == "--hand") handMode = true;
        else if (k == "--heads") headsMode = true;
        else if (k == "--fly-log") flyLog = next();
        else if (k == "--glow") glowMode = std::stoi (next());
        else if (k == "--still") still = true;
        else if (k == "--taste") tasteSpec = next();
        else if (k == "--loose") loose = true;
        else if (k == "--hand-log") handLog = next();
        else if (k == "--touch") touchFile = next();
        else if (k == "--loop-transport") loopTransport = true;
        else if (k == "--genome") genome = std::stoull (next());
        else if (k == "--itch-log") itchLog = next();
        else if (k == "--bar") barBeats = std::stod (next());
        else if (k == "--fly-dump") dumpDir = next();
        else if (k == "--scramble") scrambleSeed = std::stoll (next());
        else if (k == "--seed") flySeed = std::stoll (next());
        else if (k == "--surrogate") surrogateFile = next();
        else if (k == "--stats") statsFile = next();
        else if (k == "--denormals") denormals = true;
        else if (k == "--throw")
        {
            const std::string v = next();
            const auto colon = v.find (':');
            throws.push_back ({ std::stod (v.substr (0, colon)), std::stod (v.substr (colon + 1)) });
        }
        else if (k == "--sweep")
        {
            sweep = next();
            sweepFrom = std::stof (next());
            sweepTo = std::stof (next());
        }
    }

#if defined(__SSE__) || defined(_M_X64)
    // as the plug-in (juce::ScopedNoDenormals): tails decaying toward silence would otherwise run
    // many times slower than in a host, and the timing would measure the CPU's denormal penalty
    if (! denormals)
        _mm_setcsr (_mm_getcsr() | 0x8040); // flush to zero, denormals are zero
#endif
    Audio in;
    if (! readWav (argv[1], in))
    {
        std::fprintf (stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    onePass = in.left.size();
    if (repeat > 1)
    {
        const auto l = in.left, r = in.right;
        for (int k = 1; k < repeat; ++k)
        {
            in.left.insert (in.left.end(), l.begin(), l.end());
            in.right.insert (in.right.end(), r.begin(), r.end());
        }
    }
    const size_t extra = (size_t) (tail * (float) in.sampleRate);
    in.left.resize (in.left.size() + extra, 0.0f);
    in.right.resize (in.right.size() + extra, 0.0f);

    static dendrite::Engine engine; // large: keep it off the stack
    engine.creature().beBorn (genome);
    engine.useDice (dice);
    engine.useHand (handMode);
    engine.useFly (! handMode && ! headsMode && touchFile.empty());
    engine.itchFly().setInnerLife (! still);
    engine.itchFly().setMusical (! loose);
    {
        auto t = engine.itchFly().getTaste();
        std::stringstream spec (tasteSpec);
        for (std::string item; std::getline (spec, item, ',');)
        {
            const auto eq = item.find ('=');
            if (eq == std::string::npos)
                continue;
            const auto key = item.substr (0, eq);
            const float value = std::stof (item.substr (eq + 1));
            float* field = key == "learn" ? &t.learn : key == "forget" ? &t.forget : key == "restless" ? &t.restless
                         : key == "jumpy" ? &t.jumpy : key == "scratch" ? &t.scratch : key == "lurch" ? &t.lurch
                         : key == "eyesTire" ? &t.eyesTire : key == "eyesBack" ? &t.eyesBack : key == "lean" ? &t.lean : key == "calm" ? &t.calm : nullptr;
            if (field == nullptr)
            {
                std::fprintf (stderr, "unknown taste: %s\n", key.c_str());
                return 1;
            }
            *field = value;
        }
        engine.itchFly().setTaste (t);
    }
    auto& fly = engine.itchFly();
    if (scrambleSeed >= 0)
        fly.scramble ((uint64_t) scrambleSeed);
    if (flySeed >= 0)
        fly.reseed (0x9E3779B97F4A7C15ull * (uint64_t) (flySeed + 1)); // never zero (xorshift)
    static dendrite::Fly::StandIn standIn;
    if (! surrogateFile.empty())
    {
        std::FILE* sf = std::fopen (surrogateFile.c_str(), "r");
        int got = 0;
        for (auto& row : standIn.w)
            for (auto& x : row)
                got += sf != nullptr && std::fscanf (sf, "%f", &x) == 1 ? 1 : 0;
        if (sf)
            std::fclose (sf);
        if (got != (int) (standIn.w.size() * standIn.w[0].size()))
        {
            std::fprintf (stderr, "surrogate: %s has %d numbers, wants %zu\n", surrogateFile.c_str(), got,
                          standIn.w.size() * standIn.w[0].size());
            return 1;
        }
        fly.standIn (&standIn);
    }
    Npy spikesNpy, sensesNpy, heardNpy, outNpy;
    constexpr int outCols = 14;
    if (! dumpDir.empty())
    {
        fly.setProbing (true);
        const auto at = [&] (const char* name) { return dumpDir + "/" + name; };
        if (! spikesNpy.open (at ("spikes.npy"), "|u1", dendrite::Fly::neurons))
        {
            std::fprintf (stderr, "cannot write into %s (make the folder first)\n", dumpDir.c_str());
            return 1;
        }
        sensesNpy.open (at ("senses.npy"), "<f4", dendrite::Fly::inputCount);
        heardNpy.open (at ("heard.npy"), "<f4", dendrite::Fly::inputCount);
        outNpy.open (at ("outputs.npy"), "<f4", outCols);
    }
    std::FILE* flog = flyLog.empty() ? nullptr : std::fopen (flyLog.c_str(), "w");
    engine.setControls (feed, space, false, itch, false);
    if (glowMode >= 0)
        engine.setTail (glowMode);
    engine.prepare ((float) in.sampleRate);
    std::FILE* log = itchLog.empty() ? nullptr : std::fopen (itchLog.c_str(), "w");
    if (log)
    {
        std::fprintf (log, "seconds,ppq,h1,h2,h3,intensity,boredom,threshold,familiarity,arousal,brightness,density");
        for (int d = 0; d < dendrite::Gesture::dims; ++d)
            std::fprintf (log, ",u%d", d);
        std::fprintf (log, "\n");
    }
    int logged = 0;
    std::FILE* hlog = handLog.empty() ? nullptr : std::fopen (handLog.c_str(), "w");
    if (hlog)
        std::fprintf (hlog, "seconds,env,speed,feed,space,band,bandHz,freeze\n");

    Audio out = in;
    std::vector<std::array<float, 9>> touchRows;
    if (! touchFile.empty())
    {
        std::FILE* tf = std::fopen (touchFile.c_str(), "r");
        std::array<float, 9> r {};
        while (tf && std::fscanf (tf, "%f,%f,%f,%f,%f,%f,%f,%f,%f", &r[0], &r[1], &r[2], &r[3], &r[4], &r[5], &r[6], &r[7], &r[8]) == 9)
            touchRows.push_back (r);
        if (tf)
            std::fclose (tf);
        itch = 0;
        std::fprintf (stderr, "touch: %zu rows\n", touchRows.size());
    }
    const int block = ! probe.empty() ? 1 : ! dumpDir.empty() ? std::max (1, (int) std::lround (in.sampleRate * 0.005)) : touchRows.empty() ? 256 : 32;
    int blocksInBin = 0, binSamples = 0;
    double slowest = 0.0, slowestLate = 0.0, spent = 0.0;
    long blocks = 0;
    std::vector<float> loads;
    loads.reserve (in.left.size() / (size_t) block + 1);
    std::ofstream probeFile;
    if (! probe.empty())
        probeFile.open (probe, std::ios::binary);
    const size_t total = in.left.size();
    dendrite::HostTime time;
    time.bpm = bpm;
    time.playing = playing;
    time.barBeats = barBeats;
    const auto started = std::chrono::steady_clock::now();
    int itches = 0;
    bool itching = false;
    std::string itchTimes;
    for (size_t pos = 0; pos < total; pos += block)
    {
        const int n = (int) std::min ((size_t) block, total - pos);
        const double seconds = (double) pos / in.sampleRate;
        bool held = false;
        for (auto& t : throws)
            held = held || (seconds >= t.first && seconds < t.second);
        float f = feed, s = space;
        const float progress = (float) pos / (float) total;
        if (sweep == "feed") f = sweepFrom + (sweepTo - sweepFrom) * progress;
        if (sweep == "space") s = sweepFrom + (sweepTo - sweepFrom) * progress;
        engine.setControls (f, s, held, seconds >= itchOff ? 0 : itch, false);
        engine.setStyle (timing, dry);
        time.ppq = (loopTransport && pos < onePass * (size_t) repeat ? (double) (pos % onePass) / in.sampleRate : seconds) * bpm / 60.0;
        if (! touchRows.empty())
        {
            size_t lo = 0, hi = touchRows.size() - 1;
            while (hi - lo > 1)
            {
                const size_t mid = (lo + hi) / 2;
                (touchRows[mid][0] <= seconds ? lo : hi) = mid;
            }
            const auto& a = touchRows[lo];
            const auto& b = touchRows[hi];
            const float w = b[0] > a[0] ? std::clamp ((float) ((seconds - a[0]) / (b[0] - a[0])), 0.0f, 1.0f) : 0.0f;
            auto at = [&] (int c) { return a[(size_t) c] + w * (b[(size_t) c] - a[(size_t) c]); };
            dendrite::Touch t;
            t.speed = at (1); t.feed = at (2); t.space = at (3); t.band = at (4); t.bandHz = at (5);
            t.freeze = at (6); t.throwing = at (7) > 0.5f; t.lift = at (8);
            engine.touchFromOutside (true, t);
        }
        const auto blockStart = std::chrono::steady_clock::now();
        engine.process (out.left.data() + pos, out.right.data() + pos, n, time);
        {
            const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - blockStart).count() / n;
            slowest = std::max (slowest, took);
            if (seconds >= 1.0)
            {
                slowestLate = std::max (slowestLate, took);
                loads.push_back ((float) (took * in.sampleRate));
            }
            spent += took * n;
            ++blocks;
        }
        binSamples += n;
        if (! dumpDir.empty() && ++blocksInBin == 10)
        {
            blocksInBin = 0;
            const auto& p = fly.probe();
            const float steps = (float) std::max (1, p.steps);
            std::vector<uint8_t> row ((size_t) dendrite::Fly::neurons);
            for (int k = 0; k < dendrite::Fly::neurons; ++k)
                row[(size_t) k] = (uint8_t) std::min<int> (255, fly.takeSpikes (k));
            spikesNpy.write (row.data(), row.size());
            std::array<float, dendrite::Fly::inputCount> a {};
            for (int k = 0; k < dendrite::Fly::inputCount; ++k)
                a[(size_t) k] = p.drive[(size_t) k] / steps;
            sensesNpy.write (a.data(), sizeof (a));
            for (int k = 0; k < dendrite::Fly::inputCount; ++k)
                a[(size_t) k] = p.heard[(size_t) k] / steps;
            heardNpy.write (a.data(), sizeof (a));
            const auto& d = engine.view().fly;
            const double endSec = (double) (pos + (size_t) n) / in.sampleRate;
            const float o[outCols] = { (float) endSec, (float) (time.ppq + (double) n / in.sampleRate * bpm / 60.0),
                                       p.escape / steps, p.groom / steps, p.lean / steps, p.excited / steps, d.feeling, d.fear,
                                       d.arousal, d.fatigue, (float) d.move, d.jump, d.scratch, d.throwing ? 1.0f : 0.0f };
            outNpy.write (o, sizeof (o));
            fly.clearProbe();
            binSamples = 0;
        }
        if (flog != nullptr && pos % 256 == 0)
        {
            const auto& d = engine.view().fly;
            std::fprintf (flog, "%.4f,%.3f,%.3f,%d,%d,%.3f,%.1f,%.3f,%.3f,%.3f,%.4f,%.4f,%.3f,%d\n", seconds, d.jump, d.semitones,
                          d.throwing ? 1 : 0, d.frozen ? 1 : 0, d.scratch, d.scratchHz, d.turn, d.arousal, d.fatigue,
                          d.memory, d.feeling, d.fear, d.move);
        }
        if (engine.view().itching && ! itching)
        {
            ++itches;
            itchTimes += " " + std::to_string ((int) (seconds * 10.0 + 0.5) / 10) + "." + std::to_string ((int) (seconds * 10.0 + 0.5) % 10);
        }
        itching = engine.view().itching;
        const auto& rec = engine.lastItch();
        if (log && rec.count > logged)
        {
            logged = rec.count;
            std::fprintf (log, "%.3f,%.3f,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f", seconds, rec.ppq, rec.gesture.order[0],
                          rec.gesture.order[1], rec.gesture.order[2], rec.intensity, rec.boredom, rec.threshold,
                          rec.familiarity, rec.arousal, rec.brightness, rec.density);
            for (float v : rec.gesture.u)
                std::fprintf (log, ",%.4f", v);
            std::fprintf (log, "\n");
        }
        if (hlog)
        {
            const auto& t = engine.itchHand().now();
            std::fprintf (hlog, "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.1f,%.4f\n", seconds, engine.itchHand().envelope(), t.speed,
                          t.feed, t.space, t.band, t.bandHz, t.freeze);
        }
        if (probeFile)
        {
            const auto& heads = engine.itchHeads();
            for (int h = 0; h < dendrite::Heads::count; ++h)
            {
                probeFile.write ((const char*) &heads.probeL[(size_t) h], 4);
                probeFile.write ((const char*) &heads.probeR[(size_t) h], 4);
            }
        }
    }
    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
    std::fprintf (stderr, "itches: %d (at%s s)\n", itches, itchTimes.c_str());
    if (! dumpDir.empty())
    {
        spikesNpy.close();
        sensesNpy.close();
        heardNpy.close();
        outNpy.close();
        namespace fd = dendrite::flydata;
        std::ofstream m (dumpDir + "/meta.json");
        m << "{\"sampleRate\": " << in.sampleRate << ", \"bin\": " << (double) (10 * block) / in.sampleRate << ", \"bpm\": " << bpm
          << ", \"barBeats\": " << barBeats << ", \"loopSeconds\": " << (double) onePass / in.sampleRate
          << ", \"repeat\": " << repeat << ", \"scramble\": " << scrambleSeed << ", \"seed\": " << flySeed << ", \"surrogate\": " << (surrogateFile.empty() ? "false" : "true")
          << ",\n \"outputs\": [\"seconds\", \"beat\", \"escape\", \"groom\", \"lean\", \"excited\", \"feeling\", \"fear\", \"arousal\", \"fatigue\", \"move\", \"jump\", \"scratch\", \"throwing\"]"
          << ",\n \"senseStart\": " << jsonList (fd::senseStart) << ",\n \"kenyon\": " << jsonList (fd::kenyon)
          << ",\n \"mbon\": " << jsonList (fd::mbon) << ",\n \"mbonSign\": " << jsonList (fd::mbonSign)
          << ",\n \"dan\": " << jsonList (fd::dan) << ",\n \"ppl1\": " << jsonList (fd::ppl1)
          << ",\n \"escape\": " << jsonList (fd::escapeNeuron) << ",\n \"groom\": " << jsonList (fd::legNeuron)
          << ",\n \"left\": " << jsonList (fd::leftNeuron) << ",\n \"right\": " << jsonList (fd::rightNeuron) << "}\n";
    }
    if (! statsFile.empty())
    {
        // seconds of work per second of audio: 1.0 would be one whole core
        std::ofstream st (statsFile);
        st << "{\"block\": " << block << ", \"sampleRate\": " << in.sampleRate << ", \"blocks\": " << blocks
           << ", \"meanLoad\": " << spent / ((double) total / in.sampleRate)
           << ", \"slowestLoad\": " << slowest * in.sampleRate << ", \"slowestLoadAfter1s\": " << slowestLate * in.sampleRate;
        // one block in a thousand slower than this (a slowest block alone can be the machine, not the fly)
        if (! loads.empty())
        {
            const size_t at = std::min (loads.size() - 1, loads.size() * 999 / 1000);
            std::nth_element (loads.begin(), loads.begin() + (long) at, loads.end());
            st << ", \"load999\": " << loads[at];
        }
        st << "}\n";
    }
    std::fprintf (stderr, "rendered %.1f s in %.2f s (%.0fx real time)\n", (double) total / in.sampleRate, elapsed,
                  (double) total / in.sampleRate / elapsed);
    if (hlog)
        std::fclose (hlog);
    if (log)
        std::fclose (log);
    if (std::string (argv[2]) == "-")
        return 0;
    return writeWav (argv[2], out) ? 0 : 1;
}
