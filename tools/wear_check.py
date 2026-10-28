#!/usr/bin/env python3
"""Wear check: does the tape wear on repetition, heal on new material, and stay safe?

pedalboard's host never presses play, so the discovery audit can't hear wear. This builds
tools/tape_render.cpp on dendrite's own DSP and renders with a playing transport:
48 bars of a drum loop, 8 bars of a pad, 16 bars of the drums again.

    python3 tools/wear_check.py            # exits 1 on failure
"""
import os
import subprocess
import sys
import tempfile

import numpy as np
from pedalboard.io import AudioFile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SR = 44100
BPM = 120.0
BAR = int(4 * 60 / BPM * SR)


def drum_loop(rng):
    n = 2 * BAR
    x = np.zeros(n)
    t = np.arange(int(0.4 * SR)) / SR
    kick = np.sin(2 * np.pi * (45 * t + 2 * (1 - np.exp(-t * 30)))) * np.exp(-t / 0.18)
    snare = rng.standard_normal(len(t)) * np.exp(-t / 0.06) * 0.7
    hat = np.diff(rng.standard_normal(len(t) + 1)) * np.exp(-t / 0.012) * 0.25
    beat = BAR // 4
    for b in range(2):
        for k, s in [(0, kick), (1, snare), (2.5, kick), (3, snare)]:
            i = b * BAR + int(k * beat)
            x[i:i + len(s)] += s[: n - i]
        for k in range(8):
            i = b * BAR + k * beat // 2
            x[i:i + len(hat)] += hat[: n - i]
    return x


def pad_loop():
    t = np.arange(2 * BAR) / SR
    x = sum(np.sin(2 * np.pi * f * t) for f in (110, 165, 220, 277))
    return x * np.minimum(1, np.minimum(t / 0.3, (t[-1] - t) / 0.3))


def write(path, x):
    x = 0.4 * x / np.abs(x).max()
    with AudioFile(path, "w", SR, 2, bit_depth=32) as f:
        f.write(np.vstack([x, x]).astype(np.float32))


def main():
    tmp = tempfile.mkdtemp()
    exe = os.path.join(tmp, "tape_render")
    subprocess.run(["g++", "-O2", "-std=c++20", "-I" + os.path.join(ROOT, "Source", "dsp"),
                    os.path.join(ROOT, "tools", "tape_render.cpp"),
                    os.path.join(ROOT, "Source", "dsp", "Tape.cpp"), "-o", exe], check=True)

    rng = np.random.default_rng(1)
    drums, pad = os.path.join(tmp, "drums.wav"), os.path.join(tmp, "pad.wav")
    write(drums, drum_loop(rng))
    write(pad, pad_loop())

    def render(wear):
        out, csv = os.path.join(tmp, "out.wav"), os.path.join(tmp, "wear.csv")
        subprocess.run([exe, f"{drums}:48,{pad}:8,{drums}:16", out, str(BPM), str(wear), "0.3", "0.5", csv], check=True)
        with AudioFile(out) as f:
            audio = f.read(f.frames)
        rows = np.loadtxt(csv, delimiter=",")
        return audio, {int(r[0]): r[3:].mean() for r in rows}

    failures = []
    audio, worn = render(0.5)
    peak = float(np.abs(audio).max())
    print(f"wear 0.5: mean wear at bar 8 {worn[8]:.3f}, bar 48 {worn[48]:.3f}, "
          f"bar 56 (after 8 new bars) {worn[56]:.3f}; peak {peak:.3f}")
    if not np.isfinite(audio).all() or peak > 1.0:
        failures.append("non-finite or clipping output")
    if worn[48] < 0.1:
        failures.append("48 bars of the same loop barely wore the tape")
    if worn[56] > 0.5 * worn[48]:
        failures.append("new material did not heal the tape")
    if not worn[8] < worn[24] < worn[48]:
        failures.append("wear does not grow steadily")

    _, never = render(0.0)
    if max(never.values()) > 0.0:
        failures.append("wear 0 still wears the tape")

    for f in failures:
        print("FAIL:", f)
    print("wear check:", "FAIL" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
