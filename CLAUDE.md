# CLAUDE.md

**dendrite** (always lowercase; repo `knit`; called viber until 0.2.0): a dub echo that grows around your head. A zero-setup VST3/AU audio effect, C++/JUCE, for Hakan.

Read before working:
- `docs/PROJECT_BRIEF.md`: vision, principles, roadmap, autonomy rules. Read at the start of every session.
- `docs/DSP_PLAYBOOK.md`: DSP mechanisms, real-time rules, quality checklist. Read before touching any DSP code.
- Use the `audio-plugin-dev` skill for all plug-in code.

Layout:
- `Source/`: `Processor` (parameters, host time, state) and `Editor` (the window).
  - `Source/dsp/`: `Echo` (the mid/side dub echo: tape, breathing, throw, space), `Itch` (the listener and the five heads), `Creature` (the itch's own mind: 32 neurons, memory, saved with the set; plan in `docs/CREATURE.md`), `Engine` (clock, itch frames, dry/wet, safety limiter), `Tools.h` (primitives).
  - `Source/ui/`: `Theme` (colours, Urbanist + Fragment Mono fonts, look and feel), `Field` (the XY field and its dendrites), `Controls` (throw pad, itch panel).
- `Resources/fonts/`: Urbanist (words) and Fragment Mono (numbers), both OFL, embedded as binary data. `Resources/lamps/`: the itch's glass bead and lamps, baked by `tools/blender/lamps.py`.
- `docs/manual/`: the one-page manual (`dendrite-manual.html` → `dendrite-manual.pdf` via `node docs/manual/render_manual.js`; CI ships the PDF next to the plug-in).
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
