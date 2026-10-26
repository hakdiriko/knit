# Changelog

## 0.2.0 (test: the itch, and the new name)

- **viber is now Dendrite.** Saved Live sets that use viber open with Dendrite in the same slot and the same settings. When you install it, delete the old `viber.vst3`.
- New **Itch** knob, where the Phase 2 Motion knob was planned: still → breathing → restless → itchy → compulsive. The default is 45% (restless, no scratches).
  - **Breathing (all the way up).** Each node's delay time, filter and level now drift on a 1/f curve: slow swells with smaller swells inside them, instead of the old tiny random walk alone. Each node also has a slow wobble (0.09 to 0.17 Hz). Ecology pulls the wobbles together, and past about 60% they lock, so the whole network swells as one body.
  - **The itch (above 60%).** Dendrite listens to the input. When the input keeps sounding the same (a loop, a held pad), boredom builds. Novelty drains it, and so does moving a knob. Once it crosses a threshold, Dendrite scratches on the next 4-bar boundary (2 bars above 85%), or right away when the transport isn't running. Then it calms down for a few seconds. At 70% a loop gets scratched about every 20 to 40 s, and at 100% about every 10 to 15 s.
  - **Seven scratches, taken from the dub desk.** Throw (one beat slammed into the echo, which spins for a bar), Runaway (the network tips into self-oscillation, then is cut), Dub drop (the dry drops out for a beat or two and you hear only the echoes), Lurch (Echo and Shift jump to double, half, 1.5× or 0.75× time for two bars), Brake (a hand on the reel: Echo and Memory slow down and catch up), Grab (Memory stutters one short moment), Retune (the Resonator slides a fifth, a fourth or an octave for two bars). Drums lean toward throws, drops, grabs and lurches. Pads lean toward runaways, brakes and retunes.
  - **Scars.** Every scratch leaves a small mark: a slight detune of the Resonator, a nudge of the Echo time (only when Sync is off), or a tilt of the tone. Most of it heals over a few minutes and about 15% stays. The scars are saved with the set. At 0% Itch everything heals within seconds and Dendrite is fully predictable.
  - Auto-gain holds while a scratch plays, so a throw is heard at its real level. The safety limiter still catches every peak.
- New **Scratch** button under Itch: scratch now. Every press scratches, so you can MIDI-map it in Live and play it.
- New **jump** switch next to Sync, an A/B for the open question. Off: stepping Size from one note value to the next bends the pitch like tape (as in 0.1.1). On: it jumps cleanly, with a 25 ms crossfade between two read heads. The Lurch scratch always jumps.
- An itch meter in the header fills as boredom builds and shows the scratch's name in red while it plays.
- The window is wider (820 × 340) to fit the Itch knob.
- Same itch from the same bar: when you press play, the itch restarts and takes its random choices from the bar the transport starts on.

## 0.1.1 (test: Sync)

- New **Sync** switch under Size (off by default, so existing sets sound the same). When it's on, the Echo, Shift and Memory delay times snap to the note value nearest to where Size puts them at the host tempo: straight, dotted or triplet, from 1/64 to a whole note. Size still means small to large; Sync rounds it to the grid.
- The Resonator stays free, so it keeps its pitch.
- Memory grains replay moments one to four quarters of their reach back, so replays land on the grid too.
- Delay times still glide, so stepping Size from one note value to the next bends the pitch like tape, and tempo changes follow smoothly.
- The switch shows the Echo node's note value (for example "Sync: 1/8 dotted"), or "no tempo" when the host doesn't send one; viber then stays free.
- The window is 30 px taller to fit the switch.

## 0.1.0 (Phase 1: the network)

- viber now makes sound: four nodes (**Echo**, **Resonator**, **Shift**, **Memory**) that feed each other through a coupling matrix.
  - Echo: a clean-ish delay with tape glide (turning Size bends the pitch of what's already echoing).
  - Resonator: a short, high-feedback delay that rings at a pitch (about 520 Hz at small Size down to 65 Hz at large).
  - Shift: an octave-up and an octave-down delay; Tone balances them, so bright spirals up (shimmer) and dark spirals down.
  - Memory: four overlapping grains replaying random moments from the last 0.2 to 2.6 seconds.
- Knobs: **Ecology** (colour → space → ringing → life), **Size**, **Tone**, **Mix** (equal power) and **Output** (trim, ±12 dB).
- Full safety chain: saturator and DC blocker inside every loop, wet auto-gain (±12 dB), a −0.3 dBFS safety limiter, a NaN/Inf guard, and a click-free bypass crossfade.
- Above about 75% Ecology the network sustains itself. It feeds on the input: once the input has been silent for a while, the drone fades out on its own.
- Generic window: five knobs with live values (Ecology names its regime), hover hints, double-click to reset.
- Defaults (Ecology 42%, Size 50%, Tone 55%, Mix 40%) already put the sound in a room.

## 0.0.1 (Phase 0: foundation)

- First build of **viber**: a stereo (or mono) passthrough with a placeholder window showing the name and version. It does not change the sound yet.
- CMake/JUCE 8 project producing VST3 on Windows and macOS, plus AU on macOS.
- CI builds Windows, macOS and Linux on every push, runs pluginval at strictness 10 (and `auval` for the AU), and uploads the plug-ins as downloadable artifacts.
- The discovery audit runs on the Linux build in CI and fails the build on any FAIL.
- Docs moved into the repo: `docs/PROJECT_BRIEF.md`, `docs/DSP_PLAYBOOK.md`, `tools/discovery_audit.py`.
- Audit fix: the percussive test signal peaked at +4.3 dBFS, so even an untouched passthrough was flagged as BLOWUP. It is now normalized to a −6 dBFS peak.
