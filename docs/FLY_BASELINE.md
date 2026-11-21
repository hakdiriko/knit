# The fly's baseline (Phase 0)

Measured 2026-11-16 on the 2.4.3 fly (PR #11 head `70b0278`), with the instruments in `tools/fly/probe/` (see its README). This is the yardstick every later phase of "The fly: second review and plan" is measured against.

**Gate: passed.** Two complete runs from scratch (36 renders each, five minutes per render) give the same numbers, every one of them (`brain_share.py same build/baseline build/baseline2`). A third run after the renderer started flushing denormals, as the plug-in does, also gave the same numbers (it changed only the timing). Timing is left out of the check: it depends on the machine.

Re-run:

```
g++ -O3 -march=x86-64-v2 -std=c++20 -I Source tools/render/render.cpp Source/dsp/*.cpp Source/flies/flywire/FlyData.cpp -o build/render
python3 tools/fly/probe/material.py build/material
python3 tools/fly/probe/brain_share.py run build/material build/baseline
python3 tools/fly/probe/brain_share.py report build/baseline
```

## How it was measured

- **Material:** twelve synthetic pieces, 120 to 174 bpm, 4/4, 3/4 and 6/8, each looped to just over five minutes (`material.py`). None is Hakan's own music: his loops folder was empty. This is the set's biggest weakness.
- **Four flies per piece:**
  - **real:** the 2.4.3 fly, itch at its default;
  - **scrambled:** every non-plastic synapse strength shuffled among the connections (seed 1234), so the same strengths sit on random wires;
  - **stand-in:** no brain at all, one linear filter from the 44 senses over the last 300 ms to the four output signals, fitted once on all twelve real flies together;
  - **real, other chance:** the real fly with a different random seed (its restless kicks, readiness and sense spikes). Two real flies never play exactly alike, so this is the floor that every "how alike" number is read against.
- **Fits** train on the first half of a render and are scored on the second half.
- **The senses** are taken as they reach the sense neurons: after tiring and the moods. The raw senses give the same picture (in `report.json` as `explained_raw`).

## The headline

**A no-brain filter plays the real fly's moments as well as a second real fly does.** Measured over the 12 pieces, the real fly's move starts land within one 16th of the stand-in's 27% of the time, and within one 16th of a second real fly's 28% of the time. The scrambled brain manages 10%.

**Where the stand-in differs, it is fear.** It has no memory, so no fear, so it never dives. The real fly dives a lot (13 to 17 dives in five minutes on every groove) because its fear reaches 1.0 on 10 of 12 pieces. That fear is the one thing the brain adds that the filter does not, and it is the ratchet the review called a defect.

**The wiring matters for how much, not for when.**
- The scrambled fly moves a third as often (9 against 28 moves in five minutes, on average).
- It never scratches.
- Its fear also saturates.

So the real connectome sets the levels. The timing comes from the senses and the grid.

## Against the review's numbers

The review's throwaway probes measured two pieces for 90 s. Here it is twelve pieces for five minutes each, with the method written down.

| | review (keys, groove, 90 s) | this baseline (12 pieces, 5 min) | holds? |
|---|---|---|---|
| Escape explained by the senses | 95 to 99% | 93% mean; 95 to 100% on 11 pieces, 49% on the pad (almost no escapes) | yes |
| Excitement explained | 72 to 82% | 63% mean (26 to 89%) | lower, same story |
| Grooming explained | 46 to 78% | 82% mean (27 to 98%) | higher |
| Lean explained | under 10% | 32% mean; 4% groove, 40% keys, 82% sparse | **no**: on bare material the lean follows the senses. On grooves it is noise, as the review said |
| Place in the loop, raw senses against output neurons | 72% against 32% | 63% against 23% (keys 65/39, groove 93/36) | yes |
| Place from the Kenyon cells | 24 to 43% | 19% mean (keys 32%, groove 40%) | yes |
| How often a Kenyon cell fires | about once in 2 s | 0.35 Hz mean (0.19 to 0.65), 1 to 3% of them in any 50 ms | yes |
| Fear | 0.63 keys, 0.95 groove at 90 s | 1.00 on both at 5 min; 0.89 mean, 1.0 on 10 of 12 | worse with time, as predicted |
| Real against scrambled on keys | 9 against 8 moves | 31 against 15 in five minutes | the wiring matters more for levels than the review measured |

## What this means for the next phases

- **Phase 1 (harden):**
  - Relief learning is the fix the numbers point to. Fear is the only thing that separates the real fly from the stand-in, and fear is pinned at 1.0.
  - The lean is not pure noise on bare material. On grooves it is. Steering neurons (DNa01/DNa02), or no lean at all, are still the right call.
  - **Cost.** With denormals flushed, as in the plug-in, the whole engine averages 11 to 17% of one core of this cloud machine. The stand-in costs 5 to 6%, so the brain itself is about 8 to 10%. Phase 1's target is under 5%.
  - **The slowest blocks are the real risk.** On an idle machine, one block in a thousand needs 42 to 48% of a block's time (keys, groove, sparse). In the full run, with four renders sharing four cores, it needs about 100%, and single blocks needed 2 to 11 times their own length. Some of that is the shared machine. Phase 1 should measure on Hakan's PC in Live.
  - **The renderer had a measuring bug.** It did not flush denormals, so silence-heavy material ran several times slower (sparse: 61% instead of 11%). That was a renderer problem, not a plug-in one, because the plug-in flushes. The renderer now flushes too. The fly logs come out the same either way.
- **Phase 3 (place into the memory):** today the Kenyon cells name the right 16th 19% of the time on average, and 3% on the long pieces. The gate there asks for 60%. That gap is large.
- **Phase 4 (teaching):** the stand-in is the lower reference to beat. With no brain at all, it already matches a second real fly on timing.

## The full report (run 3, with denormals flushed)

#### The senses alone explain the outputs (real fly, R² on the second half; senses over the last 300 ms)

| piece | escape | excited | groom | lean | escape, scrambled |
|---|---|---|---|---|---|
| keys | 99% | 71% | 96% | 40% | 96% |
| groove | 96% | 49% | 79% | 4% | 83% |
| dubloop | 96% | 43% | 60% | 15% | 86% |
| drums | 98% | 82% | 92% | 48% | 91% |
| voice | 99% | 89% | 92% | 76% | 91% |
| sparse | 100% | 77% | 98% | 82% | 98% |
| dense | 95% | 26% | 27% | 14% | 83% |
| sections | 96% | 61% | 95% | 53% | 91% |
| waltz | 98% | 38% | 94% | 34% | 91% |
| sixeight | 95% | 79% | 82% | 2% | 86% |
| fast | 96% | 79% | 78% | 13% | 86% |
| pad | 49% | 65% | 92% | 6% | 79% |
| **mean** | 93% | 63% | 82% | 32% | 88% |

#### Place in the loop, read from one 50 ms bin (real fly, second half)

| piece | 16ths | chance | raw senses | output neurons | Kenyon cells | MBONs | KC Hz | KCs active per bin |
|---|---|---|---|---|---|---|---|---|
| keys | 32 | 3% | 65% | 39% | 32% | 23% | 0.23 | 1% |
| groove | 32 | 3% | 93% | 36% | 40% | 18% | 0.65 | 3% |
| dubloop | 16 | 6% | 80% | 41% | 28% | 26% | 0.36 | 2% |
| drums | 128 | 1% | 36% | 6% | 3% | 4% | 0.41 | 2% |
| voice | 128 | 1% | 28% | 16% | 9% | 7% | 0.56 | 3% |
| sparse | 64 | 2% | 20% | 18% | 13% | 10% | 0.19 | 1% |
| dense | 32 | 3% | 90% | 23% | 24% | 16% | 0.22 | 1% |
| sections | 512 | 0% | 31% | 3% | 3% | 2% | 0.39 | 2% |
| waltz | 48 | 2% | 70% | 28% | 31% | 14% | 0.51 | 2% |
| sixeight | 24 | 4% | 73% | 25% | 20% | 13% | 0.28 | 1% |
| fast | 32 | 3% | 87% | 35% | 25% | 18% | 0.37 | 2% |
| pad | 64 | 2% | 80% | 8% | 3% | 3% | 0.00 | 0% |
| **mean** | | 2% | 63% | 23% | 19% | 13% | 0.35 | |

#### Four flies, five minutes each (r real, s scrambled, st stand-in, r2 real with other chance)

| piece | moves r / s / st / r2 | per bar (r) | fear at 5 min r / s / st / r2 | scratching r / s / st / r2 | spots alike r~s / r~st / r~r2 | same moments r~s / r~st / r~r2 | longest same move (r) |
|---|---|---|---|---|---|---|---|
| keys | 31 / 15 / 30 / 32 | 0.19 | 1.00 / 1.00 / 0.00 / 0.99 | 24% / 0% / 23% / 24% | 69% / 72% / 90% | 10% / 36% / 29% | 2 |
| groove | 29 / 6 / 23 / 29 | 0.18 | 1.00 / 1.00 / 0.00 / 1.00 | 28% / 0% / 33% / 28% | 40% / 34% / 79% | 7% / 3% / 21% | 2 |
| dubloop | 30 / 2 / 34 / 33 | 0.20 | 0.97 / 1.00 / 0.00 / 0.99 | 19% / 0% / 12% / 20% | 50% / 66% / 96% | 3% / 17% / 23% | 2 |
| drums | 35 / 12 / 49 / 30 | 0.22 | 1.00 / 1.00 / 0.00 / 1.00 | 39% / 0% / 44% / 39% | 59% / 65% / 79% | 9% / 49% / 34% | 2 |
| voice | 25 / 8 / 25 / 26 | 0.16 | 1.00 / 1.00 / 0.00 / 1.00 | 8% / 0% / 14% / 8% | 54% / 88% / 79% | 8% / 52% / 36% | 2 |
| sparse | 21 / 16 / 21 / 21 | 0.17 | 0.60 / 1.00 / 0.00 / 0.60 | 16% / 0% / 15% / 15% | 85% / 90% / 81% | 33% / 62% / 76% | 2 |
| dense | 34 / 14 / 36 / 31 | 0.19 | 1.00 / 1.00 / 0.00 / 1.00 | 13% / 0% / 3% / 14% | 24% / 53% / 90% | 6% / 6% / 15% | 2 |
| sections | 41 / 6 / 48 / 37 | 0.21 | 1.00 / 1.00 / 0.00 / 1.00 | 10% / 0% / 8% / 10% | 2% / 36% / 34% | 2% / 24% / 22% | 2 |
| waltz | 31 / 15 / 54 / 35 | 0.19 | 1.00 / 1.00 / 0.00 / 1.00 | 37% / 0% / 40% / 37% | 17% / 87% / 77% | 3% / 26% / 16% | 2 |
| sixeight | 24 / 6 / 18 / 26 | 0.12 | 1.00 / 1.00 / 0.00 / 1.00 | 28% / 0% / 27% / 30% | 33% / 33% / 86% | 4% / 4% / 12% | 2 |
| fast | 27 / 11 / 39 / 38 | 0.12 | 1.00 / 1.00 / 0.00 / 1.00 | 25% / 0% / 28% / 26% | 24% / 49% / 72% | 4% / 15% / 18% | 2 |
| pad | 3 / 1 / 2 / 1 | 0.02 | 0.12 / 0.40 / 0.00 / 0.10 | 10% / 0% / 7% / 10% | 33% / 33% / 33% | 33% / 33% / 33% | 1 |
| **mean** | 27.6 / 9.3 / 31.6 / 28.2 | | 0.89 / 0.95 / 0.00 / 0.89 | | 41% / 59% / 75% | 10% / 27% / 28% | |

#### Move kinds

| piece | real | scrambled | stand-in | real, other chance |
|---|---|---|---|---|
| keys | leap 1, hold 17, sink 13 | leap 1, hold 5, sink 9 | leap 1, hold 14, sink 15 | leap 1, hold 16, sink 15 |
| groove | hop 3, leap 6, duck 7, dive 13 | hop 1, leap 1, duck 4 | hop 14, leap 3, duck 6 | hop 7, leap 3, duck 7, dive 12 |
| dubloop | hop 4, leap 7, duck 5, dive 11, rush 3 | leap 1, duck 1 | hop 12, leap 15, duck 7 | hop 5, leap 6, duck 6, dive 12, rush 4 |
| drums | hop 6, leap 7, duck 4, dive 17, rush 1 | hop 2, leap 1, duck 4, dive 5 | hop 4, leap 24, duck 5, rush 16 | hop 5, leap 6, duck 3, dive 16 |
| voice | leap 1, hold 11, sink 13 | leap 1, hold 3, sink 4 | leap 1, hold 13, sink 11 | leap 1, hold 14, sink 11 |
| sparse | leap 1, hold 9, sink 11 | leap 1, hold 8, sink 7 | leap 1, hold 11, sink 9 | leap 1, hold 11, sink 9 |
| dense | hop 5, leap 6, duck 5, dive 16, rush 2 | hop 2, leap 1, duck 9, dive 2 | hop 15, leap 15, duck 6 | hop 6, leap 6, duck 3, dive 15, rush 1 |
| sections | hop 4, leap 6, duck 9, dive 13, rush 2, hold 6, sink 1 | leap 1, duck 2, hold 2, sink 1 | hop 7, leap 20, duck 7, rush 3, hold 5, sink 6 | hop 5, leap 6, duck 1, dive 15, rush 3, hold 4, sink 3 |
| waltz | hop 4, leap 5, duck 3, dive 15, rush 4 | hop 5, leap 1, duck 8, dive 1 | hop 5, leap 24, duck 9, rush 16 | hop 4, leap 8, duck 6, dive 14, rush 3 |
| sixeight | hop 3, leap 9, duck 5, dive 7 | hop 1, leap 1, duck 4 | hop 10, leap 4, duck 4 | hop 4, leap 6, duck 6, dive 10 |
| fast | hop 7, leap 5, duck 2, dive 12, rush 1 | hop 2, leap 1, duck 7, dive 1 | hop 18, leap 15, duck 6 | hop 7, leap 7, duck 4, dive 17, rush 3 |
| pad | leap 1, hold 1, sink 1 | leap 1 | leap 1, hold 1 | leap 1 |

#### Cost (render machine, not the plug-in: one run, timing is not reproducible)

| piece | mean load real | 1 block in 1000 real | slowest block real | mean load stand-in |
|---|---|---|---|---|
| keys | 12% | 70% | 338% | 5% |
| groove | 14% | 103% | 280% | 5% |
| dubloop | 14% | 102% | 202% | 5% |
| drums | 14% | 106% | 519% | 5% |
| voice | 15% | 105% | 386% | 5% |
| sparse | 11% | 102% | 188% | 6% |
| dense | 15% | 111% | 290% | 6% |
| sections | 12% | 103% | 229% | 5% |
| waltz | 15% | 122% | 1132% | 6% |
| sixeight | 15% | 111% | 336% | 5% |
| fast | 17% | 169% | 374% | 6% |
| pad | 14% | 105% | 443% | 6% |

The stand-in's own fit (pooled, last quarter of each render held out): escape 94%, groom 92%, lean 47%, excited 75%.
