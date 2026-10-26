# Dendrite

A zero-setup audio effect (VST3, plus AU on macOS) built with JUCE. Put it on any track and turn anything.
The repository is called `knit`; the plug-in is called **Dendrite** (it was called viber until 0.2.0, and sets saved with viber still open).

**Status: 0.2 (the itch).** Four nodes (Echo, Resonator, Shift, Memory) feed each other, and the whole network breathes and, when it gets bored of repetition, scratches. Knobs:

| Knob | What you hear |
|---|---|
| Ecology | how much the parts talk: colour → space → ringing → life (self-sustaining above ~75%) |
| Size | small and close → huge and far |
| Tone | dark (downward spirals) ↔ bright (shimmer) |
| Itch | still → breathing → restless → itchy → compulsive. Past 60% it gets bored of repetition and scratches on a phrase boundary with a dub-desk gesture |
| Mix | dry ↔ wet, equal power |
| Output | trim after the automatic level matching |
| Sync | locks Echo, Shift and Memory to the host tempo; Size picks the note value |
| jump | A/B for Sync: off bends the pitch between note values like tape, on jumps cleanly |
| Scratch | scratches now; every press (or MIDI-mapped toggle) scratches |

Built for and tested on:
- Windows 11 Pro 64-bit (build 26200)
- Ableton Live 12.4.2

## Get a build

Every push builds the plug-in automatically. You never need to compile anything.

1. Open the [Actions tab](https://github.com/hakdiriko/knit/actions/workflows/build.yml) (you need to be signed in to GitHub).
2. Click the newest run with a green tick.
3. Scroll to **Artifacts** and download:
   - Windows: `dendrite-windows-vst3`
   - macOS: `dendrite-macos-vst3-au`

## Install in Ableton Live on Windows

1. **Quit Live.** Windows locks a loaded plug-in file, so replacing it while Live is open fails or leaves the old version running.
2. Unzip `dendrite-windows-vst3.zip`. Inside is a folder called `Dendrite.vst3`.
3. Copy the whole `Dendrite.vst3` folder into `C:\Program Files\Common Files\VST3\`. Windows asks for administrator permission; allow it. If an older `Dendrite.vst3` is there, replace it. **If a `viber.vst3` is there, delete it**: it is the same plug-in under its old name, and two copies confuse Live.
4. Start Live and open **Options → Settings → Plug-Ins**.
5. Turn on **Use VST3 Plug-In System Folders** (once only).
6. Click **Rescan**. If Dendrite doesn't appear, hold **Alt** while clicking Rescan to force a full rescan.
7. In the browser, open **Plug-Ins → VST3 → hakdiriko → Dendrite** and drag it onto a track.

## Install in Ableton Live on macOS

1. Quit Live and unzip `dendrite-macos-vst3-au.zip`, then the two zips inside it.
2. Copy `Dendrite.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and `Dendrite.component` to `~/Library/Audio/Plug-Ins/Components/` (delete any old `viber.vst3` and `viber.component`).
3. The builds are not code-signed, so macOS quarantines them. Open Terminal and run:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Dendrite.vst3 ~/Library/Audio/Plug-Ins/Components/Dendrite.component
   ```
4. Start Live, open **Live → Settings → Plug-Ins**, turn on **Use Audio Units v2** and/or **Use VST3 Plug-In System Folders**, and click **Rescan** (hold **Option** for a full rescan).

## For developers

See [CLAUDE.md](CLAUDE.md), [docs/PROJECT_BRIEF.md](docs/PROJECT_BRIEF.md) and [docs/DSP_PLAYBOOK.md](docs/DSP_PLAYBOOK.md).
Design decisions are in [DECISIONS.md](DECISIONS.md); changes per version in [CHANGELOG.md](CHANGELOG.md).
