#!/usr/bin/env python3
"""
Discovery audit — checks that a plug-in has no dead zones, blow-ups or level jumps.

Loads a built VST3/AU with Spotify's pedalboard, then:
  1. Default state vs dry (INSERT_DULL)
  2. One-at-a-time sweep of every parameter on several test signals
     (BLOWUP, SILENT, LEVEL_JUMP, DEAD_ZONE, NO_EFFECT)
  3. Random full states — the "turn anything" test
Writes a Markdown report and exits non-zero on BLOWUP or SILENT, so CI can gate on it.

Install:  pip install pedalboard numpy
Usage:    python discovery_audit.py path/to/Plugin.vst3 [--steps 21] [--random 100]
                                    [--out audit_report.md] [--input extra.wav ...]

dendrite notes: the space lives between the ears, so spectrograms are compared per ear.
Its mono sum is the dry plus a dub echo that the space never touches, so MONO_BREAK checks
that moving `space` changes the echo's mono sum by level only.
"""
import argparse
import math
import sys

import numpy as np

SR = 48000
DUR = 4.0          # seconds of test signal
TAIL = 2.0         # seconds of silence appended to catch tails / self-oscillation

# Thresholds (dB). Tune once we know the plug-in's character.
DEAD_DIST_DB = 1.0        # two knob positions closer than this sound alike
DEAD_SPAN = 0.25          # dead zone: a stretch this long whose ends still sound alike
ACTIVE_DB = -70.0         # spectrogram cells quieter than this in both renders are ignored
NO_EFFECT_DB = 1.0        # whole sweep never moves further than this from default
JUMP_DB = 6.0             # level change between neighbouring steps
SILENT_DBFS = -60.0
INSERT_DULL_DB = 1.5
MONO_BREAK_DB = -30.0     # the echo's mono sum may change with space by level only (residual, dB)

SKIP_PARAM_WORDS = ("bypass", "itch", "throw")   # not swept one at a time
# "itch" acts after one to eight bars of repetition plus a frame of relief, longer than a sweep render.
# "throw" is a momentary pad: held, it opens the send. Both still take part in the random states.
RANDOM_SKIP_WORDS = ("bypass",)

# ---------------------------------------------------------------- test signals
def make_signals(sr=SR, dur=DUR, seed=1):
    rng = np.random.default_rng(seed)
    n = int(sr * dur)
    t = np.arange(n) / sr
    sigs = {}

    env = np.zeros(n)
    for start in np.arange(0.0, dur, 0.5):
        i = int(start * sr)
        length = min(n - i, int(0.25 * sr))
        env[i:i + length] = np.exp(-np.arange(length) / (0.03 * sr))
    perc = rng.standard_normal(n) * env
    sigs["percussive"] = 0.5 * perc / np.max(np.abs(perc))   # peak -6 dBFS: input itself must not clip

    pad = np.zeros(n)
    for base in (110.0, 138.6, 164.8):
        for h in range(1, 9):
            pad += np.sin(2 * np.pi * base * h * t) / h
    pad *= np.minimum(1.0, t / 0.5)
    sigs["pad"] = 0.3 * pad / np.max(np.abs(pad))

    f0, f1 = 40.0, 12000.0
    k = math.log(f1 / f0)
    sigs["sweep"] = 0.3 * np.sin(2 * np.pi * f0 * dur / k * (np.exp(t / dur * k) - 1))

    imp = np.zeros(n)
    imp[int(0.1 * sr)] = 0.9
    sigs["impulse"] = imp

    out = {}
    tail = np.zeros(int(sr * TAIL))
    for name, s in sigs.items():
        s = np.concatenate([s, tail]).astype(np.float32)
        out[name] = np.stack([s, s])          # stereo, shape (channels, samples)
    return out


def load_extra(paths):
    out = {}
    if not paths:
        return out
    from pedalboard.io import AudioFile
    for p in paths:
        with AudioFile(p).resampled_to(SR) as f:
            a = f.read(int(SR * DUR))
        if a.shape[0] == 1:
            a = np.vstack([a, a])
        a = np.concatenate([a[:2], np.zeros((2, int(SR * TAIL)), np.float32)], axis=1)
        out[p.split("/")[-1]] = a.astype(np.float32)
    return out


# ---------------------------------------------------------------- analysis
BAND_EDGES = 31.25 * 2 ** (np.arange(0, 28) / 3)   # 1/3-octave edges, ~31 Hz .. ~16 kHz


