# Handover: dendrite hypnagogic (0.6.0 demo)

Written 2026-10-30 at about 23:00 (Istanbul time is UTC+3), when Hakan paused the work to shut down his PC.
**Status: paused, waiting for Hakan's approval before the next part.** Everything below is committed and pushed to branch `claude/project-thread-symtop` of `hakdiriko/knit`. No pull request yet, nothing merged, `main` untouched.

---

## 1. What Hakan asked for

From his message in the project thread (30 Oct, 22:12 UTC), in short:

- A **completely new branch** of dendrite, "hypnagogic". Analyse the repo deeply, but **don't reuse a single line** of the old plug-in.
- **Drop the classic delay + reverb approach.** Techno is about the psychoacoustic perception of space. Make it a **binaural spatial experience**, a fit for dub techno and beyond.
- **Small knob moves ("swing and rotate a little") must make huge, indescribable shifts** in perceived space.
- **The itch:** the plug-in itches when the music gets too repetitive. Time frames like 1, 1.25, 1.5, 2, 4 bars, **set by a simple drag**.
- When it itches, it plays **three heads, like the RE-201's tape heads** ("hats" was a typo): first it **screams**, second it **laughs**, third it **gets manipulative**.
- From the earlier dub thread: 0.5's throws were shallow; he wants **mono-safe phase-correlation tricks** (Waves PS22 Spread as inspiration). His mottos: **minimalism** and **organic** (micro-modulation, warmth, randomness). He dislikes **timed, cheesy gestures**.
- Demo first, then he sends psychoacoustics research for a second pass. **Don't merge into main**; the PR stays open as a demo branch.

Mid-session he raised the effort to extra high (done) and asked for a review of everything written so far (done, see §4). He then sent his research early (§5), and finally asked to pause, commit, write this handover, and wait.

---

## 2. Where everything is

