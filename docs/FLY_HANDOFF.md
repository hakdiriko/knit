# The fly: handoff

How dendrite's itch became a real fly's brain, how it works, and where it stands. It is written so another agent, thread or a separate product (Hakan has floated selling the fly as an add-on) can take it over cold. Keep it current: when the fly changes, change this file in the same commit.

Last updated: 2026-11-21 (section 4 has the male fly, an A/B; section 3 the per-fly folders). Before that: 2026-11-14, version 2.4.0 (section 5b is new: read it before the Reflexes section, which describes the 2.3 body, still there behind `setMusical (false)`). Main holds 2.2.1 (PR #9 and #10 merged on 2026-11-14). The fly is now worked on in its own project ("fly"); standard dendrite (echo, window) continues in the other project. The fly project touches only `Fly*`, `FlyData*`, `FlyBrain*`, `tools/fly/` and the small hooks that connect them (Engine, Echo's Touch, Processor's fly state).

## 0. Start here (first hour for whoever takes it over)

1. Read `CLAUDE.md`, `docs/PROJECT_BRIEF.md` and `docs/DSP_PLAYBOOK.md`, then this file to the end.
2. Build the renderer:
   ```
   g++ -O3 -march=x86-64-v2 -std=c++20 -I Source tools/render/render.cpp Source/dsp/*.cpp Source/flies/flywire/FlyData.cpp -o build/render
   ```
3. Hear it. Render any loop twice:
   - `build/render loop.wav out.wav --repeat 16 --fly-log fly.csv` is the fly with its inner life;
   - add `--still` for the 2.1 fly.
4. Watch it. Build with `-DDENDRITE_TOOLS=ON` and run under `xvfb-run`:
   ```
   DENDRITE_FLY_OPEN=0.5 build-plugin/dendrite_snapshot_artefacts/Release/dendrite_snapshot loop.wav out.png 0.5 0.4 3 20 1
   ```
5. Before any push:
   - pluginval at strictness 10;
   - `python3 tools/discovery_audit.py <vst3>` with 0 FAIL;
   - a `CHANGELOG.md` line, a `DECISIONS.md` line (with why), and this file updated.
6. Hakan judges by ear on Windows/Live. Every phase gets a short report video and a listening sheet (`dendrite-home/listening/` in the project files) with the Windows artifact link.

### Words used here

- **Connectome:** the full wiring diagram of a brain. Here that means which neuron connects to which, with how many synapses.
- **LIF (leaky integrate-and-fire):** the simplest spiking neuron. Its voltage leaks toward rest, inputs push it up, and past a threshold it spikes and resets.
- **DN (descending neuron):** carries commands from the brain to the body (legs, wings). Its firing is the fly's "action".
- **Giant fiber (DNp01):** the escape command. It fires and the fly jumps.
- **Kenyon cells (KC):** the mushroom body's sparse code for "what this smell is".
- **MBON and DAN:**
  - MBONs are the memory's outputs: approach or avoid.
  - DANs are the dopamine teachers.
  - Dopamine at a KC to MBON synapse weakens it, and that is learning.
- **Habituation:** a sense tiring of the same thing.
- **Touch:** the fly's only output: offsets to dendrite's echo (tape speed, feed, space, a narrow band, freeze, throw, return level).

## 1. Why it exists

dendrite is a dub echo (M/S dotted delay with tape, breathing, throw and space). Its **itch** is the part that acts on its own. Earlier itches were rejected by Hakan:
- 0.2 to 1.2: the five heads (scratch, scream, laugh, manipulate, ghost), fired by dice.
- 2.0a: the creature, a 32-neuron designed network.
- 2.1 sketch: the hand, designed gestures on the echo's controls ("these movements might be presets").

On 2026-11-13 he asked for the itch to be "controlled by a neural network, like how we get hurt and react, like how a fly reacts while flying... you can find a fly's brain open source. we'll use that fly's brain, can we train it?" He approved the direction ("yes. let's see how it goes") and asked to see the fly's neural activity in the window. After the first live build he found it too repetitive. He chose to give it an inner life: restless neurons, moods and memory.

Principles he holds:
- Minimalism: no new knobs.
- Organic: micro-modulation, randomness.
- Red #b3241b is used only on the itch.
- Every phase gets a report video.
- He judges by ear on Windows 11 / Ableton Live 12.

## 2. What it is, in one paragraph

A cut of the adult *Drosophila* connectome (FlyWire, 783 release) runs live inside the plug-in as leaky integrate-and-fire neurons, the way Shiu et al. (2024) ran the whole brain. Its wiring is the fly's own; nothing in it is designed. The music is fed into its real sense neurons:
- **ears:** Johnston's organ;
- **eyes:** the looming detectors;
- **smell:** the antennal lobe's projection neurons.

The firing of real output neurons moves the echo:
- **giant fiber escape:** a jump;
- **DNg11 antennal grooming:** a scratch;
- **left/right descending neurons:** a turn.

Since 2.2 it also has:
- random kicks on every neuron;
- two moods;
- live learning in its mushroom body (dopamine-gated depression of Kenyon cell to MBON synapses, which is how flies learn to avoid a smell).

It is **not trained**. "Training" so far only means it learns during use; taste training (section 9) has not started.

## 3. Where the code lives

| Path | What |
|---|---|
| `Source/dsp/Fly.h`, `Fly.cpp` | The live fly. Senses, LIF brain, inner life, reflexes, the Touch it outputs. No JUCE; pure C++20. |
| `Source/flies/<fly>/` | One folder per fly (since 2026-11-21): `flywire` (the default, FlyWire 783) and `malecns` (Janelia's male CNS, an A/B). Each holds `FlyData.h/.cpp` (the generated brain, never edit by hand; regenerate with `tools/fly/export.py`), `Atlas.h` (synapse weight, inhibition, sense gains, readout calibration, box, names) and `Identities.h/.cpp` (who each neuron is). CMake `-DDENDRITE_FLY=flywire|malecns` picks one; `Source/dsp/FlyData.h` and `Source/rig/Identities.h` are wrappers. |
| `Source/dsp/Hand.h` | Defines `Touch`, the fly's only output to the echo: speed, feed, space, band, bandHz, freeze, throwing, lift. |
| `Source/dsp/Engine.cpp` | Owns `Fly fly`. Sets awake from the itch knob (`awakeFor`), sets `emptyBandHz`, calls `fly.process(l, r)` per sample, passes the Touch to `echo.setTouch`. `useFly(bool)` (default on) skips the heads. |
| `Source/dsp/Echo.cpp` | Applies the Touch (look for `touchNow`). |
| `Source/Processor.*` | Publishes `Fly::Doing` to the window through `display.fly*` atomics; `takeFlySpikes(n)` hands each neuron's spike count to the UI. |
| `Source/ui/FlyBrain.*` | The view: a small brain in the strip; click it for a drawer with the brain large, labels, the doing-line and the moods line. The UI thread owns window sizing (Editor, `grewForDrawer`). |
| `tools/fly/` | Offline Python: `sim.py` (whole brain, reflexes), `sim2.py` (whole brain with inner life), `families.py`, `touch.py`, `prune.py` / `prune2.py` (the cut), `export.py` (writes FlyData), README with credits. |
| `tools/render/render.cpp` | Offline renderer without JUCE. `--fly-log fly.csv` logs one row per 256 samples: seconds, jump, semitones, throwing, frozen, scratch, scratchHz, turn, arousal, fatigue, memory, feeling, fear. `--still` plays the 2.1 fly (no inner life). `--repeat N` loops the input. |
| `tools/fly/probe/` | The instruments (Phase 0, 2026-11-16): `brain_share.py` plays the material set (`material.py`) with the real fly, a scrambled fly, a no-brain stand-in and the real fly with other chance, and reports how much the brain does. Renderer probes: `--fly-dump`, `--scramble`, `--seed`, `--surrogate`, `--stats`, `--bar`. Results: `docs/FLY_BASELINE.md`. |
| `tools/snapshot/` | Renders the window to PNG or frames; `DENDRITE_FLY_OPEN=t` opens the drawer at t seconds. |

**Separability (for an add-on).** The fly's whole boundary with dendrite is small:
- in: stereo samples, `setAwake(0..1)`, `setEmptyHz(hz)`;
- out: one `Touch` per sample, a `Doing` struct for display, per-neuron spike counts.

Section 10 has more.

## 4. Data and provenance

Raw data (about 135 MB) goes in `tools/fly/data/`. It is not in the repo; in sessions it has been a gitignored symlink.
- `Completeness_783.csv` and `Connectivity_783.parquet`: from github.com/philshiu/Drosophila_brain_model (MIT).
- `Supplemental_file1_neuron_annotations.tsv`: from github.com/flyconnectome/flywire_annotations (`supplemental_files/`).
- Fetched from raw.githubusercontent.com. In the cloud sessions zenodo, codex.flywire.ai and flywire.ai were blocked by the proxy.

**Credit (keep it in any product):** FlyWire connectome, Dorkenwald et al. 2024 and Schlegel et al. 2024 (Nature); model after Shiu et al. 2024 (Nature). **Licence:** the FlyWire data are released for open use with citation, and Shiu's code is MIT. 2026-11-21: FlyWire's data licence is CC BY-NC 4.0 (non-commercial); selling needs Princeton's permission (Hakan asked). The male fly below is the CC BY alternative.

### The male fly (2026-11-21, an A/B)

FlyWire's data are CC BY-NC 4.0, so selling dendrite with them needs Princeton's permission. Janelia's male CNS v1.0 (Berg et al., Cell 2026) is CC BY 4.0: anyone may sell with credit (`packaging/CREDITS-malecns.txt`). `tools/fly/malecns.py` loads it in sim.py's shape (`FLY=malecns` for every tool in tools/fly), brain only (both optic lobes, no nerve cord): 144,076 neurons, 21.9 M connections. Data: the public bucket `gs://flyem-male-cns/v1.0/connectome-data/flat-connectome/` (three feathers, about 570 MB, plus `syn-partners` (3 GB) once for places), in `tools/fly/data/malecns/`.

Its calibration, each measured against the FlyWire fly on the same music (details in malecns.py):
- synapse 0.275 / 2.0 mV (Janelia finds about 1.77 times more synapses per connection), inhibitory synapses x 1.17 (back to FlyWire's 40.4% inhibitory share; without it the central complex and DNg33 fire on their own);
- each sense group's input scaled to the FlyWire group's total output; its right ear's JO-A and JO-B are fragments, capped at x 3;
- the readouts (`READOUT`): grooming x 1.63, steering x 10.3 (its DNa01/02 hardly fire), excitement x 0.276, feeling offset -0.606 (its glutamate MBONs dominate at rest). With them it itches as often as the FlyWire fly on five of six loops (pad: 8 against 23).

Cut: `prune_runs.py` (every neuron that fired in twelve 62 s runs, plus the senses and every readout neuron) then `prune2.py` (with two inner-life runs): 13,065 neurons, 1,763,631 connections. It costs about a quarter more CPU than the FlyWire fly (render of 62 s: 12.1 against 9.8 s on one core, echo included).

Regenerate: `FLY=malecns python3 sim.py` on each loop, `prune_runs.py`, `sim2.py` on keys and groove, `prune2.py`, then `FLY=malecns python3 tools/fly/export.py keep2.npy sim_keys.npz`.

### The cut

The whole brain (139,255 neurons, about 15 M connections) is too heavy to run live. Neurons that never fire change nothing, so the plug-in keeps only:
- every neuron that fired on test material, both in the 2.1 reflex runs and in an inner-life run on a 3.8 s loop repeated for a minute;
- plus the senses, and every MBON and DAN.

The antennal lobe's own circuit is left out: receptor neurons (ORNs) and the ALLN, LHLN and ALON classes. In this model it runs away by itself, with Kenyon cells about 80% active at about 90 Hz. Smell therefore enters at the projection neurons directly.

The result is 11,705 neurons and 1,034,261 connections, including:
- 2,459 Kenyon cells;
- 96 MBONs;
- 331 DANs, of which 16 are PPL1;
- 28,761 plastic KC to MBON synapses.

### Regenerating

1. `SR=200 ETA=0.5 python3 tools/fly/sim2.py loop.wav inner.npz 60`
2. In `tools/fly`: `python3 prune2.py keep.npy keep2.npy inner.npz`
3. `python3 export.py keep2.npy song.npz`

`keep.npy` and `song.npz` come from the 2.1 pipeline (README steps). The reference `keep2` was reproduced exactly from these steps. These `.npy`/`.npz` files are not in the repo; keep a copy outside the session scratchpad if you want to rebuild bit-for-bit.

## 5. The live model (Fly.cpp)

### Neurons

These follow Shiu et al. 2024:
- rest and reset −52 mV, threshold −45 mV;
- membrane τ 20 ms, synaptic τ 5 ms, refractory 2.2 ms (4 steps);
- 0.275 mV per synapse; weight = synapse count × sign (by transmitter);
- step dt = 0.5 ms (2 kHz), spikes arrive after 2.5 ms (a 5-slot ring).

Spikes propagate through a CSR matrix (`rowStart`, `target`, and `w`, a mutable float copy of the weights). The membrane update is branch-free over arrays padded to 16, so it vectorises. CMake Release uses -O3; GCC -O2 would not vectorise it.

### Senses (per sample, read at the brain's pace)

**Ears (JO), per side:**

| Group | Band or speed | Envelope |
|---|---|---|
| B | 40–300 Hz | 10 ms |
| A | 300–4000 Hz | 8 ms |
| CE | slow level, 20–8000 Hz | 250 ms |

**Eyes (LPLC2/LC4 split by height into low/mid/high):**
- bands 40–200, 200–2000 and 2000–10000 Hz;
- looming = log(fast 5 ms ÷ slow 80 ms envelope).

**Smell:**
- 32 bandpass bands from 60 Hz to 12 kHz, each heard against its own recent peak (attack 50 ms, release 30 s);
- contrast = 2 × max(0, band − median of bands);
- glomerulus k (alphabetical) feeds band k·32/glomeruli.

**Scaling and firing:**
- Each sense is heard against its own recent loudest (attack 50 ms, release 30 s).
- Each sense habituates: use per second at full drive, then recovery time.

| Sense | Use per second | Recovery |
|---|---|---|
| Looming | 12 | 10 s |
| B and A | 0.8 | 3 s |
| CE | 0.3 | 2 s |
| Smell | 0.5 | 5 s |

- Input neurons fire Poisson at rate (80 + 240·awake)·gain·drive·freshness (xorshift RNG, fixed seed).

### Inner life (2.2)

**Restless kicks:**
- Each neuron gets a +1.5 mV kick about 10 times a second × (1 + arousal).
- The kicks are placed by geometric skipping, so the cost is per kick, not per neuron.

**Readiness:**
- An Ornstein-Uhlenbeck walk (τ 3 s) scales the jump threshold by exp(−0.35·readiness).
- So the same hit makes it jump one time and not the next.

**Arousal:**
- Each jump adds 0.12·size·(1 − arousal).
- It relaxes toward 0.2 over 40 s.
- Sense gain is (0.75 + 0.5·arousal).

**Fatigue:**
- It tracks the brain's spike rate between 40k and 140k spikes/s over 120 s.
- Sense gain is × (1 − 0.35·fatigue).

**Learning (every 10 ms; both ways since 2.5):**
- When escape is above 30 Hz, the 16 PPL1 DANs are forced to fire (5% per step): this is the shock.
- When it ends, relief: 0.3 s later the 307 PAM DANs are forced to fire for 0.5 s at 5% × `Taste::relief` (0.05) per step (Tanimoto et al. 2004). Hand-wired, as the shock is.
- DAN spike traces (τ 0.5 s) reach each MBON through the connectome's DAN to MBON matrix, normalised per MBON. Only the **burst** teaches: each MBON's dopamine minus its own 20 s average (2.5). Before 2.5 the DANs' steady firing (driven by the restless kicks) depressed every synapse all the time, which is what pinned fear at 1.0 within two minutes.
- Forward (KC active, then dopamine): w ·= 1 − dt·learn·d·(1 − e^(−KCtrace/5)), with the KC trace τ 1 s and d = 1 − e^(−burst/5).
- Backward (dopamine first, the KC fires after; Handler et al. 2019): w += dt·learn·d_before·(1 − e^(−KC spikes this 10 ms))·(w_birth − w), with d_before the burst trace over 1.5 s. Never above birth.
- Forgetting: w += (w_birth − w)·dt/640 s.

**Signals the learning produces:**
- *memory*: the plastic weight sum against at birth.
- *fear* (2.5): how much weaker what it smells now reaches its come-closer MBONs than at birth, minus the same for its keep-away MBONs (the punished compartments against the rewarded ones; dopamine everywhere at once cancels out), over 0.35 for full fear, smoothed over 0.2 s. `tools/fly/fear.py`: 0.3 to 0.4 after five minutes on both loops, and it rises when the music changes (was 1.0 and 0.99).
- *feeling*: (come-closer MBONs − keep-away MBONs) ÷ (sum + 2). Glutamate MBONs count as keep-away; ACh and GABA MBONs count as come-closer (Aso et al. 2014).

**Persistence:**
- Memory and moods survive `prepare()` and `rest()`; `forget()` resets them.
- Since 2.3 they are **saved with the set** (`Fly::Memory`, 57,560 bytes, about 77 KB as base64 in a `fly` child of the state): every plastic synapse as a uint16 share of its birth weight, plus arousal, fatigue, the feeling habit and `lived` (seconds of inner life). Activity, senses and reflexes are not saved; a fly brought back wakes rested and remembering.
- The audio thread seals it into a seqlock vault about every 2 s (`Processor::keepFly`); a loaded memory waits in `flyArriving` and is adopted between two blocks, as the creature's life is. An invalid or missing one leaves the current fly.
- Right-click the brain (small or drawer): "a new fly (it forgets what it learned)" calls `Processor::forgetFly()`, done at the next block.

### 5b. The body in time (2.4, the default)

Hakan's goal for the fly: **precise in time, moving with the music, and more than one tape trick.** Measured by `tools/fly/goal.py` (see DECISIONS 2026-11-14).

- `Fly::setClock (beat, bpm)` gets the host's position every sample (Engine). `reflexes()` finds the 16th lines (`floor (beat * 4)` changing).
- The neurons arm a move (same escape threshold as before, plus a rest of `4 * (6 - 5 * awake)` 16ths since the last move ended). The move starts on the next line: any 16th when awake > 0.75, else an 8th; big moves always an 8th.
- `startMove()` picks one of five moves from size, fear (down with probability fear) and arousal: hop, leap, duck, dive, rush. Each is up to three `Leg`s: a slope of `stretch` (heads' distance against the grid's, `speed = 1 / stretch`) per 16th, a length in 16ths, and an exact end value. Straight slides give constant pitch ratios: dotted head `1 - 3 * slope`, quarter head `1 - 4 * slope`. Holds sit at 1/2 (double time) or 2 (half time).
- Since 2.4.1: a moving budget (`busyShare`, a leaky share of time spent moving over 8 bars, must be under `0.08 + 0.24 * awake` to arm), big moves on bar lines (half bars when wide awake, from `setClock`'s `barBeats`), and `lastMoves`: a third identical move becomes its small opposite. Hakan's 2.4 marks: groove yes at 18% moving, keys no at 32%.
- Since 2.4.2: `pulse` (share of the last 32 16ths with `max (drive[10], drive[11]) > 1`, the high eye band) gives `exposed = 1 - clamp01 ((pulse - 0.2) / 0.2)`. Exposed material: rest x (1 + 4 x exposed), budget x (1 - 0.6 x exposed), and in `startMove` half the moves become `hold` (no tape: a 3/16 throw and a one-beat freeze), rush and leap become hop, dive becomes duck. The keys loop measures about 0.18, the groove loop about 0.75. Hakan's marks so far: groove yes (2.4, 2.4.1), bare keys no (2.4, 2.4.1); 2.4.2 on the keys is waiting for his mark.
- Since 2.4.3: on exposed material `startMove` only makes `hold` or `sink` (slope +1/6 for 2 16ths, back at -2/3 in half a 16th: the only slope where both heads are pure octaves and fifths), alternating; the band opens only above `0.12 + 0.4 * exposed`.
- **Known, from the review thread ("Review the fly honestly", 2026-11-16), not fixed yet:** move onsets are the same with the wiring scrambled (hits plus grid decide the timing, the brain only the size and the fear); the choice of move is a fear-weighted coin; fear only climbs on a loop (0.64 keys, 0.97 groove after 90 s). The planned answer: bar and phrase position as senses, more descending neurons read as the choice of move, and reward dopamine (PAM) so learning goes both ways. All three want a new cut of the connectome (section 4).
- Throw lengths are 1, 2 or 3 16ths; freezes start on a beat and last 2 or 4 16ths.
- The band (scratch) is sampled every 16th and held (off under 0.12); the lean every 8th; the feed every beat.
- `Doing::move` and the fly log's 14th column carry the move.
- Ideas not built yet: more outputs from the connectome as moves (the moonwalker neurons for a reversed echo, courtship song neurons for a tone of its own; both need a new cut, see section 4), bigger moves saved for bar and phrase ends, the band stepping through pitches of the music's key.

### Reflexes: neurons to Touch (`reflexes()`)

**Inputs, read off the neurons:**
- *escape*: the rate of DNp01/02/04/11, smoothed over 30 ms;
- *groom*: the 2 most active DNg11, over 150 ms;
- *rub*: the difference between those two legs;
- *lean* (2.5): DNa01 + DNa02 left minus right (`steerLeft`, `steerRight`, confirmed in the FlyWire annotations), per neuron in Hz, smoothed over 1 s, relative to its 6 s habit, /8 Hz for full. On a groove panned bar by bar it follows the pan (correlation 0.64); before 2.5 it was every descending neuron's left minus right;
- *excited*: left plus right.

**The jump:**
- It jumps when escape > 45·(1.25 − 0.3·arousal)·(1 + 0.4·fatigue)·e^(−0.35·readiness), and at least 1 + 1.5·fatigue seconds after the last jump.
- size = min(1, escape/80·(0.7 + 0.6·arousal)).
- Dive with probability = fear, otherwise spring:
  - dive: the tape slows, length 0.5 + 0.9·fear s, size × (0.5 + 0.5·fear);
  - spring: the tape speeds up, size × 0.6.
- Speed = 2^(0.4·lurch); a full lurch is ±4.8 semitones.
- Throw for 0.12 + 0.2·size + 0.3·fear s.
- Freeze 0.1 to 1.2 s after a hard jump (size > 0.7), or after a fearful jump (fear > 0.6 and size > 0.4).

**The scratch (band):**
- amount = 0.85·gm^1.5·(1 − 0.6·fatigue)·(0.75 + 0.4·mood), where gm = clamp((groom − 4 s habit + 0.5)/7).
- mood = (feeling − its 120 s habit)/0.08, clamped to ±1.
- place = emptyHz·2^(rub/40 + 0.8·mood − 1.2·fear), clamped to 150–7000 Hz.

**The rest of the Touch:**
- space = 0.35·lean;
- feed = 0.18·clamp01(excited/4) − 0.05;
- lift = 1 + 0.6·band + 0.8·throwGlow.

**Smoothing (since 2.2.1):** the brain updates the target every 0.5 ms, and the Touch glides to it per sample over about 4 ms. Before that, the tape speed stepped every 24 samples and the repeats crackled like a bitcrusher (Hakan heard it). Measured on his loop, energy above 10 kHz was 30 dB over the itch-off level; with the glide it is back to the itch-off level. This had been there since 2.1.

**The itch knob:**
- It sets awake = clamp01(1 − 0.125·log2(frameBars/0.25)).
- Off = asleep, ¼ bar = 1, 4 bars = 0.5.
- No new parameter: the id and its saved values are unchanged.

## 6. The view (FlyBrain)

- Every neuron in the cut is drawn at its FlyWire position (`neuronX`/`neuronY`, front view) over a grey density image of the whole brain.
- A neuron that just fired is red and dries to grey.
- **Small:** where the head lamps were.
- **Drawer (click):** about 330 units under the strip. Labels for ears, eyes, giant fiber, grooming and mushroom body. A Fragment Mono doing-line ("jumps tape −1.2 st throw · scratches 1.4 kHz · turns left"), and a grey line above it ("wound up 0.55 tired 0.23 afraid 0.20 learned 1.4%").
- Fullscreen: the drawer opens inside the window. The UI thread owns this behaviour.

## 7. Cost and checks

- `tools/fly/matrix.cpp` (2.5) drives the Engine with a fake transport through 26 cases: 70 to 174 bpm, 3/4, 6/8, 7/8, stopped, tempo changes, loop jumps (on and off the bar), silence around music, mono, one side, 44.1 to 192 kHz, ragged and fixed block sizes. It checks finite and bounded output, moves on the 16th grid, a move in every musical part, none in silence, silence that stays silent, and the cost per block in thread CPU time. All pass in 2.5.
- Cost on the cloud machine (2.5): 10 to 12.5% of one core for the whole plug-in at 48 kHz (2.4.3: about 13%; Phase 0's sparse piece 8%), 22% at 192 kHz. The slowest block is 79% of a 128-sample buffer and 33% of a 512-sample one (one run on a shared cloud machine). With 32-sample blocks the 10 ms learning lands in one block (up to 3x that block's length), harmless at real buffer sizes.
- Where it goes (fly alone): the step's spike delivery about 3.4%, the neuron update 1.5%, the spike search 1.2%, the Poisson senses 0.8%, learning 2.2%, reflexes 0.1% (was 1.4% before the 2.5 lookup table).
- Measure cost with denormals flushed, as the plug-in runs (JUCE's ScopedNoDenormals; Phase 0's renderer and `matrix.cpp` both do). Without that, quiet material looked several times dearer (Phase 0's sparse piece 55% of a core, 8% with them flushed).
- Brain share on 2.5 (Phase 0's `brain_share.py`, 12 pieces, 5 minutes each): moves real 37.8, scrambled 11.3, stand-in 31.9, real with other chance 36.6; same move moments real~stand-in 25% against real~real 36% (2.4.3: 27% against 28%); fear at five minutes 0.25 real, 0 stand-in; the senses still explain 94% of escape. Full table: `/mnt/project-files/fly/phase1/brain-share-2.5.md`.
- **The plan's 5% target is not met.** A cut by reach (neurons that can reach an output or the memory and hear the senses) keeps 11,556 of 11,705, so it cannot get there; only a cut that changes the brain can (by synapse weight, or a coarser step), and that has to pass the real vs scrambled vs stand-in measurement.
- `FlyData.cpp` is 11 MB of source. MSVC, Xcode and GCC compile it in CI without trouble.
- Local checks before every push:
  - pluginval at strictness 10;
  - `tools/discovery_audit.py` with 0 FAIL. Its 2 or 3 WARNs are MONO_BREAK from the space lean;
  - `build/flymatrix` (PASS) and `tools/fly/fear.py` (PASS).
- CI builds Windows and Linux on push; a manual dispatch adds macOS.

- Measure cost with flushed denormals (the renderer does since 2026-11-16, as the plug-in does). Without, silence-heavy material costs several times more and the numbers mean nothing.
- Every phase measures the brain's share before and after: `python3 tools/fly/probe/brain_share.py run build/material OUT`, then `report OUT`; the baseline is `docs/FLY_BASELINE.md`.

## 8. What was measured (Hakan's Warm Saw Keys loop, 3.78 s, repeated)

| | 2.1 | 2.2 (first 3 minutes) |
|---|---|---|
| Jumps per pass | always 2 | 0 to 2 (26, 16, 21 per minute) |
| Tape moves, pass vs pass | 99% alike | varies; the order of spring and dive changes |
| Scratch place, pass vs pass | 60 to 75% alike | 16 to 67% alike |
| Dives | always | 0 in minute 1, 3 in minute 2, 6 in minute 3 (fear rises) |
| Moods at 3 min | none | arousal about 0.5, fatigue about 0.35, fear about 0.35 |

Learning-rate sweep (fear after 30 s / 2 min / 4.5 min):

| Learning rate | 30 s | 2 min | 4.5 min |
|---|---|---|---|
| 0.01 | 0.04 | 0.13 | 0.21 |
| 0.03 | 0.12 | 0.33 | 0.61 |
| 0.1 | 0.36 | 0.88 | saturated |

0.06 was chosen.

Reports for Hakan are in the project files:
- `dendrite-home/reports/fly/` (videos `fly-1-it-hears.mp4`, `fly-2-live.mp4`, `fly-3-inner-life.mp4`; A/B audio in `audio/`);
- listening sheets in `dendrite-home/listening/` (`dendrite-fly-1.md`, `dendrite-2.1-fly.md`, `dendrite-2.2-fly.md`).

## 9. Training: status and plan

**Round 1 is out (2026-11-14), waiting for Hakan's pick.** It ran on `tools/fly/taste/loops/fly-keys_127bpm.wav` (made by `make_keys.py`, because the playground's loops folder was empty): six 90 s renders, in `C:\Claude-Playground\dendrite\listening\fly-taste-01\` on his computer and in the project files under `fly/taste/round01/`. Record his answer with `taste.py pick 1 <letter>`.

**The workflow since round 3:** Hakan found six 90 s files too slow. Now a round is 4 variants (`--n 4`) and `tools/fly/taste/player.py NN <renders> out.html` builds one page (published as an Artifact) where all flies play the same 60 s (first 8 and last 8 passes of the loop), keys 1 to 4 switch between them at the same spot, and yes/ok/no marks make one line he pastes back. Rounds 1 and 2 are picked (see `rounds/*.json` with his words): he wants a calm fly with few, big tape bends and no saturating fear; the shipped 2.2 fly was "not quite my tempo".

**Found while making the loop:** on dense material (the same keys with a kick on 1 and 3, a rim and eighth-note hats) the fly jumped once in 90 s, against 25 times on the sparse keys alone. Its looming detectors habituate (use 12 per second, 10 s to recover), so a steady groove blinds its eyes and only the scratch is left. This is the next thing to fix after the taste rounds settle: candidates are habituating each eye band to its own rhythm (so only what breaks the pattern looms) or a slower, shallower tiring.

Tooling: `Fly::Taste` holds the eight numbers taste training moves (learn, forget, restless, jumpy, scratch, lurch, lean, calm; defaults = the shipped fly, renders bit-identical). `build/render --taste learn=0.1,jumpy=1.2` sets any of them. `tools/fly/taste/taste.py round <loop.wav> <outdir>` renders 6 blind variants (one is the current centre) and logs them in `tools/fly/taste/rounds/NN.json`; `taste.py pick NN <letter> [<letter>]` records Hakan's pick and moves `centre.json` 60% toward it in log space, the spread shrinking by 0.8 each round. When the centre settles, bake it into the `Taste` defaults.

Before 2.3: What exists is live learning (the fly's own aversive plasticity during use).

The planned taste training (agreed with Hakan as the next phase):
- the wiring stays fixed;
- only strengths move, as in Lappalainen et al. 2024, where the connectome was fixed and synapse strengths were trained;
- the target is his taste, through A/B rounds.

Each round:
1. Render several variants of the fly on his material.
2. He picks one.
3. Move the parameters toward the pick. These could be per-class gains (sense to brain, DN thresholds), the learning rate and the mood constants, or a low-dimensional set of synapse-class scalings.

A preference model (Bradley-Terry over his picks) or a simple evolution strategy fits the few dozen judgements he can give. Keep each round's variants and his pick in the repo (for example under `tools/fly/taste/`) so the training is reproducible.

**Phase 2 of the second review's plan, the teach page (2026-11-16):** `tools/fly/teach/` (see its README). Hakan plays the body's moves by hand on the echo in WebAssembly; takes are saved in the page's artifact store (collection `takes`) and replay natively with `teach.py replay`. `Source/dsp/FlyBody.h` is the body without the brain; the fly does not use it yet. Gate: his "feels like playing the plug-in" (collection `verdicts`), then 10 pieces x 2 takes x 32 bars, 300 moves.

## 10. Open problems and next steps

1. ~~Memory not saved with the set.~~ Done in 2.3.0 (Hakan said yes on 2026-11-14), with a right-click for a new fly.
2. ~~Fear saturates on one loop over long sessions.~~ 2.5: only bursts teach, learning goes both ways, relief after each shock; fear settles at 0.3 to 0.4. Side effect: with fear no longer near 1, up and down moves are about even (the groove had mostly dives), so the groove has more, shorter moves (38 against 25 in 5 minutes). Hakan's re-listen decides.
3. **The scratch's amount** still follows the grooming DN's timing, which is locked to the loop (about 90% alike pass to pass). Its place now varies. A sequential grooming model (Seeds et al. 2014: hierarchical suppression) would be the biological way to vary it.
4. **The antennal lobe** is bypassed (it runs away in this model). A proper fix: add the local inhibitory neurons with weights scaled, or a gain-control step on the ORNs.
5. **CPU about 12%** (section 7). Reach does not cut it; a weight-based cut or a coarser step would, if the brain share survives.
6. **Determinism.** Fixed RNG seed, so renders repeat exactly. Live, it diverges with the timing of the input, which is fine.
7. **As a separate product (DLC):**
   - The fly needs only the `Touch` interface: per-sample parameter offsets on the host effect.
   - To sell it apart, move `Fly.*`, `FlyData.*`, `FlyBrain.*` and `tools/fly/` into their own module or library (a static library target in CMake). dendrite then links it behind a build flag or licence check. A different host effect would map `Touch` onto its own controls.
   - Keep the credits and confirm the data licence first (section 4).
   - Coordinator's advice (2026-11-13): stay in this repo for now, as a cleanly separable module.

## 11. Gotchas

- Never change `PLUGIN_MANUFACTURER_CODE`, `PLUGIN_CODE`, the bundle id or the APVTS state type `"viber"`.
- Non-ASCII in C++ strings must be escapes (for example `"\xc2\xb7"`).
- In export.py, floats must be printed with a decimal point or exponent (`'{:.5e}f'`): `1f` is not a valid C++ literal.
- `/mnt/project-files` is a network mount: write, then re-read after a few seconds.
- The renderer needs no JUCE: `g++ -O3 -march=x86-64-v2 -std=c++20 -I Source tools/render/render.cpp Source/dsp/*.cpp Source/flies/flywire/FlyData.cpp -o build/render`.
