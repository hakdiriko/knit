# Changelog

## 0.1.0 (Phase 1: the network)

- viber now makes sound: four nodes (**Echo**, **Resonator**, **Shift**, **Memory**) that feed each other through a coupling matrix.
  - Echo: a clean-ish delay with tape glide (turning Size bends the pitch of what's already echoing).
  - Resonator: a short, high-feedback delay that rings at a pitch (about 520 Hz at small Size down to 65 Hz at large).
  - Shift: an octave-up and an octave-down delay; Tone balances them, so bright spirals up (shimmer) and dark spirals down.
  - Memory: four overlapping grains replaying random moments from the last 0.2 to 2.6 seconds.
- Knobs: **Ecology** (colour → space → ringing → life), **Size**, **Tone**, **Mix** (equal power) and **Output** (trim, ±12 dB).
- Full safety chain: saturator and DC blocker inside every loop, wet auto-gain (±12 dB), a −0.3 dBFS safety limiter, a NaN/Inf guard, and a click-free bypass crossfade.
- Above about 75% Ecology the network sustains itself. It feeds on the input: once the input has been silent for a while, the drone fades out on its own.
- Generic window: five knobs with live values (Ecology names its regime), hover hints, double-click to reset.
- Defaults (Ecology 42%, Size 50%, Tone 55%, Mix 40%) already put the sound in a room.

## 0.0.1 (Phase 0: foundation)

- First build of **viber**: a stereo (or mono) passthrough with a placeholder window showing the name and version. It does not change the sound yet.
- CMake/JUCE 8 project producing VST3 on Windows and macOS, plus AU on macOS.
- CI builds Windows, macOS and Linux on every push, runs pluginval at strictness 10 (and `auval` for the AU), and uploads the plug-ins as downloadable artifacts.
- The discovery audit runs on the Linux build in CI and fails the build on any FAIL.
- Docs moved into the repo: `docs/PROJECT_BRIEF.md`, `docs/DSP_PLAYBOOK.md`, `tools/discovery_audit.py`.
- Audit fix: the percussive test signal peaked at +4.3 dBFS, so even an untouched passthrough was flagged as BLOWUP. It is now normalized to a −6 dBFS peak.
