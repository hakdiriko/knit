# viber

A zero-setup audio effect (VST3, plus AU on macOS) built with JUCE. Put it on any track and turn anything.
The repository is called `knit`; the plug-in is called **viber**.

**Status: Phase 1 (the network).** Four nodes (Echo, Resonator, Shift, Memory) feed each other. Knobs:

| Knob | What you hear |
|---|---|
| Ecology | how much the parts talk: colour → space → ringing → life (self-sustaining above ~75%) |
| Size | small and close → huge and far |
| Tone | dark (downward spirals) ↔ bright (shimmer) |
| Mix | dry ↔ wet, equal power |
| Output | trim after the automatic level matching |

Built for and tested on:
- Windows 11 Pro 64-bit (build 26200)
- Ableton Live 12.4.2

## Get a build

Every push builds the plug-in automatically. You never need to compile anything.

1. Open the [Actions tab](https://github.com/hakdiriko/knit/actions/workflows/build.yml) (you need to be signed in to GitHub).
2. Click the newest run with a green tick.
3. Scroll to **Artifacts** and download:
   - Windows: `viber-windows-vst3`
   - macOS: `viber-macos-vst3-au`

## Install in Ableton Live on Windows

1. **Quit Live.** Windows locks a loaded plug-in file, so replacing it while Live is open fails or leaves the old version running.
2. Unzip `viber-windows-vst3.zip`. Inside is a folder called `viber.vst3`.
3. Copy the whole `viber.vst3` folder into `C:\Program Files\Common Files\VST3\`. Windows asks for administrator permission; allow it. If an older `viber.vst3` is there, replace it.
4. Start Live and open **Options → Settings → Plug-Ins**.
5. Turn on **Use VST3 Plug-In System Folders** (once only).
6. Click **Rescan**. If viber doesn't appear, hold **Alt** while clicking Rescan to force a full rescan.
7. In the browser, open **Plug-Ins → VST3 → hakdiriko → viber** and drag it onto a track.

## Install in Ableton Live on macOS

1. Quit Live and unzip `viber-macos-vst3-au.zip`, then the two zips inside it.
2. Copy `viber.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and `viber.component` to `~/Library/Audio/Plug-Ins/Components/`.
3. The builds are not code-signed, so macOS quarantines them. Open Terminal and run:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/viber.vst3 ~/Library/Audio/Plug-Ins/Components/viber.component
   ```
4. Start Live, open **Live → Settings → Plug-Ins**, turn on **Use Audio Units v2** and/or **Use VST3 Plug-In System Folders**, and click **Rescan** (hold **Option** for a full rescan).

## For developers

See [CLAUDE.md](CLAUDE.md), [docs/PROJECT_BRIEF.md](docs/PROJECT_BRIEF.md) and [docs/DSP_PLAYBOOK.md](docs/DSP_PLAYBOOK.md).
Design decisions are in [DECISIONS.md](DECISIONS.md); changes per version in [CHANGELOG.md](CHANGELOG.md).