| What | Where |
|---|---|
| Branch | `claude/project-thread-symtop` (from `main` at PR #5, dendrite 0.3.0) |
| Plug-in code (all new) | `Source/Processor.*`, `Source/Editor.*`, `Source/space/Tools.h`, `Source/space/Field.*`, `Source/space/Itch.*` |
| Build | `CMakeLists.txt` (rewritten), JUCE 8.0.15 via FetchContent |
| CI | `.github/workflows/build.yml` (rewritten): Windows, macOS, Linux; pluginval strictness 10; AU validation; discovery audit on Linux |
| Audit | `tools/discovery_audit.py` (kept and extended: hears each ear, checks mono fold-down) |
| Design decisions | `DECISIONS.md`, section "The hypnagogic branch" (one line per decision, plus why) |
| What changed, in sound words | `CHANGELOG.md`, "0.6.0 hypnagogic" |
| This file | `docs/HANDOVER-hypnagogic.md`, copy in the project folder `dendrite-home/handover/` |
| Old engine (0.3) | still on `main`; 0.5 dub echo is PR #7 (unmerged) |

Identity is unchanged: codes `Hakd`/`Vibr`, bundle id `com.hakdiriko.viber`, state type `"viber"`. The product name is now lowercase **dendrite**, so the file is `dendrite.vst3`. Old Live sets load this build in the same slot, with default settings (none of the old parameters exist here).

---

## 3. What was built

### The invariant: the space lives only between the ears

Everything dendrite adds is a Side signal made from the Mid: **L = M + S, R = M − S**. So the mono sum is the dry Mid by construction. The only thing that touches mono is a level trim (at most about −1.2 dB at full depth, so deeper doesn't just sound louder). Nothing below about 150 Hz goes into the Side, so the sub stays centred.

### The engine (per sample)

1. **Analytic pair** (`Tools.h: Analytic`). A 10 ms linear-phase FIR band-pass (150 Hz to 18 kHz, Kaiser β 6) and its exact Hilbert twin. Their outputs are exactly in phase with, and exactly 90° from, the dry delayed by 5 ms. The dry is delayed to match, and 5 ms of latency is reported to Live. This is what lets a "turn" move the whole image as one (see §4 for why).
2. **Attack detector** (`Processor`). It compares a fast and a slow envelope of the Mid, and it runs on the *undelayed* input, so it knows about each onset 5 ms before the audio arrives.
3. **Near path** (`Field`). The analytic band, delayed 0.04 to 0.42 ms, then turned by an angle. The angle trades level cues (ILD) against time cues (IPD):
   - at the sides, both cues agree, so the image leans there;
   - in front and behind, they disagree, so the image splits by frequency: lows one way, highs the other.
   Near those conflict zones, a few degrees decide which cue wins. That is the "small move, big shift" mechanism. The tiny delay adds a PS22-like interleave across frequency.
4. **Diffuse path** (`Field`). 32 second-order allpasses, one per ERB band (critical band) from 150 Hz to 16 kHz. Flat magnitude, no echoes, no tail: only phase scrambled where the ear resolves it. Depth narrows the poles (2.2 → 0.32 ERB wide): more scramble, longer group delay, farther away. Attacks bypass it (input × (1 − 0.9·attack)), so drums stay sharp.
5. **Organic drift.** Slow, smooth, seeded random paths (three eased layers at golden-ratio periods, never raw random steps) move the angle, the near delay, the levels and the pole positions.
6. **Output.** Level trim, then a click-free bypass that keeps the same latency, a −0.3 dBFS safety limiter, and a NaN/Inf guard.

### The controls (no knobs)

| Parameter | ID | Range / default | What it does |
|---|---|---|---|
| orbit | `orbit` | 0..1, wraps; 0.5 = front, 0.25 left, 0.75 right, 0/1 behind | the angle around your head, plus a seeded terrain of hidden settings around the circle; behind is slightly darker (pinna cue) |
| depth | `depth` | 0..1, default 0.45 | inside (IC ≈ 0.97–1) → around (≈ 0.9–0.65) → envelop (≈ 0.5–0.3) → dissolve (≈ 0.1, slowly rotating, barberpole-like endless rise) |
| itch frame | `frame` | off, 1, 1.25, 1.5, 1.75, 2, 3, 4, 8 bars; default 2 | how long it can stand the same thing |
| bypass | `bypass` | | host bypass, click-free |

**The window** (`Editor`) is a disc seen from above, with you in the middle facing up. The point's angle is orbit and its distance is depth, measured like a Poincaré disc: equal steps of depth crowd toward the rim, so the outer ring holds the most change per millimetre. Dragging moves the point by how far you drag (it never jumps); shift is 5× finer; double-click returns home. Inside the disc, the output is drawn as Mid (vertical) against Side (horizontal), with a correlation readout. Below the disc: the itch strip (drag to choose the frame), a fill showing how repetitive it sounds right now, and three lamps for the heads.

### The itch and the three heads (`Itch`)

- **Listening:** 6 band envelopes of the Mid, sampled every sixteenth, each compared with the same sixteenth 1, 2 and 4 bars earlier (best match). A frame itches when its average sameness is above 0.72. Silence never counts. It uses Live's tempo, bars and position when the transport plays, and a free 120 BPM otherwise.
- **Relief:** frames that start while the heads are playing don't count. So on a steady loop it itches every third frame, which is co-prime with 4-bar phrases: the itch walks across the phrase instead of always landing in the same spot. Each consecutive itch is stronger; a frame of new material resets that.
- **The tape reading:** the same moment passes head 1, 2 and 3 at 1, 2 and 3 quarters of the frame (the frame is the tape speed), with a little seeded jitter.
- **Seeded by song position:** the same bar gives the same itch, so exports match what you heard.

| Head | Reading | Mechanism |
|---|---|---|
| 1 scream | a ghost only two ears can hear | a narrow band (Q 9) around 520–790 Hz plus its octave, climbing 0.5–1 octave, turned about 90° between the ears with a fast flutter; the octave tears the other way. Huggins-pitch mechanism: each ear alone hears at most a mild resonance, while both together hear a voice inside the skull. It stays below 1.6 kHz because that's where the ear reads interaural phase. The rest of the space contracts while it plays. |
| 2 laugh | the whole image giggles | a burst of hops (0.45–3 s) of the whole image's angle, about 5 per second, slowing 5–10% and fading each hop, mostly alternating sides but not always (the rhythm of a human laugh) |
| 3 manipulate | Franssen | the body of the sound turns ½ to 1½ half-turns and spins up and back down, while the attacks keep their old angle, so the ear believes nothing moved. The decorrelator's poles narrow (the room swells). Then the attacks catch up over 1.4 s, and the new place heals back over about a minute. |

All three only add Side, so they vanish in mono.

---

## 4. The review at extra-high effort (what changed and why)

The first draft used a zero-latency IIR 90° phase pair (Niemitalo's allpass chains) to turn the Side. Re-checking it numerically showed the flaw: that pair is 90° *apart from each other*, but each chain disperses relative to the dry, by about 5 ms at 50 Hz and 0.3 ms at 1 kHz. So the "turn" put every band at a different angle. That is phasing, not a coherent space, and Hakan already disliked phasing on drums in 0.2. Fix: the exact linear-phase analytic pair, at the cost of 5 ms latency (Live compensates). Verified numerically: exactly −90.0° at every frequency, with matched magnitudes (−6 dB at 150 Hz, −0.2 dB at 300 Hz, flat above).

Other fixes from the review:
- The first diffuser (Schroeder allpasses) turned into audible slapback at large sizes. It was replaced by the ERB allpass decorrelator, which has no echoes at any setting.
- The itch had no relief, so the heads played about half the time on a loop. Now there's always a full frame of rest.
- A stale "ha" from a previous laugh could replay on the next itch (bug, fixed).
- The manipulate head's spin used to leave the body and the attacks permanently apart. Now the spin is folded into the place that heals.
- The first knob design had orbit as 3 turns. On the disc it's one lap, which matches "where around my head".

---

## 5. Hakan's research: used now, parked, and my opinion

His document "Psikoakustik DSP ve VST Yaklaşımları" (in Turkish; it arrived mid-build).

**Used in this build:**
- ERB-spaced perceptual allpass decorrelation → the diffuse path.
- Transient / body split and the Franssen effect → attacks anchored, manipulate head.
- Huggins dichotic pitch → scream head.
- Barberpole / Shepard idea → dissolve rotates so the interleaved bands rise endlessly (via SSB-style rotation of the Side, mono untouched).
- Poincaré disc surface with exponential room near the rim → the window and the depth mapping.
- MPEG-I-style energy matching, in spirit → the half-power level trim.

**Parked for the second pass (my view, with pushback where I have it):**
- **Velvet-noise decorrelators (OVN/SVN/DVN):** a good candidate for the dissolve regime or as a Huggins carrier on sparse material. Next to try.
- **Kuramoto coupling:** worth a test as "coherence": modulators locking together = the space snapping into focus. It's a real, audible idea.
- **Chaotic attractors (Rössler/Lorenz):** our seeded smooth wanders already never repeat. Chaos would add character only if we hear it; low priority.
- **Autocatalytic resonators, harmonicity analysis, "Bloom":** they generate pitched content, which is a new instrument inside the instrument. Big; only if the space core convinces you first.
- **Wasserstein / optimal-transport morphing:** expensive and hard to make real-time. The disc plus terrain already morphs smoothly. Low priority.
- **Zwicker tone:** needs a notch in the Mid, which breaks "mono = dry". Only as a deliberate, optional exception.
- **HRTF / pinna elevation (KEMAR, CIPIC):** possible, but true HRTFs colour the mono sum unless they're applied only to the Side. Worth a careful look in pass 2.
- Several numbers in the document (for example "−90 dB IR thresholding", "R₀ = 0.9830", "3× efficiency") are from specific papers and contexts. I treat them as pointers, not as specs.

---

## 6. Verified so far

- Builds clean on Linux (no warnings in our code).
- **pluginval strictness 10 passes** (local, Linux, VST3).
- Offline measurements (pedalboard, 48 kHz, pink noise unless noted):
  - Mono sum vs dry: residual −50 to −78 dB after a gain match; level −0.03 dB (depth 0) to −1.15 dB (depth 1). On a drum loop with the heads firing: −44 dB.
  - Interaural coherence by depth (bands 150 Hz to 16 kHz): depth 0 → 0.97–1.0; 0.2 → 0.90–0.96; 0.45 → 0.65–0.82; 0.7 → 0.33–0.64; 1.0 → 0.07–0.61.
  - Orbit: left (0.25) leans +3.5 to +4.9 dB ILD below 1.2 kHz; right mirrors it; front splits (lows left, 2.4–4.8 kHz right). These were measured before near gain was raised about 25%, so the leans are now a little stronger.
  - Itch on a 1-bar synthetic dub loop at 120 BPM, frame = 1 bar: it fired at 4 s, 10 s and 16 s (every third frame, as designed). The scream band rose +5 to +9 dB in the Side; mono changed by at most 0.013 between itch on and off.
  - CPU: about 21× real time for one instance at 48 kHz on this cloud machine (about 5% of a core).
- **Discovery audit: 0 FAIL, 2 WARN** (local, Linux). 300 random states: 0 broken, 88% alive. Mono fold-down at default: −58 dB (percussive), −46 dB (pad), −35 dB (sweep), all under the −30 dB MONO_BREAK line. The 2 WARNs are a DEAD_ZONE at the bottom of depth on the impulse and percussive signals. That's by design (attacks stay out of the space) and is justified in DECISIONS.md.

**Not verified yet:** CI on Windows and macOS (it starts with the push), the lowercase `dendrite.vst3` bundle name (renamed after the local build; CI builds it), and above all **Hakan's ears**. Nothing here has been heard by a human.

---

## 7. Next steps (waiting for Hakan's go)

1. Check that CI is green on all three platforms and its audit also shows 0 FAIL. Fix anything red. CI runs automatically on the push; that's harmless and reversible.
2. Open the demo PR from this branch to `main`, **left open, never merged** (as asked), and assign Hakan.
3. Write the listening sheet (what changed, 3 things to try on which material, 1–3 taste questions) in `dendrite-home/listening/dendrite-0.6-hypnagogic.md`, with the Windows artifact link.
4. Optionally render A/B audio sketches on his loops (`C:\Claude-Playground\dendrite\loops\`) so he can hear before loading.
5. Second pass: his ears' notes plus the parked research items above.

**Taste questions I'd ask him after he plays it:**
- Is the default (front, 45%) a good "sound on insert", or should it start more inside (subtler) or more around?
- Does the scream feel like a scream, or too polite, or too much? (It is deliberately a ghost, not a shriek.)
- Is the itch rate right? Every third frame on a steady loop; at 2 bars that's every 6 bars.

---

## 8. Notes for whoever resumes (Claude or human)

- Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`. Linux needs the apt packages listed in the workflow.
- Audit: `python3 tools/discovery_audit.py build/dendrite_artefacts/Release/VST3/dendrite.vst3`. pedalboard names the frame parameter `itch_frame`.
- **pedalboard quirk:** it reads plug-in latency once, at its first sample rate (44.1 kHz → 221 samples instead of 240 at 48 kHz). So the first render after loading is misaligned by 19 samples. The audit now renders once before measuring.
- **Keep the invariant:** never add anything to the Mid. If a future idea needs it (Zwicker, HRTF on the direct sound), make it a conscious exception and log it in DECISIONS.md.
- **Never feed raw random steps into a delay or phase** (lesson from the 0.5 sketches: Hakan hears it as noise). Use `Wander`.
- Real-time rules: no allocation after `prepareToPlay` (`Analytic`, `Line` and the `Field` arrays allocate there only); `ScopedNoDenormals` at the top of every block.
- Hakan: expert ears, never writes C++; talk sound and mechanisms; end each reply with one short, kind English tip; never bring up "metallic" (he retracted it).
