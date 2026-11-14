"""Synthetic test material for dendrite's offline renders (48 kHz, 120 BPM).

   python3 tools/render/material.py OUTDIR

chords  Dm9 stabs on offbeats, 8 bars, then 4 bars of silence for the tails
voice   formant syllables with rests, then silence
drums   kick, rim, hats; gaps in bars 4 and 8
loop    a 1-bar dub techno loop (kick, offbeat chord, hats) repeated 24 bars: it itches
pad     a held chord, slowly filtered, 8 bars
"""
import sys, os
import numpy as np
from numpy.fft import rfft, irfft, rfftfreq
import soundfile as sf

sr = 48000; bpm = 120.0; beat = 60 / bpm; bar = 4 * beat
rng = np.random.default_rng(11)
out = sys.argv[1] if len(sys.argv) > 1 else "mat"
os.makedirs(out, exist_ok=True)


def save(name, x, peak=0.5):
    x = np.atleast_2d(x)
    if x.shape[0] == 1:
        x = np.vstack([x, x])
    x = x / np.max(np.abs(x)) * peak
    sf.write(f"{out}/{name}.wav", x.T.astype(np.float32), sr, subtype="FLOAT")


def env(n, tau):
    return np.exp(-np.arange(n) / (tau * sr))


def put(buf, t, s):
    i = int(round(t * sr)); m = min(s.shape[-1], buf.shape[-1] - i)
    buf[..., i:i + m] += s[..., :m]


def stab(length=0.16):
    n = int((length + 0.35) * sr); t = np.arange(n) / sr; s = np.zeros((2, n))
    for f in [146.83, 174.61, 220.0, 261.63, 329.63]:
        for c, det in enumerate([-0.06, 0.07]):
            ph = np.cumsum(np.full(n, f * 2 ** (det / 12))) / sr + rng.random()
            s[c] += (ph % 1) * 2 - 1
    gate = np.where(t < length, 1.0, np.exp(-(t - length) / 0.05))
    cut = 500 + 2800 * np.exp(-t / 0.06)
    o = np.zeros_like(s)
    a = np.exp(-2 * np.pi * cut / sr)
    for c in range(2):
        z1 = z2 = 0.0
        for i in range(n):
            z1 = (1 - a[i]) * s[c, i] + a[i] * z1; z2 = (1 - a[i]) * z1 + a[i] * z2; o[c, i] = z2
    return o * gate * (1 - np.exp(-t / 0.002))


N = int(12 * bar * sr)
st = stab()
ch = np.zeros((2, N))
pattern = {0: [0.5, 1.75], 1: [0.5, 2.5], 2: [0.5, 1.75], 3: [0.5, 3.0], 4: [0.5, 1.75], 5: [0.5, 2.5], 6: [0.5], 7: []}
for b, beats in pattern.items():
    for k in beats:
        put(ch, b * bar + k * beat, st)
save("chords", ch)


def syllable(f0, dur, vowel):
    n = int(dur * sr); t = np.arange(n) / sr
    f = f0 * (1 + 0.008 * np.sin(2 * np.pi * 5.2 * t) * np.minimum(1, t / 0.2))
    ph = np.cumsum(f) / sr; src = np.zeros(n)
    for h in range(1, 45):
        src += np.sin(2 * np.pi * h * ph) / h
    F = {"a": (730, 1090, 2440), "i": (270, 2290, 3010), "o": (570, 840, 2410), "e": (530, 1840, 2480)}[vowel]
    X = rfft(src); fr = rfftfreq(n, 1 / sr)
    H = sum(a / (1 + ((fr - F0) / (F0 * 0.07)) ** 2) for F0, a in zip(F, (1, 0.5, 0.3)))
    y = irfft(X * H, n) + rng.standard_normal(n) * 0.002
    w = np.minimum(1, np.minimum(t / 0.02, (t[-1] - t) / 0.06))
    return y * w


vo = np.zeros(N)
D, F, A, C = 293.66, 349.23, 440.0, 261.63
phr = [(0, 0, D, 0.45, "a"), (0, 0.5, F, 0.45, "i"), (0, 1.0, A, 0.9, "o"),
       (2, 0, A, 0.45, "e"), (2, 0.5, C * 2, 0.45, "a"), (2, 1.0, A, 0.45, "i"), (2, 1.5, F, 0.95, "o"),
       (4, 0, D, 1.9, "a"), (6, 0, F, 0.45, "i"), (6, 0.5, D, 0.95, "o")]
for b, k, f0, d, v in phr:
    put(vo, b * bar + k * beat, syllable(f0, d * beat, v))
save("voice", vo)

n = int(0.5 * sr); t = np.arange(n) / sr
kick = np.sin(2 * np.pi * (48 * t + 70 * (1 - np.exp(-t * 35)) / 35)) * env(n, 0.16)
rim = (np.sin(2 * np.pi * 1650 * t) * 0.5 + np.sin(2 * np.pi * 480 * t)) * env(n, 0.018) + rng.standard_normal(n) * env(n, 0.008) * 0.6
hat = np.diff(rng.standard_normal(n + 1)) * env(n, 0.02) * 0.22
dr = np.zeros(N)
for b in range(8):
    o = b * bar
    if b in (3, 7):
        put(dr, o, kick); put(dr, o + 1 * beat, rim * 0.9)
        continue
    for k in range(4):
        put(dr, o + k * beat, kick)
    for k in (1, 3):
        put(dr, o + k * beat, rim * 0.8)
    for k in range(4):
        put(dr, o + (k + 0.5) * beat, hat * rng.uniform(0.7, 1.0))
save("drums", dr)

# a 1-bar loop that never changes, for the itch
L = int(24 * bar * sr)
lp = np.zeros((2, L)); one = np.zeros((2, int(bar * sr)))
for k in range(4):
    put(one, k * beat, np.vstack([kick, kick]) * 0.9)
    put(one, (k + 0.5) * beat, np.vstack([hat, hat]) * 0.8)
put(one, 0.5 * beat, st * 0.55); put(one, 2.75 * beat, st * 0.45)
for b in range(24):
    put(lp, b * bar, one)
save("loop", lp)

# a held chord, slowly filtered
P = int(8 * bar * sr); tt = np.arange(P) / sr; pad = np.zeros((2, P))
for f in [146.83, 220.0, 261.63, 329.63, 392.0]:
    for c, det in enumerate([-0.08, 0.09]):
        ph = np.cumsum(f * 2 ** (det / 12) * (1 + 0.002 * np.sin(2 * np.pi * 0.3 * tt + c))) / sr
        pad[c] += (ph % 1) * 2 - 1
X = rfft(pad, axis=1); fr = rfftfreq(P, 1 / sr)
pad = irfft(X / (1 + (fr / 1400.0) ** 2), P, axis=1)
pad *= np.minimum(1, np.minimum(tt / 0.4, (tt[-1] - tt) / 1.0))
save("pad", pad, 0.35)

# pink noise bursts (measurements): 1 s on, 1 s off
Q = int(12 * sr); w = rng.standard_normal(Q); W = rfft(w); fq = rfftfreq(Q, 1 / sr); fq[0] = 1
pink = irfft(W / np.sqrt(fq), Q); gate = ((np.arange(Q) // sr) % 2 == 0).astype(float)
save("pink", pink * gate, 0.4)
print("ok")
