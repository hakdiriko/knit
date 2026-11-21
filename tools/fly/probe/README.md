# the fly's instruments

Phase 0 of "The fly: second review and plan" (2026-11-16): make "does the brain matter?" a number anyone can re-run. Tools only; the plug-in sounds exactly as before (fly logs are identical with the probes off, and the plug-in never turns them on).

```
g++ -O3 -march=x86-64-v2 -std=c++20 -I Source tools/render/render.cpp Source/dsp/*.cpp Source/flies/flywire/FlyData.cpp -o build/render
python3 tools/fly/probe/material.py build/material [--own <folder of Hakan's loops>]
python3 tools/fly/probe/brain_share.py run build/material build/baseline      # about 30 min on 4 cores
python3 tools/fly/probe/brain_share.py report build/baseline                  # report.json, report.md
python3 tools/fly/probe/brain_share.py same build/baseline build/baseline2    # the gate: two runs agree
```

Needs only numpy.

## The probes (build/render)

- `--fly-dump DIR`: every 50 ms, into `DIR`:
  - `spikes.npy` (uint8, bins x 11,705): each neuron's spike count;
  - `senses.npy` (float32, bins x 44): the senses as measured (ears, looming eyes, then 32 smell bands);
  - `heard.npy` (float32, bins x 44): the same as they reach the sense neurons, after tiring and the moods (the chance per 0.5 ms step that a sense neuron fires);
  - `outputs.npy` (float32, bins x 14): seconds, beat, then the output signals the reflexes read (escape, groom, lean, excited, each the bin's mean), and what the fly is doing (feeling, fear, arousal, fatigue, move, jump, scratch, throwing);
  - `meta.json`: bin length, tempo, bar, loop length, and which neurons are Kenyon cells, MBONs, DANs, PPL1, the escape path, DNg11 and the left and right descending neurons.
- `--scramble SEED`: every non-plastic synapse strength shuffled among the connections (the same strengths and signs, on random connections). The 28,761 learning KC to MBON synapses stay where they are. The baseline uses seed 1234.
- `--surrogate FILE`: no brain at all. A linear filter from `heard` over the last six 50 ms bins (sliding, updated every 10 ms) gives the four outputs; the senses, the body, the grid and every rule are the same. `brain_share.py fit` writes the file (4 lines of 265 numbers: lag 0 inputs, ..., lag 5 inputs, a constant).
- `--stats FILE`: the mean load and the slowest single audio block (seconds of work per second of audio; 1.0 is a whole core). Timing depends on the machine and what else runs, so it is not part of the reproducibility check.
- `--bar BEATS`: quarter notes per bar (3 for 3/4 and for 6/8).

## brain_share.py

`run` renders every piece of the material set three times (real, scrambled, stand-in), five minutes each, analyses each dump and deletes the spike files (keep them with `--keep-spikes`; about 75 MB per render). The stand-in is fitted once on all the real flies together. `report` writes the tables; `same` compares two runs number by number.

Every fit trains on the first half of a render and is scored on the second half; the ridge strength is chosen on the last quarter of the training half. Fixed seeds everywhere, so a second run gives the same numbers.

## goal.py

Now also: moves per bar (`--bar`), fear at five minutes and at the end, the longest run of one kind of move, scratching share, and with a `--stats` file the slowest block.

## The material set

Twelve synthetic pieces (`material.py`: keys and groove from the taste rounds, a dub loop, drums with gaps, voice, a pad with no hits, sparse, dense, a 32-bar passage that changes section, 3/4, 6/8, 174 bpm). None is Hakan's: his playground loops folder was empty when this was built. `--own` takes any `name_<bpm>bpm.wav` (and `_3-4` or `_6-8`) from a folder.

## The baseline

`docs/FLY_BASELINE.md` (2026-11-16, the 2.4.3 fly). `baseline-2026-11-16.json` is its full report and `standin-2026-11-16.txt` the stand-in it fitted. To compare a later fly against them, write the json into a folder as `report.json` and run `brain_share.py same`, or read them side by side. `--seed N` on the renderer gives the fourth fly, the real one with other chance, which is the floor for every likeness.
