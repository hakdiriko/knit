# the fly (sketch)

A real fly's brain hears a wav and its reflexes move dendrite's echo. Offline only; nothing here is in the plug-in.

1. Get the data into `tools/fly/data/` (about 135 MB, not in the repo):
   - `Completeness_783.csv`, `Connectivity_783.parquet` from github.com/philshiu/Drosophila_brain_model (MIT)
   - `Supplemental_file1_neuron_annotations.tsv` from github.com/flyconnectome/flywire_annotations (`supplemental_files/`)
2. `python3 tools/fly/sim.py song.wav song.npz 48`: the whole brain (leaky integrate-and-fire, as Shiu et al. 2024) for 48 s, about 2 minutes. The ears (Johnston's organ) get the music, the eyes' looming detectors (LPLC2, LC4) get the hits; each sense habituates.
3. `python3 tools/fly/touch.py song.npz touch.csv`: its reflexes (giant fiber escape, DNg11 grooming, left/right turning) become a touch on the echo.
4. `build/render song.wav out.wav --touch touch.csv`.

Needs numpy, scipy, pandas, pyarrow, soundfile.

## Credits

The connectome is FlyWire's (Dorkenwald et al. 2024, Schlegel et al. 2024, Nature); the brain model follows Shiu et al. 2024 (Nature). Cite them when using this.
