# dendrite

A zero-setup audio effect (VST3, plus AU on macOS) built with JUCE. Put it on any track and turn anything.
The repository is called `knit`; the plug-in is called **dendrite**, lowercase (it was called viber until 0.2.0, and sets saved with viber still open).

**Status: 0.4 (reset: the tape and the room).** The sound passes through a tape, then into a room. A part that keeps repeating slowly wears the tape at fixed places, like a tape loop played to death; anything new heals it. What wears off falls into the room. Three knobs:

| Control | What you hear |
|---|---|
| wear | how fast a repeating part wears the tape; the value says about how many bars it takes (0 = never) |
| friction | the material: clean → warm → worn → rough (saturation, wow and flutter, grain, dust) |
| room | the space the sound and its worn fragments fall into: none → wood → hall → afterlife |

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
2. Unzip `dendrite-windows-vst3.zip`. Inside is a folder called `dendrite.vst3`.
3. Copy the whole `dendrite.vst3` folder into `C:\Program Files\Common Files\VST3\`. Windows asks for administrator permission; allow it. If an older `Dendrite.vst3` is there, delete it first. **If a `viber.vst3` is there, delete it**: it is the same plug-in under its old name, and two copies confuse Live.
4. Start Live and open **Options → Settings → Plug-Ins**.
5. Turn on **Use VST3 Plug-In System Folders** (once only).
6. Click **Rescan**. If dendrite doesn't appear, hold **Alt** while clicking Rescan to force a full rescan.
7. In the browser, open **Plug-Ins → VST3 → hakdiriko → dendrite** and drag it onto a track.

## Install in Ableton Live on macOS

1. Quit Live and unzip `dendrite-macos-vst3-au.zip`, then the two zips inside it.
2. Copy `dendrite.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and `dendrite.component` to `~/Library/Audio/Plug-Ins/Components/` (delete any old `Dendrite.*` and `viber.*` copies first).
3. The builds are not code-signed, so macOS quarantines them. Open Terminal and run:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/dendrite.vst3 ~/Library/Audio/Plug-Ins/Components/dendrite.component
   ```
4. Start Live, open **Live → Settings → Plug-Ins**, turn on **Use Audio Units v2** and/or **Use VST3 Plug-In System Folders**, and click **Rescan** (hold **Option** for a full rescan).

## For developers

See [CLAUDE.md](CLAUDE.md), [docs/PROJECT_BRIEF.md](docs/PROJECT_BRIEF.md) and [docs/DSP_PLAYBOOK.md](docs/DSP_PLAYBOOK.md).
Design decisions are in [DECISIONS.md](DECISIONS.md); changes per version in [CHANGELOG.md](CHANGELOG.md).
