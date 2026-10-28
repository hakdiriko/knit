# Changelog

## 0.4.0 (reset: the tape and the room)

You tested 0.3 and found it metallic, crowded with controls, slow to count bars and cheap in its moves. All four came from the engine, so the engine is new.

- **dendrite is written lowercase**, everywhere, including the name Live shows. Sets saved with earlier versions still open in the same slot (the codes that identify it haven't changed), with the new knobs at their defaults.
- **Three knobs, nothing else.** wear, friction, room. Ecology, Size, Tone, Itch, Mix, Output, Sync, steady, auto, after N bars, build-up and scratch are gone. Bypass is still there for Live, with no button.
- **The sound goes through a tape, then into a room.** The tape is in series: there is no dry copy beside a moving copy, so nothing can comb-filter or flange, on drums or anywhere.
- **wear** replaces the itch, and there are no events any more. The tape is a 4-bar loop cut into 64 sixteenths in 4 bands. Each time a sixteenth plays something that sounds the same as 4 or 8 bars ago, it loses a little oxide in that band. Weak stretches go first, and wear speeds up as it goes, so a loop that never changes slowly disintegrates at fixed places, like a tape loop played to death. The worn spots follow a fine oxide texture, so they are irregular flakes, never a gate on the grid. Anything new heals it: a new hit comes through clean at once, and sixteenths that changed grow back. What wears off falls into the room. The knob says roughly how long it takes: "30% · ~36 bars" (drums wear in about half that, long pads in about 1.5 times). At 0 it never wears. When Live stops, the tape stops and the damage gets out of the way.
- **friction** is the material: tape saturation with the highs giving first and a touch of even harmonics, 1/f wow and flutter (a few cents at the middle, a dragging tape at the top), the tape's own grain riding the signal, and dust where the sound wore away. clean → warm → worn → rough.
- **room** is new and built against the metal: 32 delay lines with irregular lengths and an allpass inside each loop, every line drifting slowly on its own, highs dying 3 to 6 times faster than lows. none → wood → hall → afterlife (up to 30 s, dark). Measured on a noise burst, its tail is as dense as Gaussian noise at every setting; 0.3's network reached about a third of that, and pushing Tone and Ecology made its tail strongly periodic, which is what metal sounds like.
- **The window** shows the tape: 64 slivers of oxide, dark when fresh, pale where worn, with the head moving across.
- The tape adds 4 ms of latency, reported to Live, which compensates for it.

## 0.3.0 (test: listening to Hakan)

What you heard in 0.2, and what changed:

- **Phasing on drum loops.** Every delay time in the network was always moving a little (a fixed drift, plus the Itch breathing on top). Dry plus a delayed copy whose time moves is a comb filter whose notches sweep, which is flanging, and drums expose it most because they're broadband and sharp. Now:
  - **Phase guard (automatic):** when the input is percussive, the delay times nearly stop moving, and the breathing lives in the filters and levels instead. On a test drum loop at default settings, how much one bar of the wet signal differs from the next dropped by about 2 dB.
  - **New steady switch under Size** (this is the old jump switch, grown up): delay times hold completely still, and any change of time (turning Size, a new note value under Sync, a step move) crossfades between two read heads instead of bending the pitch, like Live's Delay in Fade mode. Another 2 dB steadier on the same loop.
  - What remains on drums comes from the Memory node's grains and the Shift node's pitch shifter. Both are part of the sound by design, and you can lower them with Mix or Tone.
- **The itch only acts when you let it.** New **auto** switch, off by default: Dendrite never breaks your loop on its own. The Itch knob still sets how much it breathes. Turning auto off in the middle of a move hands the knobs back at once. The Scratch button always works.
- **You choose how many bars.** New **after N bars** menu: 4, 8, 16 (default), 32 or 64 bars of repetition before a move. The header counts them: "same for 9 of 16 bars".
- **How it hears repetition.** It compares the input, in 8 bands from 80 Hz to 10 kHz, with the same moment 1, 2, 4 and 8 bars earlier, and keeps the best match. So a 1-bar loop and a 4-bar phrase both count as repeating. A bar of new material (a fill, a new section) empties the count, and moving a knob drains it. When the count is full, the move lands on the next 4-bar line. Without a running transport, it listens for a steady spectrum and counts 2-second bars.
- **Build-up** (new switch, on by default, an A/B for your idea): while the bars fill, the breathing deepens and the network leans about 30% toward the move it's about to make. When the line arrives, the move completes. Off: nothing happens until the move.
- **The dub scratches are gone.** In their place are **moves**: the network turns its own knobs, the way a person at the desk would, holds for 4 bars (2 when the count is 4 bars), then hands them back over 2. Nothing gets louder: auto-gain keeps working through a move.
  - **open**: Ecology leans up and the top opens, so the space blooms.
  - **close**: Ecology and Tone come down, so it's drier and darker.
  - **step**: the delay time moves to double, half, 1.5× or 0.75× (a note value under Sync). It bends like tape, or cuts cleanly with steady on.
  - **lean**: one node comes forward: echoes or the past on drums, shimmer or the Resonator on sustained sounds.
  - **tilt**: the tone tips dark or bright, and the Shift octaves follow.
- The Itch regimes are now still, breathing, restless and feverish.
- The window is 40 px taller for the new switches.
- Sets saved with 0.2 load. If one had Itch above 60%, switch auto on to hear moves again.


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
