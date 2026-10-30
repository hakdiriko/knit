# CLAUDE.md

**dendrite** (repo `knit`; called viber until 0.2.0; always lowercase): a zero-setup VST3/AU audio effect, C++/JUCE, for Hakan.
This branch is the **hypnagogic** rewrite (0.6.0 demo): a binaural space instrument, all new code. Its handover is `docs/HANDOVER-hypnagogic.md`.

Read before working:
- `docs/PROJECT_BRIEF.md`: vision, principles, roadmap, autonomy rules. Read at the start of every session.
- `docs/DSP_PLAYBOOK.md`: DSP mechanisms, real-time rules, quality checklist. Read before touching any DSP code.
- Use the `audio-plugin-dev` skill for all plug-in code.

Layout:
- `Source/`: `Processor` (clock, attack detector, analytic pair, output chain) and `Editor` (the disc and the itch strip). `Source/space/`: `Tools.h` (primitives, including the linear-phase `Analytic` pair), `Field` (the Side-only space: near path and ERB allpass decorrelator), `Itch` (`Listener` for repetition, `Heads` for scream, laugh, manipulate).
- Invariant: the plug-in only ever adds Side. L = M + S, R = M − S, so mono = dry. Keep it that way.
- `tools/discovery_audit.py`: sweeps a built VST3 for blow-ups, silence, dead zones and level jumps, per ear, and checks the mono fold-down.
- `.github/workflows/build.yml`: builds Windows, macOS and Linux, runs pluginval at strictness 10, runs the audit on Linux.
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
