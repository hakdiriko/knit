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
- 2026-10-10: Phase 0 INSERT_DULL warnings are expected. Why: the plug-in is a passthrough until Phase 1. (Gone as of Phase 1.)
- 2026-10-10: The parameter tree (APVTS) exists from day one, empty. Why: state save/recall is wired and tested by pluginval before the first knob lands.
- 2026-10-10: The `audio-plugin-dev` skill is not vendored into `.claude/skills/`. Why: it is available to Claude sessions in this project from Hakan's account; vendoring it only when it isn't (per Hakan).
- 2026-10-10: Phase 1 parameter IDs `ecology`, `size`, `tone`, `mix`, `output`, `bypass` (version 1). Why: hosts store automation and state by ID, so they never get renamed.
- 2026-10-10: Ecology drives coupling `c` and per-node loop gain `g` on separate paths; each node crosses g = 1 between 70% and 76%. Why: playbook rule of thumb, and staggered crossings make the tip into self-oscillation a blur instead of a step.
- 2026-10-10: Shift node runs an octave up and an octave down at once, and Tone balances them (dark = downward spirals, bright = shimmer). Why: gives Tone a second, musical job without a new knob; Character takes over intervals in Phase 2.
- 2026-10-10: Phase 1 Memory node uses its own 3.4 s buffer with 4 Hann grains, reach 0.2–2.6 s from Size. Why: the brief puts the 30 s memory buffer, Memory knob and tape slowdown in Phase 2; this keeps the node audible now.
- 2026-10-10: Resonator pitch follows Size continuously (520 Hz → 65 Hz) and glides. Why: tape-like bends feel alive; snapping to a scale is Phase 4.
- 2026-10-10: "Starvation": loop gain above 0.8 fades toward 0.8 when the input stays below about −65 dBFS (≈1.5 s envelope + 2 s ramp). Why: life-regime drones stay driven by the input and don't ring forever after the transport stops.
- 2026-10-10: Small fixed random-walk drift on delay times (0.25–0.4%, golden-ratio periods, fixed seeds). Why: breaks up metallic FDN ringing now; Phase 2 Motion scales it. Fixed seeds keep renders repeatable.
- 2026-10-10: Auto-gain measures per-channel power (not mono sum) with fast-rise/slow-fall envelopes (80 ms / 0.8 s) and holds while the input is silent. Why: a wide wet signal read as quiet and got over-boosted; a sudden loud wet now gets pulled back in ~100 ms; tails aren't pumped up.
- 2026-10-10: Safety limiter is instant-attack, linked, 120 ms release, −0.3 dBFS ceiling, no look-ahead. Why: guarantees no sample over the ceiling with zero latency, so the null test and Live's latency stay untouched.
- 2026-10-10: Bypass is a host-visible parameter with a 30 ms crossfade, and the network keeps running while bypassed. Why: click-free on/off, and un-bypassing returns to a living network.
