// Offline renderer for dendrite's engine (no JUCE, no host): reads a WAV, runs it through
// the same DSP the plug-in uses, with a playing transport at a given tempo, and writes a
// 32-bit float WAV. Used for sketches and measurements.
//
//   g++ -O2 -std=c++20 -I Source tools/render/render.cpp Source/dsp/*.cpp -o build/render
//   build/render in.wav out.wav [--feed 0.5] [--space 0.4] [--itch 7] [--bpm 120]
//                [--throw start:end (seconds, repeatable)] [--stopped] [--tail seconds]
//                [--sweep feed|space from to]  (moves a control linearly over the file)
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
    int itch = 7;
    double bpm = 120.0;
    bool playing = true;
    std::string sweep;
    std::vector<std::pair<double, double>> throws;
    for (int i = 3; i < argc; ++i)
    {
        const std::string k = argv[i];
        auto next = [&] { return i + 1 < argc ? argv[++i] : (char*) "0"; };
        if (k == "--feed") feed = std::stof (next());
        else if (k == "--space") space = std::stof (next());
        else if (k == "--itch") itch = std::stoi (next());
        else if (k == "--bpm") bpm = std::stod (next());
        else if (k == "--tail") tail = std::stof (next());
        else if (k == "--stopped") playing = false;
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

    Audio in;
    if (! readWav (argv[1], in))
    {
        std::fprintf (stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    const size_t extra = (size_t) (tail * (float) in.sampleRate);
    in.left.resize (in.left.size() + extra, 0.0f);
    in.right.resize (in.right.size() + extra, 0.0f);

    dendrite::Engine engine;
    engine.setControls (feed, space, false, itch, false);
    engine.prepare ((float) in.sampleRate);

    Audio out = in;
    const int block = 256;
    const size_t total = in.left.size();
    dendrite::HostTime time;
    time.bpm = bpm;
    time.playing = playing;
    const auto started = std::chrono::steady_clock::now();
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
        engine.setControls (f, s, held, itch, false);
        time.ppq = seconds * bpm / 60.0;
        engine.process (out.left.data() + pos, out.right.data() + pos, n, time);
    }
    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
    std::fprintf (stderr, "rendered %.1f s in %.2f s (%.0fx real time)\n", (double) total / in.sampleRate, elapsed,
                  (double) total / in.sampleRate / elapsed);
    return writeWav (argv[2], out) ? 0 : 1;
}
