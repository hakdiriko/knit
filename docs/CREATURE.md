# the creature: main plan

Written 2026-11-12, after Hakan approved the plan doc
(https://claude.ai/artifact/DfSJ6FQ5KonkqatiBqmr8j) and gave full control.

**The goal:** the itch is alive. It thinks on its own: it decides when it has had enough
and what to do about it, from what it heard, what it remembers and what your hand did,
and it never does the same thing twice. The echo, the breathing and the throw that Hakan
approved do not change.

## What it is

A small mind (32 leaky neurons) that lives inside dendrite from the moment it loads. It
takes one breath per sixteenth note (`Source/dsp/Creature.*`, stepped from
`Engine::tickClock`). Every instance is born with its own genome (a 64-bit seed that grows
its wiring), so two dendrites on two tracks are two different creatures.

| Organ | What it does | Why |
| --- | --- | --- |
| Senses | the Listener's six bands against their own running average, the strongest hit, how repetitive the music is, where the light is | level-free, so a quiet track is heard like a loud one |
| Arousal | throws per minute, how fast the light moves, how dense the hits are; up over ~20 s, down over ~2 min | your hand winds it up |
| Body rhythms | three slow clocks at 600, 971 and 1571 sixteenths (golden-ratio spaced) | the same bar never finds it in the same state; without them a stable mind repeats 51-82% of the time |
| Mind | 32 neurons, time constants 2-48 sixteenths, recurrent gain 0.9 | stable: it can neither run away nor lock into a cycle; a kick fades in ~4 s |
| Readout | 14 outputs, each normalised against its own last ~30 minutes | it always uses the whole range: no dead zones in the mind |
| Instincts | brightness pulls the register up, density quickens the pace, low end leans to manipulate, dense highs to swarm, sparse stabs to ghost and laugh | tied to the music |
| Tiredness, novelty | used heads rest; the itch takes the freshest of the last 8 candidates against its diary, and leans away from its recent habits | never the same twice |
| Familiarity | a 64-bit fingerprint of each bar; loops it already scratched make it impatient | it knows your loop |
| Patience | one output of the mind shifts its own boredom threshold (0.66-0.78) together with familiarity and arousal | it decides when, not only what |
| Memory | genome, age, mind, clocks, tiredness, habits, 32 loop prints, a diary of its last 64 itches, saved with the set | reopen the set tomorrow: the same creature |

## Decisions (Hakan's three questions, answered with his full control)

1. **Order:** keep + first light (1.3) runs in a parallel thread; the creature is built
   here at the same time. keep's event log feeds the creature's diary when they merge.
2. **Memory:** its life lives in the set (old mixes stay as they were; no file races between
   instances). Only its taste (2.0e) will follow Hakan across sets.
3. **Between itches:** silent in sound. It is heard through the itch (what, and now also
   when), and seen in the window (2.0d). Breathing the echo stays as approved.

## Phases (each one a build he loads, with a listening sheet and a report video)

| Step | What he hears or sees | Gate |
| --- | --- | --- |
| 2.0a it chooses and decides | the same loop itches differently every pass; dark loops itch low and slow, bright ones high and quick; heads take turns; it gets impatient with loops it knows | offline: under 2% near-repeats over 2 hours, heads within 2 dB of 1.2, audit 0 FAIL, pluginval 10; then his ears |
| 2.0b the bounce matches | Export and Freeze give the itches he last heard (the itch tape, replayed when Live renders offline) | render from bar 1 and bar 17 match the live pass |
| 2.0c it answers | A/B renders only: after a throw it may answer; in silence it dreams (replays its diary, shifting its habits) | his pick; the loser is deleted |
| 2.0d you can watch it think | the window's tissue shows its 32 neurons firing | his eyes |
| 2.0e it learns your taste | moves right after an itch breed that kind in or out; bounded; inherited by newborns | weeks of use |
| later | ~20 temperaments (screened genomes, Bahadırhan's presets); synapses between instances | after 2.0 |

## Rules

- Real time: fixed arrays only on the audio thread, no allocation, locks or files. State
  crosses threads through a seqlock snapshot; a broken state loads as a newborn.
- Until 2.0c it changes only what the itch does and when, inside 1.2's ranges.
- `tools/render --dice` keeps 1.2's dice itch for A/B renders.
- Simulations behind the design: `/mnt/project-files/dendrite-home/creature/sims/`.
