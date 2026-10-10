# Decisions

One line per decision, plus why. Newest at the bottom.

- 2026-10-10: Plug-in name **viber** (Hakan's working name); the repo stays `knit`. Why: Hakan named it.
- 2026-10-10: Plug-in codes `Hakd` (manufacturer) / `Vibr` (plug-in), bundle id `com.hakdiriko.viber`. Why: hosts identify plug-ins by these; they must never change, or saved Live sets lose the device.
- 2026-10-10: JUCE pinned to 8.0.15 via CMake FetchContent. Why: the brief asks for JUCE 8.x and 8.0.15 is its latest release; JUCE 9 (9.0.3) now exists and gets evaluated before the Phase 3 WebView UI work.
- 2026-10-10: Windows build links the MSVC runtime statically. Why: the VST3 then loads without a Visual C++ redistributable on Hakan's machine.
- 2026-10-10: macOS build is universal (arm64 + x86_64), minimum macOS 11. Why: one download for any Mac.
- 2026-10-10: CI runs on every push for Windows, macOS and Linux; pluginval at strictness 10 on every platform, `auval -strict` for the AU. Why: the playbook checklist asks for strictness 10, and Hakan never compiles.
- 2026-10-10: The discovery audit runs in CI on the Linux VST3 and fails the build on any FAIL. Why: "0 FAIL" is part of done, and pedalboard loads Linux VST3s headless.
- 2026-10-10: Audit test signal "percussive" normalized to a −6 dBFS peak (it peaked at +4.3 dBFS). Why: a clipping input made a pure passthrough fail BLOWUP, which measures the test signal, not the plug-in.
- 2026-10-10: Phase 0 INSERT_DULL warnings are expected. Why: the plug-in is a passthrough until Phase 1.
- 2026-10-10: The parameter tree (APVTS) exists from day one, empty. Why: state save/recall is wired and tested by pluginval before the first knob lands.
- 2026-10-10: The `audio-plugin-dev` skill is not vendored into `.claude/skills/`. Why: it is available to Claude sessions in this project from Hakan's account; vendoring it only when it isn't (per Hakan).
