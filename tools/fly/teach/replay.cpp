// Replays a teach session natively, through the same rig the teach page runs in WebAssembly, so
// a take can be heard again (and checked: the moves must start on the same beats the page logged).
// Normally run through tools/fly/teach/teach.py replay, which does the WAV and JSON parts.
//
//   g++ -O2 -std=c++20 -I Source -I tools/fly/teach tools/fly/teach/replay.cpp Source/dsp/*.cpp Source/flies/flywire/FlyData.cpp -o build/teach_replay
//   build/teach_replay sampleRate bpm barBeats frames loop.f32 events.txt out.f32
//
// loop.f32: one pass of the loop, float32, all left then all right. events.txt: one per line,
// "sample action value...", sorted, every sample a multiple of 128 (the page's render quantum):
//   play 0|1, cue <move>, throw 0|1, freeze 0|1, controls <feed> <space> <time>
// out.f32: frames of output, float32, left/right interleaved. stdout: "start <beat> <move>" per move.
#include "rig.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main (int argc, char** argv)
{
    if (argc != 8)
    {
        std::fprintf (stderr, "usage: teach_replay sampleRate bpm barBeats frames loop.f32 events.txt out.f32\n");
        return 2;
    }
    const float sr = std::stof (argv[1]);
    const double bpm = std::stod (argv[2]), bar = std::stod (argv[3]);
    const long long frames = std::stoll (argv[4]);

    std::vector<float> loop;
    {
        std::ifstream again (argv[5], std::ios::binary | std::ios::ate);
        const auto bytes = (size_t) again.tellg();
        loop.assign (bytes / 4, 0.0f);
        again.seekg (0);
        again.read ((char*) loop.data(), (std::streamsize) bytes);
    }
    const size_t length = loop.size() / 2;

    struct Event
    {
        long long sample;
        std::string action;
        float a = 0, b = 0, c = 0;
    };
    std::vector<Event> events;
    {
        std::ifstream ef (argv[6]);
        for (std::string line; std::getline (ef, line);)
        {
            std::istringstream s (line);
            Event e;
            if (s >> e.sample >> e.action)
            {
                s >> e.a >> e.b >> e.c;
                events.push_back (e);
            }
        }
    }

    static teach::Rig rig;
    // the take's first controls are set before the engine is prepared, as the page does
    for (const auto& e : events)
        if (e.sample == 0 && e.action == "controls")
            rig.setControls (e.a, e.b, (int) e.c);
    rig.prepare (sr);
    rig.setLoop (loop.data(), loop.data() + length, length);
    rig.setTempo (bpm, bar);

    std::FILE* out = std::fopen (argv[7], "wb");
    if (out == nullptr)
        return 1;
    constexpr int quantum = 128;
    std::vector<float> l (quantum), r (quantum), mix (2 * quantum);
    size_t next = 0;
    for (long long at = 0; at < frames; at += quantum)
    {
        while (next < events.size() && events[next].sample <= at)
        {
            const auto& e = events[next++];
            if (e.action == "play") rig.play (e.a > 0.5f);
            else if (e.action == "cue") rig.hand().cue ((int) e.a);
            else if (e.action == "throw") rig.hand().setThrow (e.a > 0.5f);
            else if (e.action == "freeze") rig.hand().setFreeze (e.a > 0.5f);
            else if (e.action == "controls") rig.setControls (e.a, e.b, (int) e.c);
        }
        const int n = (int) std::min<long long> (quantum, frames - at);
        rig.process (l.data(), r.data(), n);
        for (const auto& s : rig.started)
            std::printf ("start %.4f %s\n", s.beat, dendrite::FlyBody::moveNames[s.move]);
        rig.started.clear();
        for (int i = 0; i < n; ++i)
        {
            mix[(size_t) (2 * i)] = l[(size_t) i];
            mix[(size_t) (2 * i + 1)] = r[(size_t) i];
        }
        std::fwrite (mix.data(), sizeof (float), (size_t) (2 * n), out);
    }
    std::fclose (out);
    return 0;
}
