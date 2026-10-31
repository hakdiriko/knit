# Changelog

## 1.0.0 (dendrite, finished: a dub echo that grows around your head)

A new instrument, built from what survived every test: the dotted dub echo of 0.5, the breathing you liked, the mono-safe space you asked for after the stab test, and the three heads of the hypnagogic demo. Four controls, a window to play, and a one-page manual in every download.

- **The field.** One light to drag. Up is **feed**, how long the repeats live: off, whisper, dub, drown, endless. Right is **space**, where they live: inside, around, envelop, beyond. Every hit you play grows a branch on the field, and the repeats branch off it at the dotted ⅛ and the ¼. Shift-drag for fine moves, double-click for the default (dub 50, around 40), the mouse wheel moves feed (sideways: space).
- **The echo.** Two tape heads in one loop, the dotted eighth and the quarter of your project's tempo (120 BPM without one). Every pass loses a little top, gains a little grit and wobbles with a slow wow that never repeats (the tape colour of 0.5, unchanged). The dry passes untouched and there is no mix knob.
- **The space grows around your head.** The ⅛ keeps the side and the ¼ crosses it, so you hear the dub ping without hard left and right. Every pass writes a little of the middle into the side again, so each repeat steps further out. Envelop dissolves the side with every pass; beyond also turns it slowly around you. Fold to mono and you hear the plain dub echo: space changes the mono sum by level only (measured −133 to −139 dB residual).
- **It breathes.** It ducks under what you play, swells in the gaps, drifts ±3 dB on its own and brightens on the in-breath. When you stop, the echo fills the silence for up to four bars.
- **throw.** Hold it and what's playing goes into the echo: the send opens, the repeats jump louder and step out to one side, then fall back over about two bars and drift home. At feed off, only what you throw reaches the echo (the dub engineer's closed send). Map it to a pad or a key in Live.
- **itch.** Choose 1, 1¼, 1½, 1¾, 2, 3, 4 or 8 bars, or off (default 4). dendrite compares every sixteenth with 1, 2 and 4 bars earlier. When a whole stretch sounds the same, it grabs the loudest hit of the last bar and three heads answer at one, two and three beats apart: **scream** (a rising throat sweeping across your head), **laugh** (ha, ha, ha, bouncing left and right) and **manipulate** (the hit slowed down and turned around you). A stretch of quiet always follows, and the longer a loop runs unchanged, the harder it itches.
- **Never louder than what you played.** The echo has a ceiling just under the level of what goes in, so throws and endless stay big without slamming the −0.3 dBFS safety limiter.
- **The window.** Dark stone, ochre light, coral for the itch. It scales from 75% to 150% (drag the corner), and shows the tempo it's locked to.
- **Install.** The plug-in folder is now `dendrite.vst3` (lowercase). Replace any `Dendrite.vst3` or `viber.vst3` in `C:\Program Files\Common Files\VST3`.
- Sets saved with 0.3 or earlier open with dendrite at its defaults: the network's knobs (ecology, size, tone, mix, output, sync, steady, auto, bars, build-up, scratch) are gone.
- Zero latency. Checked: pluginval strictness 10, discovery audit 0 FAIL / 0 WARN, 300 random states all alive.

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
