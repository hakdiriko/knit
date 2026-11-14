// keep: dendrite never forgets the last minute.
//
// Two tapes are always recording: everything dendrite plays, and the itch alone (the heads,
// without the dry or the echo). Next to them a diary marks every itch, throw and runaway with
// its sample, song position and tempo. From the window you pull any of it into Live as a WAV
// cut on bar lines, so it lands on the grid.
//
// The diary is also the creature's first memory: you can't freeze a creature that never itches
// the same way twice, but you can keep what it did.
//
// Threads: the audio thread writes (capture), without locks or allocation. The window reads
// (collect, take, write) on the message thread. prepare() runs on neither while audio flows.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "dsp/Engine.h"

namespace dendrite::keep
{
enum class Kind : uint8_t { itch, throwing, runaway };

// One event in the diary. The format is the creature's too:
// {sample, ppq, bpm, kind, heads mask, frame choice}.
struct Mark
{
    uint32_t id = 0;
    Kind kind = Kind::itch;
    int64_t sample = 0;     // tape frame where it began (an itch: its first sound)
    int64_t endSample = -1; // where it ended (an itch: the end of its own tail); -1 while it plays
    double ppq = 0.0;       // song position at `sample`, in quarter notes
    double bpm = 120.0;
    double barBeats = 4.0;  // quarter notes per bar
    uint8_t headsMask = 0;  // bit k: head k played (Heads::names order)
    uint8_t frameChoice = 0;
    bool hostPlaying = false;
};

// What the window shows and drags: a slice of a tape, on bar lines.
struct Take
{
    int64_t from = 0, length = 0; // tape frames; length is a whole number of bars
    double bpm = 120.0, barBeats = 4.0;
    int bars = 1;
    double fromBeat = 0.0;        // song position of `from` (a bar line)
    bool itchOnly = false;
    uint32_t markId = 0;          // 0: a stretch of the tape, not a mark
    juce::String name;            // the file's name, without the extension
};

class Keep
{
public:
    static constexpr double seconds = 60.0;
    static constexpr int64_t maxFrames = 5'760'000; // 60 s at 96 kHz; higher rates keep less time

    // Allocates the tapes. Not on the audio thread.
    void prepare (double sampleRate, int maxBlock);

    // Audio thread. The engine writes what its heads play into these (Engine::setItchTap).
    float* itchTapLeft() noexcept { return tapL.data(); }
    float* itchTapRight() noexcept { return tapR.data(); }
    int tapSize() const noexcept { return (int) tapL.size(); }

    // Audio thread, after the engine: the block dendrite played, and what the engine saw.
    void capture (const float* outL, const float* outR, int n, const Engine::View& view, const HostTime& time,
                  int frameChoice) noexcept;

    // Message thread: pull new marks into the diary, forget what fell off the tape.
    void collect();
    const std::vector<Mark>& diary() const noexcept { return marks; }
    const Mark* find (uint32_t id) const noexcept;

    double sampleRate() const noexcept { return sr; }
    int64_t capacity() const noexcept { return frames; }
    int64_t written() const noexcept { return writtenFrames.load (std::memory_order_acquire); }

    // The song position at a tape frame, from the latest block (assumes a steady tempo since).
    struct Clock { int64_t frame = 0; double beat = 0.0, bpm = 120.0, barBeats = 4.0; bool playing = false; };
    Clock clock() const noexcept;

    // Plans: what a drag would take.
    std::optional<Take> takeMark (const Mark&, bool everything) const;
    std::optional<Take> takeFrom (int64_t frame) const; // from the bar at `frame` to the last full bar

    // Reads a take off the tape (zeros where the tape has nothing) and writes it to
    // Documents/dendrite/kept/ (or `folder`). The same mark taken again reuses its file.
    juce::File write (const Take&, juce::File folder = {});
    static juce::File defaultFolder();

    // Reads tape frames [from, from + n) into dest (2 channels). False if the tape moved past them meanwhile.
    bool read (int64_t from, int n, bool itchOnly, juce::AudioBuffer<float>& dest, int destStart) const;

private:
    void push (const Mark&) noexcept;
    double beatAt (int64_t frame, const Mark&) const noexcept;
    std::optional<Take> barsAround (int64_t start, double startBeat, int64_t end, double bpm, double barBeats) const;
    juce::String stamp (const Mark*) const;

    double sr = 48000.0;
    int64_t frames = 0;
    std::vector<float> allL, allR, itchL, itchR, tapL, tapR;
    std::atomic<int64_t> writtenFrames { 0 };
    juce::CriticalSection storage; // prepare() vs the window's reads; the audio thread never takes it

    // the clock, published once per block (seqlock: odd while writing)
    std::atomic<uint32_t> clockSeq { 0 };
    std::atomic<int64_t> clockFrame { 0 };
    std::atomic<double> clockBeat { 0.0 }, clockBpm { 120.0 }, clockBar { 4.0 };
    std::atomic<bool> clockPlaying { false };
    double freeBeat = 0.0;

    // the audio thread's view of what is happening
    enum class ItchState { idle, armed, playing, tail };
    ItchState itchState = ItchState::idle;
    Mark itchMark, throwMark, runawayMark;
    bool throwOn = false, runawayOn = false;
    float itchPeak = 0.0f;
    int64_t itchLastLoud = 0;
    uint32_t nextId = 1;

    // marks travel to the window through a FIFO; a mark is sent when it begins and when it ends
    juce::AbstractFifo fifo { 1024 };
    std::array<Mark, 1024> pending {};

    // message thread
    std::vector<Mark> marks;
    struct Kept { uint32_t id = 0; bool itchOnly = false; juce::File file; };
    std::vector<Kept> kept;
    std::atomic<uint32_t> epoch { 0 }; // prepare() started a new tape: the diary starts over
    uint32_t seenEpoch = 0;
};

// "scream laugh ghost": the heads in a mask, by name.
juce::String headNames (uint8_t mask);
} // namespace dendrite::keep