def analyse(y, sr=SR):
    """Return dict: finite, peak, rms_db, spec_db.

    spec_db is a time x band matrix (1/3-octave bands, ~43 ms frames) in dB, for each
    channel side by side. Comparing whole spectrograms, not just the average spectrum,
    matters: delays, echoes, grains and tails change *when* energy happens far more
    than the long-term average colour. Comparing each ear, not the mono sum, matters
    for a plug-in whose space lives between the ears: its mono sum barely changes.
    """
    finite = bool(np.all(np.isfinite(y)))
    if not finite:
        return {"finite": False, "peak": float("inf"), "rms_db": 0.0, "spec_db": None}
    chans = y if y.ndim == 2 else y[np.newaxis, :]
    peak = float(np.max(np.abs(y)))
    rms = math.sqrt(float(np.mean(chans ** 2)) + 1e-20)
    nfft, hop = 2048, 2048
    win = np.hanning(nfft)
    freqs = np.fft.rfftfreq(nfft, 1 / sr)
    specs = []
    for ch in chans:
        frames = np.array([ch[i:i + nfft] * win for i in range(0, len(ch) - nfft, hop)])
        mag = np.abs(np.fft.rfft(frames, axis=1)) ** 2            # (time, bins)
        cols = []
        for lo, hi in zip(BAND_EDGES[:-1], BAND_EDGES[1:]):
            sel = (freqs >= lo) & (freqs < hi)
            cols.append(mag[:, sel].mean(axis=1) if np.any(sel) else np.zeros(len(frames)))
        specs.append(np.stack(cols, axis=1))
    spec = 10 * np.log10(np.concatenate(specs, axis=1) + 1e-12)
    spec = np.maximum(spec, -80.0)                            # floor: silence vs silence = 0
    return {"finite": True, "peak": peak, "rms_db": 20 * math.log10(rms), "spec_db": spec}


def mono_residual(dry, out_a, out_b):
    """How far the echo's mono sum in render b is from render a, in dB below it, after a
    gain match. Very negative = the two differ only in level (mono-safe space)."""
    d = dry.mean(axis=0)
    a = out_a.mean(axis=0) - d
    b = out_b.mean(axis=0) - d
    g = float(np.dot(b, a) / (np.dot(a, a) + 1e-20))
    r = b - g * a
    return 10 * math.log10(float(np.mean(r ** 2)) / (float(np.mean(b ** 2)) + 1e-20) + 1e-20)


def distance(a, b):
    """Perceptual-ish distance in dB: mean |difference| over the time x band spectrogram."""
    if a["spec_db"] is None or b["spec_db"] is None:
        return float("inf")
    n = min(len(a["spec_db"]), len(b["spec_db"]))
    A, B = a["spec_db"][:n], b["spec_db"][:n]
    active = np.maximum(A, B) > ACTIVE_DB      # ignore cells silent in both renders,
    if not np.any(active):                     # otherwise silence dilutes every difference
        return 0.0
    return float(np.mean(np.abs(A - B)[active]))


# ---------------------------------------------------------------- plug-in access
def get_params(plugin):
    return dict(plugin.parameters)


def get_raw(param):
    return float(param.raw_value)


def set_raw(param, x):
    param.raw_value = float(x)


def render(plugin, audio):
    return plugin.process(audio, SR, reset=True)


