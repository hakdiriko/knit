"""The fly's reflexes -> a touch on dendrite's echo (sketch). usage: python3 touch.py song.npz touch.csv"""
import sys, numpy as np
from families import rates, idx
def smooth(x, tau, dt):
    a = np.exp(-dt / tau); y = np.empty_like(x); s = 0.0
    for i, v in enumerate(x): s = a * s + (1 - a) * v; y[i] = s
    return y
def make(npz):
    dt = 0.005
    r = rates(npz, dt)
    z = np.load(npz); t, i = z['t'], z['i']; nb = len(r['escape'])
    g = idx(['DNg11'])
    legs = [np.bincount((t[i == n] / dt).astype(int), minlength=nb)[:nb] / dt for n in g]
    legs = sorted(legs, key=lambda x: -x.sum())[:2]
    esc = smooth(r['escape'], 0.03, dt)
    groom = smooth(r['groom'], 0.15, dt)
    lean = smooth(r['turnL'] - r['turnR'], 0.25, dt)
    lean = lean - smooth(lean, 6.0, dt)  # its resting lean is not a turn
    arousal = smooth(r['turnL'] + r['turnR'], 0.4, dt)
    rub = smooth(legs[0] - legs[1], 0.08, dt)
    n = nb; T = np.arange(n) * dt
    jump = esc > 45.0  # the giant fiber path firing hard: it jumps
    # a jump: the tape lurches and comes back; a hard one freezes it for a moment
    lurch = np.zeros(n); frz = np.zeros(n); throw = np.zeros(n)
    last = -9.0
    for k in np.where(jump)[0]:
        if T[k] - last < 1.0: continue
        last = T[k]; size = min(1.0, esc[k] / 80.0)
        m = (T >= T[k]) & (T < T[k] + 0.9)
        lurch[m] = np.minimum(lurch[m], -size * np.sin(np.pi * (T[m] - T[k]) / 0.9))
        throw[(T >= T[k]) & (T < T[k] + 0.12 + 0.2 * size)] = 1
        if size > 0.7: frz[(T >= T[k] + 0.1) & (T < T[k] + 1.2)] = 1
    speed = 2.0 ** (0.4 * lurch)  # up to about 5 semitones down
    gm = np.clip((groom - smooth(groom, 4.0, dt) + 0.5) / 7.0, 0, 1)  # scratching more than it was
    band = 0.85 * gm ** 1.5
    bandHz = 1200.0 * 2.0 ** np.clip(rub / 40.0, -1.2, 1.2)
    space = np.clip(lean / 0.8, -1, 1) * 0.35
    feed = np.clip(arousal / 4.0, 0, 1) * 0.18 - 0.05
    lift = 1.0 + 0.6 * band + 0.8 * smooth(throw, 0.3, dt)
    rows = np.stack([T, speed, feed, space, band, bandHz, smooth(frz, 0.05, dt), throw, lift], 1)
    return rows, dict(escape=esc, groom=groom, lean=lean, jumps=T[np.diff(np.r_[0, jump.astype(int)]) > 0])
if __name__ == '__main__':
    rows, info = make(sys.argv[1])
    np.savetxt(sys.argv[2], rows, delimiter=',', fmt='%.5f')
    print('jumps at', np.round(info['jumps'], 2))
    print('band>0.2', (rows[:, 4] > 0.2).mean().round(3), 'space range', rows[:, 3].min().round(2), rows[:, 3].max().round(2), 'speed min', rows[:, 1].min().round(3))
