# dendrite

**A dub echo that grows around your head.** VST3 (plus AU on macOS), built with JUCE. Put it on a track and play.
The repository is called `knit`; the plug-in is called **dendrite**, always lowercase (it was called viber until 0.2.0).

**Status: in development, not released yet.** A dotted-eighth dub echo locked to the host tempo. Every repeat steps a little further out from the middle, so the echo grows from inside your head to all around it, while the mono sum stays a clean dub echo. It breathes with your playing, and when a part repeats too long it gets an itch: three of five heads (scream, laugh, manipulate, swarm, ghost) answer. It also keeps the last minute, so you can drag an itch straight into Live.

| Control | What you hear |
|---|---|
| feed (the field, up) | how long the repeats live: off, whisper, dub, drown, endless |
| space (the field, right) | where they live: inside, around, envelop, beyond. Mono-safe all the way |
| throw | press the dark to throw what's playing into the echo; at feed off it's the only way in |
| itch | after 1 to 8 bars of the same, the heads answer (or off) |
| keep | after an itch its lamps glow; drag one into Live to get that itch, cut on bar lines |

The one-page manual is [docs/manual/dendrite-manual.pdf](docs/manual/dendrite-manual.pdf), and it comes inside every download.

Made for Windows 11 and Ableton Live 12; macOS builds (VST3 and AU) are made too, but not code-signed.

## Get a build

Every push builds the plug-in automatically. You never need to compile anything.

1. Open the [Actions tab](https://github.com/hakdiriko/knit/actions/workflows/build.yml) (you need to be signed in to GitHub).
2. Click the newest run with a green tick.
3. Scroll to **Artifacts** and download:
   - Windows: `dendrite-windows-vst3` (every push)
   - macOS: `dendrite-macos-vst3-au` (only on runs started by hand: **Run workflow** on the Actions page)

## Install in Ableton Live on Windows

1. **Quit Live.** Windows locks a loaded plug-in file, so replacing it while Live is open fails or leaves the old version running.
2. Unzip `dendrite-windows-vst3.zip`. Inside are a folder called `dendrite.vst3` and the manual, `dendrite-manual.pdf`.
3. Copy the whole `dendrite.vst3` folder into `C:\Program Files\Common Files\VST3\`. Windows asks for administrator permission; allow it. If an older `dendrite.vst3` or `Dendrite.vst3` is there, replace it. **If a `viber.vst3` is there, delete it**: it is the same plug-in under its old name, and two copies confuse Live.
4. Start Live and open **Options → Settings → Plug-Ins**.
5. Turn on **Use VST3 Plug-In System Folders** (once only).
6. Click **Rescan**. If dendrite doesn't appear, hold **Alt** while clicking Rescan to force a full rescan.
7. In the browser, open **Plug-Ins → VST3 → hakdiriko → dendrite** and drag it onto a track.

## Install in Ableton Live on macOS

1. Quit Live and unzip `dendrite-macos-vst3-au.zip`, then the two zips inside it.
2. Copy `dendrite.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and `dendrite.component` to `~/Library/Audio/Plug-Ins/Components/` (replace any `Dendrite` versions, and delete any old `viber.vst3` and `viber.component`).
3. The builds are not code-signed, so macOS quarantines them. Open Terminal and run:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/dendrite.vst3 ~/Library/Audio/Plug-Ins/Components/dendrite.component
   ```
4. Start Live, open **Live → Settings → Plug-Ins**, turn on **Use Audio Units v2** and/or **Use VST3 Plug-In System Folders**, and click **Rescan** (hold **Option** for a full rescan).

## For developers

See [CLAUDE.md](CLAUDE.md), [docs/PROJECT_BRIEF.md](docs/PROJECT_BRIEF.md) and [docs/DSP_PLAYBOOK.md](docs/DSP_PLAYBOOK.md).
Design decisions are in [DECISIONS.md](DECISIONS.md); changes per version in [CHANGELOG.md](CHANGELOG.md).

## Rights

© 2026 hakdiriko. All rights reserved. The source is public so it can be read and built, but no licence is granted to copy, modify or redistribute it.

The fonts in `Resources/fonts/` (Urbanist and Fragment Mono) are under the SIL Open Font License; JUCE is used under its own licence.
