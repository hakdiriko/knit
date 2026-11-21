# Changelog

## Unreleased (the male fly, an A/B)

FlyWire's data are CC BY-NC (no selling without Princeton's permission). Janelia's male fly brain (male CNS v1.0, Berg et al. 2026) is CC BY: free to sell with credit. This adds it as a second fly you can load next to today's, so Hakan can hear whether it is as alive.

- **Two flies, one switch.** The default build is today's fly, bit-identical to 3.0.0 (renderer on keys, groove and voice: identical to the bit). A second build carries the male fly: 13,065 of his 144,076 brain neurons, 1.76 M connections, with his ears, eyes, giant fiber, grooming and steering neurons, mushroom body and smell. CI builds it for Windows and Linux as `dendrite-windows-male-vst3` (with CREDITS.txt for Janelia).
- **Calibrated to sound like the same animal.** Janelia counts more synapses per connection and predicts transmitters a little differently, so each synapse weighs half as much, inhibition a little more, and each sense reaches his brain as hard as it reached hers. His grooming, steering, excitement and mood are read at the same scale. On six loops he itches about as often as she does (pad: 8 against 23 times a minute), jumps as often, and grooms as much; his steering neurons barely fire, so he turns less on grooves.
- **The window** names him: "adult male, male cns v1.0", 754 x 496 um, hover shows Janelia body ids and types. The scale bar and legend move left when the map is narrower so the row fits.
- About a quarter more CPU than the FlyWire fly.

## 3.0.0 (the preparation: the window becomes an electrophysiology rig)

Hakan asked for a full rebuild of the window, not a reskin: the fly is the specimen, the window is the rig recording it, dark science, clinical. One rule above style: every number, trace and label on screen comes from real state. The sound does not change (null test below).

- **The specimen.** All 11,705 neurons at the places FlyWire mapped them, from the front, to scale (816 x 393 um), with calibration ticks every 50 um and a 1-2-5 scale bar that picks its length again on every resize. A neuron is as bright as its own firing over the last 300 ms, on a log scale (1 Hz dim, 100 Hz white, legend under the map). Under that, every spike of the session leaves it a little brighter for good (the exposure), saved with the set.
- **The electrode** is the old light: its place over the brain is feed (up) and space (right), the same parameters and automation, with the same drag, shift, wheel and double-click. Its ring is its real 25 um recording radius; it reads the neurons under it (count, mean rate, the busiest one by its FlyWire cell type). Its place is marked on both axes.
- **Throw** = press the specimen anywhere away from the electrode and hold (a hairline joins the press to the tip).
- **The instruments** (right column): input and output levels, tempo; the electrode's readings; the delay line as it is (each head's time in ms and gain per pass in dB, return, spread, diffuse, the throw's envelope, the afterglow, correlation); the specimen (how awake, the whole brain's spikes per second, arousal, fatigue, fear and memory with ten minutes of history, escape move and semitones, grooming band, steering). Hover any line: what it measures. No decay time: a formula from the heads' gains alone ignores the tape's losses and would call a dying tail "sustained".
- **The raster**: 13 of the fly's real populations (ears and eyes per side, smell, Kenyon cells, MBONs, PPL1, PAM, steering per side, grooming, giant fiber), each cell the population's mean rate in the recorder's real bin (printed). It stands still while the fly rests.
- **The log**: session, restored history, input lost and back, every reflex (move, furthest pitch), groom (band, length), throw and runaway (length), and every switch the hand or the host turns, with the session clock; newest white at the bottom. Lines keep still holds on tape carry a small square: drag them into Live as before (shift: everything).
- **Scars.** At each reflex the 64 neurons firing hardest are burned into the map as crosses: white when made, settled to mid grey within a minute, adding up where they overlap, never removed, saved with the set. New in 3.0 (no earlier set has any).
- **Who each neuron is.** Hover the map: the nearest neuron's FlyWire root id, cell type, transmitter, class, side, rate now, its neighbours within 5 um and its scars. All 11,705 were matched to FlyWire 783 by place and proven by rebuilding the cut's whole wiring from FlyWire's connectivity: identical, synapse for synapse (`tools/rig/identities.py`).
- **States.** The header says recording, no input or at rest, with "output bypassed" in front when the host bypasses (the output goes dry, the specimen keeps recording). At rest the raster says its brain does not step. No intro, no ghost-hand demo: the clock starts at t+00:00:00.0 and the log's first line is the session.
- **Look.** Black, five steps of grey (live, bright, mid, dim, floor), one face (Fragment Mono, 12 px for data, 11 px for small labels, raised from 10 after a check at 100% Windows scaling), hairlines one physical pixel wide. The dark red, Urbanist, the lamps, the field's dendrites and the fly drawer are gone. The arcade window is kept on branch `ui-simulative-arcade`.
- **Window**: 960 x 680 by default, 900 x 640 at least, any size up to 8K; the column keeps its width and the specimen and raster take the room.
- **The sound is bit-identical to 2.6.0.** `tools/nulltest` ran the loop and a song through the processor of 2.6.0 and of 3.0.0 at 44.1 and 48 kHz, 512- and 333-sample blocks, with a throw and an itch selector change: 8 of 8 outputs identical to the bit. The audio thread gains one relaxed store per block (the afterglow level, for the window).
- The manual is rewritten for the rig, in the same face and greys.

