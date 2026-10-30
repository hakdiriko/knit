# Project Brief — Hakan's discovery plug-in

> Source of truth for vision, principles and working method.
> Read this first in every session. Read `DSP_PLAYBOOK.md` before writing any DSP code.

---

## The one sentence

**Whatever you put through it, and however you turn it, it inspires — and you can build on what comes out.**

Every decision in this project is judged against that sentence.

---

## Who you're working with

**Hakan** works in audio post-production (sound design, foley, dialogue editing, re-recording mixing). He composes electroacoustic music for theatre and produces experimental electronic music (IDM, jungle, dub techno). He has deep, trained ears and uses Ableton Live heavily. He has a background in JavaScript and Three.js from audio-reactive work.

He has **never written C++**. You write all the code. He listens, judges, and decides on taste.

How to talk with him:
- Speak audio fluently. On the sound side he knows more than most plug-in engineers, so don't explain what a comb filter is.
- Explain code only where it changes what he hears or what he has to do.
- He likes **mechanistic explanations**: say *why* something works and what the underlying mechanism is.
- He asked for **one small English tip or kind correction of his English in every reply**. Keep it short and put it at the end.

---

## Why we're starting over: lessons from Fular

Fular (private repo `hakdiriko/fular`) was a real-time, two-source wavecycle interleaver. Hakan used it in real work and didn't like it:

- **The setup wasn't user-friendly.** It needed routing and thinking before it made a sound, and it made him feel lost.
- **It felt cheap.**

So this plug-in has to be the opposite of Fular:

| Fular | This plug-in |
|---|---|
| Needed a second source and routing | One input: the track it's on. Zero setup. |
| Silent or dull until configured | Sounds interesting the moment it's inserted |
| Felt lost | Few controls with perceptual names, and you can see them respond |
| Felt cheap | No clicks, no level jumps, everything smoothed, and a UI that reacts to sound |

Two Fular ideas are worth keeping: **tape-style slowdown** (pitch and speed change together) and **host tempo sync**.

---

## Design principles (non-negotiable)

1. **No dead zones.** Every position of every knob, in any combination, is usable. A knob that does nothing for 40% of its travel is a bug, not a design choice.
2. **Thresholds, not just amounts.** Macro knobs pass through qualitatively different regimes (colour → space → resonance → self-oscillation). A small turn can tip the sound into a new state. This is where the feeling of discovery comes from.
3. **It lives.** Slow internal motion makes the output evolve even when nobody touches it, so accidents happen while you're just listening.
4. **Safe at the edges.** Feedback may self-oscillate, but saturation inside every loop and a limiter at the output make it impossible to wreck a mix or a pair of speakers.
5. **Input-agnostic.** Drums, voice, a pad, a field recording, or a full mix must all produce something worth keeping. Test with all of them.
6. **No knobs, if it can be done.** Hakan's call (2026-10-29): minimalism means a more creative interface than a row of knobs. Live still gets parameters to automate, but the window shows something you touch or watch, never a control panel. An A/B is a temporary build or an audio sketch, never a permanent switch. Until the skin phase, the test builds may show plain controls.
7. **Sound first on insert.** The default state, at the default Mix, already does something beautiful.
8. **Organic.** Hakan's own motto for his sonic art: micro-modulation, fluctuation, warmth, randomness.

---

## Direction since 2026-10-29: the dotted dub echo

Hakan listened to 0.4.0 (the tape and the room) and rejected it: wear wasn't the answer. 0.4 was never merged (PR #6). The new direction, in his words:

