"""Peaks of what the reflexes read, from a sim.py run: escape (DNp01/02/04/11) and DNg11, smoothed as Fly.cpp
smooths them (30 ms and 150 ms), as percentiles over the run, and how often escape crosses 45 Hz.
usage (in tools/fly): [FLY=malecns] python3 peaks.py run.npz [...]"""
import sys, numpy as np
runs = sys.argv[1:]; sys.argv = ['x']
import sim
ids, W, ann = sim.load(); ct = ann.cell_type.fillna('').values
esc = np.where(np.isin(ct, ['DNp01', 'DNp02', 'DNp04', 'DNp11']))[0]; dng = np.where(ct == 'DNg11')[0]
def smooth(x, tau, dt=1e-3):
    a = np.exp(-dt / tau); y = np.empty_like(x); s = 0.0
    for i, v in enumerate(x): s = a * s + (1 - a) * v; y[i] = s
    return y
for r in runs:
    z = np.load(r); sec = float(z['seconds']); n = int(sec * 1000)
    def rate(ix):
        m = np.isin(z['i'], ix); h = np.bincount((z['t'][m] * 1000).astype(int), minlength=n)[:n]
        return h / len(ix) / 1e-3
    e = smooth(rate(esc), 0.03); g = smooth(rate(dng), 0.15)
    up = np.sum((e[1:] > 45) & (e[:-1] <= 45))
    print(f'{r.split("/")[-1]:20s} escape p50 {np.percentile(e,50):5.1f} p99 {np.percentile(e,99):6.1f} max {e.max():6.1f} Hz, crossings of 45 Hz: {up:3d};  DNg11 p50 {np.percentile(g,50):5.1f} p99 {np.percentile(g,99):5.1f}')
