# CLAUDE.md

**dendrite** (always lowercase; repo `knit`; called viber until 0.2.0): a dub echo that grows around your head. A zero-setup VST3/AU audio effect, C++/JUCE, for Hakan.

Read before working:
- `docs/PROJECT_BRIEF.md`: vision, principles, roadmap, autonomy rules. Read at the start of every session.
- `docs/DSP_PLAYBOOK.md`: DSP mechanisms, real-time rules, quality checklist. Read before touching any DSP code.
- Use the `audio-plugin-dev` skill for all plug-in code.

Layout:
- `Source/`: `Processor` (parameters, host time, state) and `Editor` (the window).
  - `Source/dsp/`: `Echo` (the mid/side dub echo: tape, breathing, throw, space), `Itch` (the listener and the five heads), `Fly` (the itch since 2.1: 11,705 neurons of a real fly's brain from FlyWire with its mushroom body, smell, restless neurons, moods and learning since 2.2; data generated into `FlyData.cpp` by `tools/fly/export.py`), `Creature` (2.0a's 32-neuron mind, still the itch's memory; plan in `docs/CREATURE.md`), `Hand` (2.1 sketch, off), `Engine` (clock, itch frames, dry/wet, safety limiter), `Tools.h` (primitives).
  - `Source/keep/`: `Keep` (the last minute on two tapes, the diary of marks, bar-aligned WAV takes; the creature's first memory).
  - `Source/ui/`: `Theme` (colours, Urbanist + Fragment Mono fonts, look and feel), `Field` (the XY field and its dendrites), `FlyBrain` (the fly's brain firing, small in the strip, large in the drawer), `Controls` (the itch strip, embers, wordmark), `Tape` (keep's tape, ruler and drag into Live), `FirstLight` (the silent ghost-hand demo), `HostFocus` (Live's focus before a drag on Windows).
- `Resources/fonts/`: Urbanist (words) and Fragment Mono (numbers), both OFL, embedded as binary data. `Resources/lamps/`: the itch's glass bead and lamps, baked by `tools/blender/lamps.py`.
- `docs/manual/`: the one-page manual (`dendrite-manual.html` → `dendrite-manual.pdf` via `node docs/manual/render_manual.js`; CI ships the PDF next to the plug-in).
- `tools/keeptest/`: keep tested headless with a fake transport (`-DDENDRITE_TOOLS=ON`, `dendrite_keeptest loop.wav outdir`).
- `tools/fly/`: the whole fly brain offline (numpy), the cut and the export.
- `tools/render/`: offline renderer without JUCE (`g++ -O2 -std=c++20 -I Source tools/render/render.cpp Source/dsp/*.cpp -o build/render`); `tools/snapshot/`: renders the window to PNG (`-DDENDRITE_TOOLS=ON`, run under `xvfb-run`).
- `tools/discovery_audit.py`: sweeps a built VST3 for blow-ups, silence, dead zones and level jumps.
- `.github/workflows/build.yml`: every push builds Windows and Linux; a manual run (workflow_dispatch) adds macOS (VST3 + AU). pluginval at strictness 10 everywhere, the audit on Linux.
- `DECISIONS.md` (one line per decision, plus why) and `CHANGELOG.md`: keep both current.

Build locally (Linux needs the apt packages listed in the workflow):

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
python3 tools/discovery_audit.py build/dendrite_artefacts/Release/VST3/dendrite.vst3
```

Done means: CI green on all three platforms (pluginval passes), the audit shows 0 FAIL, and `CHANGELOG.md` is updated.
Hakan is on Windows 11 with Ableton Live 12, so the Windows VST3 artifact is the one he loads.
Never change `PLUGIN_MANUFACTURER_CODE`/`PLUGIN_CODE` (or the bundle id, or the APVTS state type `"viber"`): hosts identify the plug-in by them, and saved sessions would lose it or its settings.
