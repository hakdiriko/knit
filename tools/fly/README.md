# the fly

A real fly's brain hears a wav and its reflexes move dendrite's echo. Since 2.1.0 the plug-in runs the part of it that answers music live (`Source/dsp/Fly.*`); these are the offline tools that made it.

1. Get the data into `tools/fly/data/` (about 135 MB, not in the repo):
   - `Completeness_783.csv`, `Connectivity_783.parquet` from github.com/philshiu/Drosophila_brain_model (MIT)
   - `Supplemental_file1_neuron_annotations.tsv` from github.com/flyconnectome/flywire_annotations (`supplemental_files/`)
2. `python3 tools/fly/sim.py song.wav song.npz 48`: the whole brain (leaky integrate-and-fire, as Shiu et al. 2024) for 48 s, about 2 minutes. The ears (Johnston's organ) get the music, the eyes' looming detectors (LPLC2, LC4) get the hits; each sense habituates.
3. `python3 tools/fly/touch.py song.npz touch.csv`: its reflexes (giant fiber escape, DNg11 grooming, left/right turning) become a touch on the echo.
4. `build/render song.wav out.wav --touch touch.csv`.

To rebuild the plug-in's fly (`Source/dsp/FlyData.cpp`): run `sim.py` on test material (the eight pieces in the commit that made it: loop, drums, chords, song, pad, voice, pink, and the song at `RATE=400`), then `python3 tools/fly/prune.py` (in that folder: every neuron that fired, plus the senses, into `keep.npy`) and `python3 tools/fly/export.py keep.npy song.npz`.

The 2.2 fly (with an inner life) is a bigger cut: `SR=200 ETA=0.5 python3 tools/fly/sim2.py loop.wav inner.npz 60` runs the whole brain with smell, restless neurons, moods and learning (in the commit that made it: a 3.8 s loop repeated for a minute), then in this folder `python3 prune2.py keep.npy keep2.npy inner.npz` (every neuron that fired, the 2.1 cut, every MBON and DAN; 11,705 neurons) and `python3 export.py keep2.npy song.npz`.

Needs numpy, scipy, pandas, pyarrow, soundfile.

## Credits

The connectome is FlyWire's (Dorkenwald et al. 2024, Schlegel et al. 2024, Nature); the brain model follows Shiu et al. 2024 (Nature). Cite them when using this.
