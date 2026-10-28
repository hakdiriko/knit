// Offline render on dendrite's own DSP (Source/dsp), with a playing transport, so wear
// can be heard and measured outside a DAW (pedalboard's host never presses play).
//
//   g++ -O2 -std=c++20 -ISource/dsp tools/tape_render.cpp Source/dsp/Tape.cpp -o tape_render
//   ./tape_render "a.wav:32,b.wav:8,a.wav:16" out.wav bpm wear friction room [wear.csv]
//
// Inputs are 32-bit float WAVs (loops), each tiled for the given number of bars.
// wear.csv gets one line per bar: bar, tape age, 0, then the wear of each of the 64 cells.
#include "OutputChain.h"
#include "Room.h"
#include "Tape.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
using namespace dendrite::dsp;

struct Wav { int sr = 44100; std::vector<float> l, r; };

static Wav readWav (const std::string& path)
{
    std::ifstream f (path, std::ios::binary);
    std::vector<char> d ((std::istreambuf_iterator<char> (f)), {});
    Wav w; int chans = 2, bits = 32, fmt = 3; size_t p = 12;
    while (p + 8 <= d.size())
    {
        std::string id (d.data() + p, 4); uint32_t sz; std::memcpy (&sz, d.data() + p + 4, 4);
        if (id == "fmt ") { uint16_t a; std::memcpy (&a, d.data()+p+8, 2); fmt = a; std::memcpy (&a, d.data()+p+10, 2); chans = a;
                            std::memcpy (&w.sr, d.data()+p+12, 4); std::memcpy (&a, d.data()+p+22, 2); bits = a; }
        if (id == "data")
        {
            if (fmt == 0xFFFE) fmt = 3;
            const size_t frames = sz / (size_t) (chans * bits / 8);
            w.l.resize (frames); w.r.resize (frames);
            for (size_t i = 0; i < frames; ++i)
                for (int c = 0; c < chans && c < 2; ++c)
                {
                    float v; std::memcpy (&v, d.data() + p + 8 + (i * chans + c) * 4, 4);
                    (c == 0 ? w.l : w.r)[i] = v;
                    if (chans == 1) w.r[i] = v;
                }
        }
        p += 8 + sz + (sz & 1);
    }
    return w;
}

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr)
{
    std::ofstream f (path, std::ios::binary);
    uint32_t n = (uint32_t) l.size(), data = n * 8, riff = 36 + data, fmtsz = 16, srr = sr, br = sr * 8;
    uint16_t fmt = 3, ch = 2, ba = 8, bits = 32;
    f.write ("RIFF", 4); f.write ((char*) &riff, 4); f.write ("WAVEfmt ", 8); f.write ((char*) &fmtsz, 4);
    f.write ((char*) &fmt, 2); f.write ((char*) &ch, 2); f.write ((char*) &srr, 4); f.write ((char*) &br, 4);
    f.write ((char*) &ba, 2); f.write ((char*) &bits, 2); f.write ("data", 4); f.write ((char*) &data, 4);
    for (uint32_t i = 0; i < n; ++i) { f.write ((char*) &l[i], 4); f.write ((char*) &r[i], 4); }
}

int main (int argc, char** argv)
{
    if (argc < 7) { std::fprintf (stderr, "usage\n"); return 1; }
    const double bpm = std::atof (argv[3]);
    const float wear = std::atof (argv[4]), fric = std::atof (argv[5]), room_k = std::atof (argv[6]);
    std::map<std::string, Wav> cache;
    std::vector<float> L, R; int sr = 44100;
    std::stringstream ss (argv[1]); std::string item;
    while (std::getline (ss, item, ','))
    {
        const auto colon = item.rfind (':');
        const std::string file = item.substr (0, colon); const double bars = std::atof (item.substr (colon + 1).c_str());
        if (! cache.count (file)) cache[file] = readWav (file);
        const Wav& w = cache[file]; sr = w.sr;
        const size_t want = (size_t) (bars * 4 * 60.0 / bpm * sr);
        for (size_t i = 0; i < want; ++i) { L.push_back (w.l[i % w.l.size()]); R.push_back (w.r[i % w.r.size()]); }
    }
    // 4 s of silence for tails
    L.resize (L.size() + 4 * sr, 0.0f); R.resize (R.size() + 4 * sr, 0.0f);

    // Same chain as DendriteProcessor::processBlock (without bypass), on the plug-in's own DSP.
    Tape tape; Room room; tape.prepare ((float) sr, 1); room.prepare ((float) sr, 1);
    tape.setWear (wear); tape.setFriction (fric); room.set (room_k);
    const float ret = Room::returnGain (room_k), dir = Room::directGain (room_k);
    SafetyLimiter lim; lim.prepare ((float) sr);
    std::FILE* csv = argc > 7 ? std::fopen (argv[7], "w") : nullptr;
    const int block = 64; const double ppqPerSample = bpm / (60.0 * sr);
    int lastBar = -1;
    std::vector<float> lostL (block), lostR (block);
    for (size_t s = 0; s < L.size(); s += block)
    {
        const int n = (int) std::min<size_t> (block, L.size() - s);
        Tape::Transport tr; tr.ppq = ppqPerSample * (double) s; tr.bpm = bpm; tr.playing = true;
        tape.process (L.data() + s, R.data() + s, lostL.data(), lostR.data(), n, tr);
        for (int i = 0; i < n; ++i)
        {
            float rl, rr;
            room.process (Room::tapeSend * L[s + i] + Room::lostSend * lostL[(size_t) i], Room::tapeSend * R[s + i] + Room::lostSend * lostR[(size_t) i], rl, rr);
            float l = dir * L[s + i] + ret * rl, r = dir * R[s + i] + ret * rr;
            lim.process (l, r);
            L[s + i] = l; R[s + i] = r;
        }
        const int bar = (int) (ppqPerSample * s / 4.0);
        if (csv && bar != lastBar)
        {
            lastBar = bar;
            std::fprintf (csv, "%d,%.4f,0", bar, tape.getAge());
            for (int c = 0; c < Tape::cellsPerTape; ++c)
                std::fprintf (csv, ",%.4f", tape.getCellWear (c));
            std::fprintf (csv, "\n");
        }
    }
    if (csv) std::fclose (csv);
    writeWav (argv[2], L, R, sr);
    return 0;
}