# ---------------------------------------------------------------- audit
def audit(plugin, signals, steps=21, n_random=100, seed=7):
    params = get_params(plugin)
    defaults = {k: get_raw(p) for k, p in params.items()}
    xs = np.linspace(0.0, 1.0, steps)
    flags = []          # (severity, param, signal, message)
    lines = []

    def restore():
        for k, p in params.items():
            set_raw(p, defaults[k])

    # 1. Insert test
    restore()
    lines.append("## Default state vs dry\n")
    lines.append("| signal | distance from dry (dB) | out RMS (dBFS) | echo mono, default vs no space (dB) |\n|---|---|---|---|")
    render(plugin, signals[next(iter(signals))])      # warm-up: some hosts read latency only after a first render
    space = next((p for k, p in params.items() if k.lower() == "space"), None)
    for sname, audio in signals.items():
        out = render(plugin, audio)
        dry, wet = analyse(audio), analyse(out)
        d = distance(dry, wet)
        mono = float("nan")
        if space is not None and sname != "impulse":
            set_raw(space, 0.0)
            flat = render(plugin, audio)
            restore()
            mono = mono_residual(audio, flat, out)
        mono_text = "–" if math.isnan(mono) else f"{mono:.1f}"
        lines.append(f"| {sname} | {d:.2f} | {wet['rms_db']:.1f} | {mono_text} |")
        if sname != "impulse" and d < INSERT_DULL_DB:
            flags.append(("WARN", "(default)", sname, f"INSERT_DULL: only {d:.2f} dB from dry"))
        if sname != "impulse" and mono > MONO_BREAK_DB:
            flags.append(("WARN", "(default)", sname, f"MONO_BREAK: space changes the echo's mono sum ({mono:.1f} dB)"))
    lines.append("")

    # 2. Parameter sweeps
    lines.append("## Parameter sweeps\n")
    for pname, p in params.items():
        if any(w in pname.lower() for w in SKIP_PARAM_WORDS):
            continue
        # A choice or a switch (a few steps) is judged at its steps only: it has no travel
        # between them to be dead, and a jump between two choices is the choice.
        steps = getattr(p, "num_steps", 0)
        stepped = 1 < steps <= 8
        px = [i / (steps - 1) for i in range(steps)] if stepped else xs
        for sname, audio in signals.items():
            restore()
            base = analyse(render(plugin, audio))
            res = []
            for x in px:
                set_raw(p, x)
                res.append(analyse(render(plugin, audio)))
            restore()

            for x, r in zip(px, res):
                if not r["finite"] or r["peak"] > 1.0:
                    flags.append(("FAIL", pname, sname, f"BLOWUP at {x:.2f} (peak {r['peak']:.2f})"))
                elif sname != "impulse" and r["rms_db"] < SILENT_DBFS:
                    flags.append(("FAIL", pname, sname, f"SILENT at {x:.2f} ({r['rms_db']:.1f} dBFS)"))
            if not all(r["finite"] for r in res):
                continue

            jumps = [] if stepped else [abs(res[i + 1]["rms_db"] - res[i]["rms_db"]) for i in range(len(res) - 1)]
            for i, j in enumerate(jumps):
                if j > JUMP_DB:
                    flags.append(("WARN", pname, sname,
                                  f"LEVEL_JUMP {j:.1f} dB between {xs[i]:.2f} and {xs[i+1]:.2f}"))
            # Dead zone = a stretch of travel whose two ends still sound alike.
            # Judged end-to-end, not step-to-step: a smooth knob makes small steps
            # everywhere, but its total change across a stretch is still large.
            # A switch has no travel, only its two states, so it is judged by reach alone.
            i = 0
            while not stepped and getattr(p, "num_steps", 0) > 2 and i < len(res) - 1:
                j = i
                while j + 1 < len(res) and distance(res[i], res[j + 1]) < DEAD_DIST_DB:
                    j += 1
                if xs[j] - xs[i] >= DEAD_SPAN:
                    flags.append(("WARN", pname, sname,
                                  f"DEAD_ZONE {xs[i]:.2f}–{xs[j]:.2f} ({xs[j] - xs[i]:.0%} of travel)"))
                i = max(j, i + 1)
            reach = max(distance(base, r) for r in res)
            if reach < NO_EFFECT_DB and not (stepped and sname == "impulse"):
                flags.append(("WARN", pname, sname, f"NO_EFFECT: max {reach:.2f} dB from default"))

    # 3. Random full states
    rng = np.random.default_rng(seed)
    bad, dull = 0, 0
    test = {k: v for k, v in signals.items() if k != "impulse"}
    for _ in range(n_random):
        for k, p in params.items():
            if not any(w in k.lower() for w in RANDOM_SKIP_WORDS):
                set_raw(p, rng.random())
        for sname, audio in test.items():
            r = analyse(render(plugin, audio))
            # dry off with feed off is silence by design (a return track with nothing sent back)
            echo_only_off = any("dry" in k.lower() and get_raw(q) < 0.5 for k, q in params.items()) \
                and any(k.lower() == "feed" and get_raw(q) < 0.1 for k, q in params.items())
            if not r["finite"] or r["peak"] > 1.0 or (r["rms_db"] < SILENT_DBFS and not echo_only_off):
                bad += 1
            elif distance(analyse(audio), r) < INSERT_DULL_DB:
                dull += 1
    restore()
    total = n_random * len(test)
    lines.append("## Random states (\"turn anything\" test)\n")
    lines.append(f"- {total} renders: **{bad} broken** (blow-up/silent), "
                 f"{dull} too close to dry, {total - bad - dull} alive "
                 f"({(total - bad - dull) / max(total, 1):.0%})")
    if bad:
        flags.append(("FAIL", "(random)", "*", f"{bad}/{total} random states broken"))
    lines.append("")

    return flags, lines


def write_report(flags, lines, path, plugin_path):
    fails = [f for f in flags if f[0] == "FAIL"]
    warns = [f for f in flags if f[0] == "WARN"]
    out = [f"# Discovery audit — `{plugin_path}`\n",
           f"**{len(fails)} FAIL, {len(warns)} WARN**\n"]
    if flags:
        out.append("## Flags\n\n| severity | parameter | signal | finding |\n|---|---|---|---|")
        for sev, p, s, m in sorted(flags):
            out.append(f"| {sev} | {p} | {s} | {m} |")
        out.append("")
    out += lines
    with open(path, "w") as fh:
        fh.write("\n".join(out))
    return len(fails)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("plugin")
    ap.add_argument("--steps", type=int, default=21)
    ap.add_argument("--random", type=int, default=100)
    ap.add_argument("--out", default="audit_report.md")
    ap.add_argument("--input", nargs="*", help="extra audio files (voice, field recording, mix...)")
    args = ap.parse_args()

    from pedalboard import load_plugin
    plugin = load_plugin(args.plugin)
    signals = make_signals()
    signals.update(load_extra(args.input))

    flags, lines = audit(plugin, signals, steps=args.steps, n_random=args.random)
    n_fail = write_report(flags, lines, args.out, args.plugin)
    print(f"Report written to {args.out}: {n_fail} FAIL, {len(flags) - n_fail} WARN")
    sys.exit(1 if n_fail else 0)


if __name__ == "__main__":
    main()