- Start again from **0.3** and make it minimal.
- **No knobs**, if possible, and a more creative interface (principle 6). It belongs to the skin phase, but the engine is built for it from now on.
- The magic he believes in is **dub echo, dotted**. It must not be a plain delay followed by a reverb: the dotted dub echo has to be done in an original way.
- The itch is an open question: it's original, but it never worked out. Proposal: give it a new job, listening for the moments a dub engineer would throw into the echo.
- Brainstorm before building, by flashcards ([dendrite dub brainstorm](https://claude.ai/artifact/6fj6HHUUSgZYr8aeHrhrNC)) and audio sketches. Claude advises and pushes back throughout, not only when asked.

Ideas on the cards: a branching echo (each repeat splits into a dotted and a straight branch, so the echo grows like a dendrite and thickens into its own space), repeats that diffuse more on every pass, an echo that breathes with the music, an echo that fills the silence, rare recollections from further back.

His picks: the branching echo, diffusion that grows each pass, breathing, filling the silence (recollections: no). The dotted eighth is the trunk. The itch becomes the throw (to be judged when he plays it). From 0.3, keep the Echo with its tape glide, Memory and Sync's tape slowdown. Analog warmth, with the Roland RE-201 as a reference, but light. Interface: no knobs (one at most), controllable from Push or a MIDI keyboard, automatable, drivable by an external LFO; Krotos Studio's control surface is a reference. A sidechain that drives the breathing is for a later phase.

**0.5.0** is the first build of it (see the changelog and DSP_PLAYBOOK §10). Memory and the tape slowdown are not in it yet. From here on: builds first, audio sketches only for quick A/B questions.

### Rejected: the tape and the room (0.4.0, 2026-10-28)

Three knobs (wear, friction, room): a repeating part wore its place on a 4-bar tape and new material healed it; what wore off fell into a 32-line room. The room's measurements (no periodicity, echo density equal to noise) are still a useful bar for anything spacious.

The rest of this section is the V1 concept (0.1–0.3), kept for history.

## Core concept (V1, replaced at 0.4.0)

### "Ecosystem": a small network of nodes that feed into each other

- **4 nodes.** Each is a modulated delay → filter → saturation, with its own character:
  - **Echo**: a clean-ish delay with tape glide
  - **Resonator**: a short delay with high feedback, so it rings at a pitch
  - **Shift**: a pitch-shifted delay, so you get shimmer or downward spirals
  - **Memory**: grains read from the recent past
- **A coupling matrix** decides how much the nodes feed each other:
  - low: separate colours
  - mid: diffusion and space
  - high: ringing
  - top: self-sustaining textures that are still driven by the input
- **A memory buffer** is always recording the last ~30 s. The Memory node sprays grains from the past, and tape slowdown lives here too.
- **A later node type, Spectral Scatter**, adds per-band delay and freeze (Phase 4).
- **A perceptual layer on top**: knobs are named by what you hear, not by DSP terms.

### Proposed controls (names are drafts; Hakan has the final word)

| Knob | What you hear | What it moves internally |
|---|---|---|
| **Ecology** | how much the parts talk to each other: colour → space → ringing → living drone | coupling matrix, loop gains, diffusion |
| **Size** | small and close → huge and far | delay times, stereo spread, damping |
| **Tone** | a dark ↔ bright tilt of the whole network | node filter cutoffs, saturation drive |
| **Motion** | still → restless | depth and rate of the internal random walks |
| **Memory** | the present ↔ the recent past | grain read position, grain density, slowdown |
| **Character** | morphs the balance between node types | node levels, pitch intervals, resonator tunings |
| **Mix** | dry ↔ wet (equal-power crossfade) | — |
| **Output** | trim, applied after auto-gain | — |

**Buttons:**
- **Freeze**: holds the network's current state
- **Sync**: switches between free time and host tempo
- **Reroll**: a new random seed for Motion and Character, so the same knob positions lead to new territory

---

## Tech stack

- **Language and framework:** C++ with JUCE (current 8.x), built with CMake. Bring JUCE in as a git submodule or with FetchContent.
- **Formats:** VST3, plus AU on macOS. Hakan uses Ableton Live.
- **UI:** start with a clean native JUCE UI. For the premium UI, evaluate JUCE 8's WebView UI. It would let the visual layer be written in HTML, JS and Three.js, which is Hakan's own territory, so he could shape it himself.
- **CI:** GitHub Actions builds macOS and Windows binaries on every push and runs pluginval. **Hakan never compiles anything.** He downloads the build artifact and drops it into his plug-in folder.
- **First session, ask Hakan once:**
  - which OS and Ableton version he uses
  - what the plug-in is called, so the repo can be named (he names it)

  Record the answers in the repo README.
- **Repo:** a new repo, separate from Fular. Hakan creates it or grants access.

---

## Roadmap

**Phase 0 — Foundation**
- Repo and the CMake/JUCE skeleton
- CI producing macOS and Windows artifacts, with pluginval passing
- A README with install steps, including the macOS quarantine step for unsigned builds

**Phase 1 — The network**
- 4 nodes, the coupling matrix, and the Ecology, Size, Tone and Mix knobs
- The full safety chain
- A generic UI
- The discovery audit passes

**Phase 2 — Life**
- Motion
- The memory buffer, Memory node and tape slowdown
- Freeze, Sync, Character and Reroll

**Phase 3 — Premium**
- A custom UI with a live visualization of the network
- Presets with evocative names
- A polish pass against the quality checklist in the playbook

**Phase 4 — Expansion**
- The Spectral Scatter node
- Optional key/scale tuning for the Resonator nodes
- New ideas from Hakan's sessions

---

## How each iteration works

1. **Make it audible fast.** A rough version he can hear beats a perfect version he can't.
2. **Audit before handing over.** Run `tools/discovery_audit.py` and fix every flag first.
3. **Hand over with a listening sheet:**
   - what changed
   - 3 things to try, and on which kind of material
   - 1–3 questions about taste
4. **A/B instead of asking.** When two musical directions diverge, ship both, as a switch or as two builds. Don't ask him to imagine the difference.
5. **Keep records.** Maintain `CHANGELOG.md` and `DECISIONS.md` (one line per decision, plus why).

---

## Autonomy

Hakan grants this project broad authorization. Work as the lead developer.

**Decide and act without asking:**
- architecture, algorithms, libraries, file structure, and naming of internals
- writing, refactoring and deleting code in the project repo; creating branches, committing, merging, setting up and changing CI
- researching freely, and building new skills (see below) whenever they help
- sound-design calls guided by the principles above: report them, don't request permission

**Pause only for:**
- things only Hakan can do: signing in, credentials and API keys, paid services, code-signing certificates, listening in his DAW
- irreversible actions outside the repo's normal history: deleting repos, deleting branches with unmerged work, public releases, license changes
- taste forks with no clear winner: handle these with A/B builds (above)

---

## Skills to learn

This plug-in's quality depends on specialist knowledge. Study as you go, and **turn what you learn into reusable skills** using the `skill-creator` skill. Propose them to Hakan so he can save them.

**Topics:**
- real-time audio safety in JUCE: allocation, locks, denormals, parameter threading
- the JUCE + CMake + GitHub Actions + pluginval build pipeline
- feedback delay networks and bounded nonlinear feedback
- granular synthesis and buffer-based time manipulation, including tape slowdown
- real-time spectral processing: STFT, per-bin delay and freeze
- psychoacoustic mapping: perceived loudness, distance cues, perception of pitch and time
- JUCE 8 WebView UI with parameter relays

**Starting references:**
- the JUCE documentation and tutorials
- Julius O. Smith's online books at CCRMA: *Physical Audio Signal Processing* (delay networks) and *Spectral Audio Signal Processing*
- Geraint Luff (Signalsmith Audio), *Let's Write a Reverb* (practical FDN design)
- Will Pirkle, *Designing Audio Effect Plugins in C++*
- Udo Zölzer (ed.), *DAFX*
- Curtis Roads, *Microsound* (granular)

Before writing a skill, check version-specific API details against the current JUCE docs.

---

## Files in this project

- `PROJECT_BRIEF.md`: this file
- `DSP_PLAYBOOK.md`: the mechanisms and code patterns behind the principles, plus the quality checklist
- `discovery_audit.py`: an automated sweep that finds dead zones, blow-ups and level jumps in a built plug-in. Copy it into the repo as `tools/discovery_audit.py`.