## 2.6.0 (the afterglow: the itch keeps feeding itself, then dissolves)

Hakan, in his demos: the itch repeated two or three times and then suddenly disappeared. He wants it to keep feeding itself, then dissolve smoothly, and any resonance in the tail to be psychoacoustically trippy.

- **Why it disappeared.** The itch's throw rode the echo's own loop: for about a bar the feedback rose and the return was lifted, then both fell back and the ducking hid the rest. Worse, the two tape heads wobbled each on their own: in a long, regenerating tail they drifted against each other until their repeats cancelled, and the tail fell 15 dB in one beat. That happened after a throw and four bars into any break.
- **The afterglow.** What the itch throws now goes onto a tape of its own, fed by the dotted head alone, so it cannot cancel itself. Its first repeats land like a throw (+8 dB, settling over a bar or two), it keeps feeding itself at the level of the echo for about three bars, then dissolves along a half parabola in dB (no corner where it stops): -60 dB after 7 bars at whisper, 9 at dub, 13 at endless. As it fades, every pass gets a little darker and more blurred, and it drifts further out around your head. It ducks half as deep as the echo, so the next chord doesn't swallow it. The echo itself no longer rides on the itch's throws: it plays the music as it does without the itch.
- **The spiral (B, the default).** Inside the afterglow's loop, a single-sideband shift moves every partial by the same few Hz on every pass (6 Hz, growing to 12 as it fades). Harmonics stop being harmonic, and the tail climbs like a barber pole after a rising move (hop, leap, rush) and sinks after a falling one (duck, dive, sink); a hold spirals the way the dice say. The Side's own loop turns a little faster than the Mid's, so the image whirls faster around your head as it fades. A (the afterglow without the spiral) is one line away.
- **Tails no longer fall off a cliff.** The tape's wow now moves both heads together, as a real tape's speed does (the quarter head by its longer distance), with a fifth of a wow of its own; and when a break outlasts the fill's four bars, the fill lets go over about two bars (it used to take a tenth of a second) and the loop eases off a little over the next four. A break's tail now dissolves over about five bars, never faster than 3 dB a beat, instead of dropping 15 dB in one beat, and is silent (under -120 dBFS) 25 seconds into the break. While music plays, the echo's level is unchanged (within 0.2 dB).
- Measured on Hakan's Warm Saw Keys loop and the groove, two minutes each: the wet is 0.4 to 2 dB quieter on average than 2.5 and its peaks 1 to 2.5 dB lower (no build-up from overlapping afterglows); in a break after one itch, 2.5 fell from -34 to -56 dB in two beats, 2.6 falls smoothly to -60 over about five bars. The fly's 28-case matrix passes.
- **"itch selector".** The red word in front of the line of dots now says what it is: the itch selector, in the same type as before (Urbanist, red, since it is the itch's). The host shows the same name for the parameter; its ID stays `frames`, so saved sets and automation keep it.
- **It says what it does.** Rest the mouse on the name or the dots: "how restless the fly inside is: ¼ it can't sit still, 8 it dozes, off it sleeps". Since 2.1 the numbers no longer count bars between itches; they set how awake the fly is (¼ most, 8 least). The manual's itch section says the same.
- Tools: `build/render --glow 0|1|2` (2.5's ride, A, B). `--glow 0` is bit-identical to 2.5. Renders in `dendrite-home/reports/2.6/audio/`.

## 2.5.0 (the fly, hardened: Phase 1 of the second plan)

- **Fear can fall.** The dopamine neurons fire steadily all the time, and until now that steady firing taught the memory as much as the jumps did, so fear climbed to 1.0 within two minutes on any loop and stayed. Now only bursts above each MBON's own level teach, learning goes both ways (dopamine before a Kenyon cell fires gives back, Handler et al. 2019), and a moment after each jump's sting the reward neurons (PAM) fire: relief (Tanimoto et al. 2004). Fear is now the balance of the punished against the rewarded compartments. After five minutes it sits at 0.3 to 0.4 on the groove and the keys, and rises when the music changes.
- **A real turn.** The space leans by the fly's steering neurons, DNa01 and DNa02 on each side, instead of every descending neuron's left against right. They follow where the sound is (correlation 0.64 with a groove panned bar by bar).
- **The material matrix** (`tools/fly/matrix.cpp`): 26 cases, every tempo from 70 to 174, 3/4, 6/8, 7/8, Live stopped, tempo changes, loop jumps, silence, mono, one side, 44.1 to 192 kHz, odd block sizes. All pass. `tools/fly/fear.py` measures the fear target.
- **Cheaper bookkeeping**, with bit-for-bit the same sound: a lookup table for what each spike does, and the spike search skips quiet groups of neurons. About 10 to 12.5% of a core (was 13%); the slowest 128-sample block takes under 80% of its time. The plan's 5% target is not reached (see the handoff, section 7).
- **Ready to download.** A `v*` tag now builds Windows, macOS and Linux, validates all three, and publishes a release: a Windows installer (puts `dendrite.vst3` in the system VST3 folder and removes an old `viber.vst3`), zips for each system with the manual, install notes and `CREDITS.txt`, and checksums.
- **Credits.** The fly's data is FlyWire's, under CC BY-NC 4.0; `CREDITS.txt`, the manual's footer and the README now say so, with the papers to cite.
- **A new fly without a hiccup.** Right-click "a new fly" reset all million synapses on the audio thread; only the 28,761 that learn ever change, so now only those are reset (the same fly, about 35 times less work in that one block).
- **Known limits.** The fly costs about 10 to 12% of one core per instance at 48 kHz (about 22% at 192 kHz). Its busiest moments land in single blocks: the slowest 128-sample block at 48 kHz uses about 80% of its time, and at 192 kHz with a 256-sample buffer close to 100%, so at 192 kHz use 512 samples or more, or set the itch to off. The matrix now has those two high-rate cases.
- The release also carries the fly's measuring tools (Phase 0) and the teach page (Phase 2); neither changes the sound.
- **Honest words.** The manual's itch section described the 32-neuron creature of 2.0; it now says what the fly decides and what we built, and that what wakes it is mostly how hard the hits land. The drawer says it too.

## Fly instruments (tools only; the plug-in sounds exactly as 2.4.3)

Phase 0 of the fly plan: "does the brain matter?" as a number anyone can re-run. See `docs/FLY_BASELINE.md` and `tools/fly/probe/README.md`.

- `build/render --fly-dump` writes every neuron's spikes, the senses and the outputs every 50 ms; `--scramble` shuffles the wiring's strengths; `--surrogate` swaps the brain for a fitted loudness filter; `--stats` measures the slowest audio block; `--bar` sets the time signature.
- `tools/fly/probe/brain_share.py` plays twelve pieces with the real fly, a scrambled fly and the no-brain stand-in, five minutes each, and reports how much of the fly's behaviour its senses alone explain.
- `tools/fly/probe/material.py`: the material set. `tools/fly/goal.py` also reports fear at five minutes, the longest run of one move, moves per bar and the slowest block.
- The probes are off in the plug-in. Fly logs with them off are identical to before.
- The renderer now flushes denormals, as the plug-in does (`--denormals` turns that off). Before, silence-heavy material rendered several times slower, so the renderer's timing could not be trusted.
- The baseline's own numbers are in `tools/fly/probe/baseline-2026-11-16.json`, and the stand-in it used is in `standin-2026-11-16.txt`, so later phases can diff against them.

## Tools: the teach page (fly phase 2, the plug-in does not change)

- **Teach the fly.** A one-page player where Hakan plays the fly's moves by hand on dendrite's own echo, over a loop: seven keys for the tape moves (hop, duck, sink, hold, leap, dive, rush), space to throw, G to freeze. The moves snap to the grid exactly as the fly's body does. Each take is saved with every key at the sample it took hold: the teaching data for the plan's phase 4.
- The echo runs in the browser as WebAssembly (the plug-in's C++, 158 KB, in an AudioWorklet), within -79 dB of the native build and with the same move beats.
- `Source/dsp/FlyBody.h`: the fly's body without its brain (not yet used by the fly itself). `tools/fly/teach/`: the page, the replay and the progress count.

## 2.4.3 (pure bends on bare material)

From the honest review of 2.4: the duck and dive played held chords a minor sixth lower and the leap a major tenth higher (one of the two tape heads landed on those), which drums hide and bare keys expose.

- **Sink**, the one bend the fly uses where there is no pulse: both heads fall by pure intervals (an octave, and an octave and a fifth) for an 8th, then the tape snaps back in a 32nd, too fast to hear as a pitch. On bare material its moves are now only hold and sink, taking turns.
- **It scratches less there**: only its strongest scratching gets through (on the keys loop the band is open 31% of the time, was 48%).
- Inside a groove nothing changes (the render Hakan said yes to is bit-identical).

## 2.4.2 (the fly reads the room)

Hakan's marks on 2.4.1: the groove yes again, the bare keys no again, however calm.

- **It hears whether there is a pulse.** It counts how many 16ths of the last two bars had a hit in the top of the spectrum (hats, a rim, a kick's click). In a groove (most 16ths filled) it dances as in 2.4.1; nothing changes there.
- **On bare material it is gentle.** With chords and rests and nothing to lean on, every move is naked. There it rests about five times longer, never rushes or dives, and half of its moves leave the tape alone: a **hold**, a throw of a dotted 8th and a held breath for a beat. On the keys loop: 9 moves in 48 bars (was 21), moving 5% of the time (was 18%).

## 2.4.1 (the fly keeps time, calmer)

Hakan's marks on 2.4: the timekeeping fly was a yes on a full groove and a no on sparse keys. The difference was how much it moved: 18% of the time on the groove, 32% on the keys, ending in dive after dive.

- **A budget.** Over any eight bars it moves for at most about a fifth of the time (a third with the itch at 1/4 bar). On the keys it now moves 18% of the time, like the groove he liked.
- **Big moves land on the one.** Leaps, rushes and dives wait for the bar line (the half bar when wide awake); hops and ducks still take an 8th.
- **No three in a row.** A third identical move becomes its small opposite (three dives become dive, dive, hop).
- The fly follows the host's time signature for its bar lines.

## 2.4.0 (the fly keeps time)

Hakan: "it feels like their only function is to fuck around... the goal of the fly should be being precise, time-wise... and that thing it does only with the tape drives me nuts."

- **The song is its wingbeat.** A real fly's steering commands only take hold at one phase of each wingbeat; its halteres keep that time. Now Live's tempo is that clock. The brain still decides what to do and how hard, from the music, but the body does it on the grid: every move starts on a 16th line (an 8th when it is half awake) and lasts an exact note value. Measured on two loops: 100% of its moves, throws and scratch changes land within 10 ms of a line (before: 29%).
- **Five moves instead of one wobble.** The tape's heads now slide in straight lines, so each slide bends the repeats by one exact interval for one exact note value:
  - **hop**: up an octave for a 16th, back over an 8th.
  - **leap**: up an octave and a fifth for a 16th, back down through an octave over a dotted 8th.
  - **duck**: down an octave for an 8th, back up in a 16th.
  - **rush** (when it is wound up): up an octave, then the echo runs in double time for a beat or two, then falls back.
  - **dive** (when it is afraid): down an octave over a half note into half time, it holds its breath for a beat, stays, then climbs back over a quarter.
  Its fear and its arousal choose the move, so the vocabulary changes as it gets to know the loop.
- **The scratch, the lean and the feed step on the lines** (16ths, 8ths and beats) and hold between them, instead of drifting.
- **It leaves room.** After a move it leaves the music alone: about a bar at the default itch, a beat at 1/4 bar.
- **Its eyes no longer go numb on a full groove.** With a kick, rim and hats under the keys it used to jump once in 48 bars; now about once in five bars (its looming sense tires more slowly and recovers faster).
- Its temperament starts from where the taste rounds 1 and 2 put it (learns a little faster, less restless, scratches a little harder, leans a little further).
- The drawer's line names the move: "leaps", "dives  half time", "rushes  double time".
- Tools: `tools/fly/goal.py` measures the goal on a render (on the grid, variety, how alike two phrases are, how much room it leaves); `build/render --loose` plays the 2.3 fly for comparison.

## 2.3.0 (the fly remembers)

- **The fly is saved with your set.** What it has learned (all 28,761 synapses its fear lives in) and its moods (wound up, tired, its feeling) go into the Live set, about 77 KB. Reopen the set and it is the same fly, still afraid of what hurt it, rested but remembering. Sets from before 2.3 open with a newborn fly, as before.
- **A new fly, when you want one:** right-click the fly's brain, small or in the drawer, and choose "a new fly". It forgets everything and calms down. Nothing else changes.
- Tools: the fly's temperament (`Fly::Taste`: how fast it learns and forgets, how restless and jumpy it is, how hard it scratches, lurches and leans, how fast it calms) can be set in the renderer, and `tools/fly/taste/taste.py` runs taste-training rounds: blind variants on your loop, your pick moves it. The plug-in sounds exactly as before.

## 2.2.1

- **The bitcrush is gone.** The fly's brain moves in 0.5 ms steps, and the echo's tape speed jumped with it every step, so its repeats crackled like a bitcrusher whenever it jumped or leaned (since 2.1). Now the echo glides to what the fly wants, sample by sample (about 4 ms). On Hakan's loop the grit above 10 kHz drops by 30 dB, to where it is with the itch off. Turning the itch off also glides back instead of snapping.
- `docs/FLY_HANDOFF.md`: the fly explained end to end (data, cut, model, inner life, mapping, view, measurements, training plan, open problems, how to split it out as an add-on), for whoever takes it over.

## 2.2.0 (the fly's inner life)

**The fly lives between the hits.** In 2.1 nothing inside the fly changed, so on a loop it did the same thing every pass: on Hakan's 3.8 s loop it jumped twice a pass, every pass, and its tape moves were 99% alike pass to pass. Now it has an inner life, all of it in its own neurons.

- **A bigger brain: 11,705 neurons and 1,034,261 connections**, now with its mushroom body (the fly's memory: 2,459 Kenyon cells, 96 output neurons, 331 dopamine neurons).
- **It smells the music.** The spectrum, in 32 bands, reaches its antennal lobe's projection neurons, glomerulus by glomerulus; only the bands that stand out count, as an odour is a few glomeruli. Its Kenyon cells answer sparsely: a code for what this sound is.
- **Restless neurons.** Every neuron gets small random kicks, as real neurons do, so it is never exactly the same fly twice.
- **Its readiness wanders.** The same hit makes it jump one time and not the next, as a real fly's escape does. On Hakan's loop it now jumps 0, 1 or 2 times a pass.
- **Moods.** Each jump winds it up (arousal; it calms over about 40 s): wound up, it jumps sooner and harder. Busy music tires it (fatigue, over minutes): tired, it rests longer between jumps and scratches less.
- **It remembers, and it learns.** When it jumps, the punishing dopamine neurons fire, and where dopamine meets a Kenyon cell that was just active, that cell's synapses onto the output neurons weaken (the way real flies learn to avoid a smell). So the sounds that made it jump stop sounding safe. It forgets over about ten minutes. While a sound is new it springs up (the tape speeds up, short); the more of what it hears once hurt, the likelier it dives (the tape slows, long, throws long, freezes), and it scratches lower. Its output neurons' "come closer" against "keep away" moves where it scratches and how much.
- On Hakan's loop over three minutes the scratch's place is now 16 to 67% alike pass to pass (2.1: 60 to 75%), the jumps vary in number and kind, and the scratching thins as it tires.
- **Watch it feel.** The drawer shows its moods and memory in one quiet line ("wound up 0.55 tired 0.23 afraid 0.20 learned 1.4%") and where its mushroom body sits.
- Its memory and moods last while the plug-in is loaded (a new sample rate or a host reset keeps them); they start fresh when the plug-in loads.
- It costs about 11% of one CPU core (2.1: about 5%).
- `tools/fly/`: `sim2.py` (the whole brain with an inner life, offline) and `prune2.py` (the bigger cut). The renderer's `--still` plays the 2.1 fly for comparison; `--fly-log` adds the moods, memory and fear.

## 2.1.0 (the fly)

**The itch is a real fly's nervous system.** Inside dendrite lives the part of a fruit fly's brain that answers music: 4,431 neurons and 300,922 connections from the FlyWire map of the whole adult brain, wired exactly as they are in the fly and run live the way Shiu et al. (2024) run the whole brain. Nothing about its wiring was designed for dendrite, and it is not trained yet.

- **It hears and sees the music.** Your track goes into its ears (Johnston's organ on each antenna: low vibration, sound, and the slow push of the level; left channel left antenna). The hits come at its eyes as something approaching (the looming detectors; lows from below, highs from above).
- **It gets used to things.** Each sense tires when the same thing keeps coming and recovers slowly. It startles at the first hits, settles into a loop, and jumps again at what is new: a break, a new part.
- **What it does moves the echo.** When its giant fiber fires it jumps: the tape lurches down (up to 5 semitones) and comes back, and it throws the moment in. A hard jump freezes the loop for a second. Its grooming (the antennal scratch) rubs a narrow band in the loop back and forth, placed where the music leaves room. Its turning leans the space; its excitement feeds the echo. As with the hand, nothing touches your knobs or automation.
- **The itch knob is how awake it is.** Off sleeps. Toward ¼ it is wide awake and recovers quickly; toward 8 it is drowsy. The five heads step aside (the code stays).
- **Watch it think.** Its brain lives where the head lamps were: every neuron at its place, flashing red as it fires and drying to grey. Click it and the window opens a drawer under the strip with the brain large, where its ears, eyes, giant fiber and grooming sit, and one line saying what it is doing. Click again to close.
- It costs about 5% of one CPU core.
- Window: in fullscreen the fly's drawer opens and closes without leaving fullscreen.
- Window: the light keeps up with the mouse while you drag it.
- Window: keep's tape and the itch controls stretch with a wider window.
- Window: background growth spreads like a rhizome instead of knotting and stacking.
- `tools/fly/`: the whole-brain simulation, the cut and the export (`export.py` writes `Source/dsp/FlyData.cpp`). The hand (`--hand`) stays in the renderer for comparison; `--heads` renders the 2.0a itch.

## 2.0.0 (2.0a: the creature)

**The itch has a mind of its own.** Every dendrite is born a small creature (32 neurons) that lives inside the itch from the moment it loads. It takes one breath per sixteenth note, hears the music, feels your hand and breathes on three slow clocks of its own. The echo, the breathing and the throw are unchanged.

- **Never the same twice.** Loop the same bars and every pass itches differently. 1.2 rolled dice by song position, so the same bar gave the same itch; over two hours of one loop 1.2 repeated 97.5% of its itches exactly, the creature 0%.
- **One animal.** An itch has one temperament: its three heads share a register, a pace and a side. Consecutive itches are related, the way one person's gestures are, and it tires of turning the same way three times running.
- **Tied to the music.** Bright music itches higher, busy music quicker, a dark sparse part low and slow. It grabs the hit that matches its mood: a dark mood takes the kick, a bright one the hat. Heavy low end leans to manipulate, dense highs to swarm, sparse stabs to ghost and laugh.
- **Tired heads rest.** A head it used a lot sits out for a while, and it takes the freshest of its last two bars of thoughts against its diary.
- **It decides when.** Its own patience, how well it knows the loop (a loop it has scratched three times bores it sooner) and how wound up your throws and moves made it set how much repetition it takes; 1.2 always waited for 0.72.
- **Your hand winds it up.** After a run of throws and fast moves its itches leave more on the tape and their twist of the space heals in about 25 s instead of 50.
- **It remembers.** Its life (about 5.5 KB: genome, age, mind, clocks, tiredness, habits, 32 loop fingerprints, a diary of its last 64 itches) is saved with the Live set. Reopen the set and it is the same creature. Sets from before 2.0 open with a newborn.
- The one-page manual now covers the creature, keep and first light (still one page, same voice).
- Window: maximise and fullscreen now work; the window takes any shape and the field grows to fill it.

## 1.3.0 (keep, first light)

Nothing here changes a sample of the sound.

**keep: dendrite never forgets the last minute.**

- It always records about a minute: everything it plays, and the itch on its own (the heads, no dry, no echo). Next to that it keeps a diary of every itch (with its heads and length), every throw and every runaway. That diary is also the creature's first memory.
- **Embers.** After an itch, its three lamps keep a faint glow while keep still holds it, dimming as the minute forgets it. Drag one into Live and you get that itch alone, from the bar line before it to the bar line after its tail. Hold shift to get everything that played over those bars.
- **The tape.** Move the mouse into the strip and a thin tape appears along its top: a minute ago at the left, now at the right, with bar ticks. Red marks show itches, grey marks show throws and white marks show runaways. Drag a mark out to take it. Press anywhere else on the tape and pull: everything from that bar up to the last full bar.
- **The ruler.** While you hold a mark, a ruler opens over it with the take's waveform, its bars and beats (1 2 3 4), which heads played, how many bars, and the tempo.
- **The files** go to Documents/dendrite/kept/ and are never deleted. They are 32-bit float WAVs, a whole number of bars long, with the tempo and metre in the ACID chunk. Each name says what it is: `dendrite itch · scream laugh ghost · 2 bars · 124 bpm · 21-43-07.wav`. Dragging the same itch twice reuses its file.
- In Live on Windows, dendrite gives Live's window the focus before a drag starts. Live can crash when another window has focus at that moment.

**first light: the window shows itself.**

- The first time dendrite opens on a computer, a ghost hand shows how it works in about 24 silent seconds: it drags the light, presses the dark to throw, lets the itch fire three heads, and pulls an ember out of the window. One deadpan line goes with each step.
- It never touches your sound or your knobs; it moves a puppet of the light. Any click ends it, and clicking the wordmark plays it again.

## 1.2.0 (your critique, answered)

**Level: the same dendrite at any track level.** Nothing is normalised: a quiet track gets a quiet echo, a loud one a loud echo.

- **Your dry is never squashed.** The safety limiter used to pull the whole mix down when a hot track (peaks near 0 dBFS) and its echo didn't fit together, up to 5 dB on a stab. Now only the echo steps aside under each hit, the way an engineer rides the return down, and comes back in the gap.
- **The first seconds sound the same at any level.** The echo used to start from values that suited a medium-loud track, so a quiet or loud one sounded different for 5 to 10 seconds after play.
- **The itch hears quiet tracks.** A loop at −60 dBFS never itched; now it itches exactly when a loud one does. From −66 to 0 dBFS the echo matches within 0.1 dB and the itches land at the same moments.

**The itch and whisper**

- **Off means off.** Turning the itch off fades a running itch out within 100 ms (it used to play to its end).
- **Short lengths stay events.** With itch lengths under two bars there are now at least two bars of quiet after each itch (it was one), so ¼ bar no longer itches nearly every other bar.
- **More to grab.** Each itch takes one of the strongest hits of the last bar (any within 3 dB of the loudest), not always the loudest, so a drum loop doesn't hand the heads the same kick every time.
- **whisper is easier to hear:** it ducks less under what's playing, about 3 dB more present on a busy part.

**New**

- **The runaway.** Hold throw at endless and the loop may now grow past what you played, climbing into a howl that the tape's saturation holds. Let go and it falls back like any throw.
- **time:** dotted (dotted ⅛ + ¼, as before), triplet (¼ triplet + ¼) or half (dotted ¼ + ½, half-time dub). Switching glides the heads like tape.
- **dry:** on by default. Off for a return track in Live: only the echo and the itch come out, and a send feeds it.

**The window**

- Every hit and echo sends impulses along the strands; held sounds glow. The window follows loudness relative to your track.
- The background lives: a slow tide moves the medium, tips drape and sway, strands grow, branch, wither and pull back, and where two touch they join into a synapse that carries impulses and can snap.
- "feed" and "space" sit faintly at the field's edges while you hover; resting in the dark shows a faint nerve to the light. A press in the dark throws only after 80 ms, so clicking to focus the window doesn't throw. Tooltips name the hidden moves.
- Head names stay faintly visible; itch lengths are faint ticks until you reach for them; the chosen itch LED warms as repetition builds; small text never drops under 10 px.
- time (dotted / triplet / half) and dry on/off are clickable words on the itch strip.
- A runaway howls: the light swells and wobbles, the tissue strobes, impulses pour out.
- The hit tremor shivers each strand as one body (dense repeats no longer zigzag the strands).
- The light shows your input's level: a tight shimmering skin when it comes in hot, a dimmer core when it's very quiet, with a tooltip saying what to do.

## 1.1.1 (two things you found)

- **The window answers on your second monitor.** On a monitor with a different display scale (125 % next to 100 %, say), Windows handed the window's mouse positions over in the wrong scale, so clicks missed everything. dendrite is built on JUCE 9.0.5 now, which fixes that and keeps the window's size when you drag it between monitors.
- **itch hears the rests of a loop.** A silent sixteenth counted as "not repeating", so a stab track with a few short notes, and a lot of air between them, often never itched at short lengths (on our test stabs, 131 of 216 patterns never did; now all of them do). Now the rests belong to the loop; only more than two bars of silence counts as the music stopping. Loops that itched before itch at the same moments.
- Window: every stab sends a shiver through the tissue behind it.
- Window: the itch's lights are red LEDs in the dark (rendered in Blender).

## 1.1.0 (your 1.0 review)

What you wrote in the review checklist, and what changed in the sound:

- **The rumble at 220–320 Hz is gone.** At dub the loop gained a little on every pass, so one hit's echo kept growing for about 6 seconds until the ceiling held it, and what grew was a 250–630 Hz drone. Now every pass thins the low mids a little (the tape's colour on the first repeats is the same), dub sums to just under 1 so its echo fades out on its own, and filling the silence can only take the loop as far as feed allows. whisper is a whisper again: one hit's echo is about 30 dB down after four seconds (it used to drone through the gaps).
- **The echo leads, the wash follows.** The tape's smearing delayed every repeat by 41 ms per pass, so the "dotted ⅛" was really 416 ms at 120 BPM, and each repeat drifted further off the beat into a wash (it was the same in 0.5). Repeats now land exactly on the dotted ⅛ and the ¼, keep their edge for a few passes, and only then dissolve.
- **itch has five heads now, three per itch.** Each itch draws three of them in an order of its own, never the same three twice running:
  - **scream** now really happens around your head: the climbing throat circles you once or twice as it rises.
  - **laugh** was the weak one (about 10 dB under scream). It's levelled now, and each "ha" hops from one side of your head to the other, sometimes a little behind.
  - **manipulate** (slowed, falling, swinging around you, turning the echo's space) is unchanged in character.
  - **swarm** (new): the attack splinters into a dozen grains all around you, each with its own pulse. They pull each other into step and close into one hit in front of you, on the beat.
  - **ghost** (new): the hit's air turns into a whisper at one ear, which passes behind your head to the other.
  - All five sit within about 2 dB of each other, and none ever peaks louder than the hit it came from. Every head is placed with the cues your ears use: the time and level difference between them, the head's shadow, a darker sound from behind. Headphones show it best.
- **Shorter itch lengths.** ¼, ½ and ¾ bar join 1, 1¼, 1½, 1¾, 2, 3, 4 and 8 (default 4). With short lengths there is always at least a bar of quiet after each itch.
- Sets saved with 1.0 open with itch at its default (4 bars). Everything else keeps its settings.

And in the window and the manual:

- **True black, like the inside of a brain.** No panels or frames: the field is the whole window and its edges sink into black. Behind it, living tissue: strands grow in, sway, pull back after a minute or so and are replaced, and impulses run along them, more of them when the echo is loud, when you throw and when it itches.
- **The window floats in a fluid:** dragging the light stirs it, every hit pushes a ring outwards, and the strands and branches are carried by it.
- **Strands and branches are rods** that bend, drape, whip and tear (XPBD), and each tree grows toward its own scattered cloud of points (space colonisation). The young ones hang on the light and trail behind when you drag; the old ones root, peel off and stay where you left them.
- **throw has no button.** Grab the light to move it; touch anywhere else in the dark to throw, and a nerve runs from your finger to the light. `throw` is still there to map to a key in Live.
- **Each head leaves its own trace:** scream a tendril circling as it climbs, laugh twigs from side to side, manipulate a spiral, swarm grains that close in on the light, ghost pale strands passing from one side to the other.
- **The itch's lamps and bead are real glass**, rendered in Blender; they glow as each head plays.
- **Type:** Urbanist for the name and words, Fragment Mono for numbers (your signature pair), in the window and the manual.
- **Much less text.** The name, "itch" with its length, the head names only while they play, and two words under the light while you touch it.
- **Monochrome:** black, greys and white, and one colour, dark red, which belongs to the itch: the echo grows in grey and white. The same in the window and the manual.
- **The manual** is rewritten on true black, lowercase, about half the words, and with the copy for Corpus in the voice of Norman Leto's Photon (your reference).

Checked: pluginval strictness 10, discovery audit 0 FAIL / 0 WARN, mono residual −130 to −139 dB.

## 1.0.0 (dendrite, finished: a dub echo that grows around your head)

A new instrument, built from what survived every test: the dotted dub echo of 0.5, the breathing you liked, the mono-safe space you asked for after the stab test, and the three heads of the hypnagogic demo. Four controls, a window to play, and a one-page manual in every download.

- **The field.** One light to drag. Up is **feed**, how long the repeats live: off, whisper, dub, drown, endless. Right is **space**, where they live: inside, around, envelop, beyond. Every hit you play grows a branch on the field, and the repeats branch off it at the dotted ⅛ and the ¼. Shift-drag for fine moves, double-click for the default (dub 50, around 40), the mouse wheel moves feed (sideways: space).
- **The echo.** Two tape heads in one loop, the dotted eighth and the quarter of your project's tempo (120 BPM without one). Every pass loses a little top, gains a little grit and wobbles with a slow wow that never repeats (the tape colour of 0.5, unchanged). The dry passes untouched and there is no mix knob.
- **The space grows around your head.** The ⅛ keeps the side and the ¼ crosses it, so you hear the dub ping without hard left and right. Every pass writes a little of the middle into the side again, so each repeat steps further out. Envelop dissolves the side with every pass; beyond also turns it slowly around you. Fold to mono and you hear the plain dub echo: space changes the mono sum by level only (measured −139 to −141 dB residual).
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
