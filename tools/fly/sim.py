"""The whole FlyWire fly brain (783 release, Shiu et al. 2024 LIF model) listening to a wav.
Ears: Johnston's organ (JO-B low vibration, JO-A sound, JO-C/E slow push). Eyes: looming detectors (LPLC2, LC4)
driven by onsets. Left channel -> left side, right -> right. Records every spike.
usage: python3 sim.py in.wav out.npz [seconds]"""
import sys, time, numpy as np, pandas as pd, soundfile as sf, scipy.sparse as sp
from scipy.signal import butter, sosfilt
D = __file__.rsplit('/', 1)[0] + '/data/'
import os
MUTE = set(os.environ.get('MUTE', '').split(',')) - {''}
DT = 0.5e-3
V0, VTH, TM, TAU, TRF, DLY, WSYN = -52.0, -45.0, 20e-3, 5e-3, 2.2e-3, 1.8e-3, 0.275

def load():
    comp = pd.read_csv(D + 'Completeness_783.csv', index_col=0)
    con = pd.read_parquet(D + 'Connectivity_783.parquet')
    n = len(comp)
    W = sp.csr_matrix((con['Excitatory x Connectivity'].values.astype(np.float32) * WSYN,
                       (con['Presynaptic_Index'].values, con['Postsynaptic_Index'].values)), shape=(n, n))
    ann = pd.read_csv(D + 'Supplemental_file1_neuron_annotations.tsv', sep='\t', low_memory=False)
    ann = ann.set_index('root_id').reindex(comp.index)
    return comp.index.values, W, ann

def features(wav, seconds):
    x, sr = sf.read(wav, dtype='float32')
    if x.ndim == 1: x = np.stack([x, x], 1)
    x = x[:int(seconds * sr)]
    hop = int(sr * DT * 2)  # 1 ms
    def env(sig, lo, hi, tau):
        sos = butter(2, [lo, hi], 'bandpass', fs=sr, output='sos') if lo else butter(2, hi, 'lowpass', fs=sr, output='sos')
        y = np.abs(sosfilt(sos, sig))
        n = len(y) // hop
        y = y[:n * hop].reshape(n, hop).mean(1)
        a = np.exp(-1e-3 / tau); out = np.empty_like(y); s = 0.0
        for i, v in enumerate(y): s = a * s + (1 - a) * v; out[i] = s
        return out
    f = {}
    for side, ch in (('left', 0), ('right', 1)):
        s = x[:, ch]
        f['B', side] = env(s, 40, 300, 0.01)     # low vibration: bass, kick
        f['A', side] = env(s, 300, 4000, 0.008)  # sound
        f['CE', side] = env(s, 0, 8000, 0.25)    # slow push of the whole level
        for name, lo, hi in (('loomlo', 40, 200), ('loommid', 200, 2000), ('loomhi', 2000, 10000)):
            fast, slow = env(s, lo, hi, 0.005), env(s, lo, hi, 0.08)
            f[name, side] = np.maximum(0, np.log((fast + 1e-4) / (slow + 1e-4)))  # something coming at it
    for k in list(f):
        ref = np.percentile(np.concatenate([f[k[0], 'left'], f[k[0], 'right']]), 99.5) + 1e-9
        f[k] = np.clip(f[k] / ref, 0, 1.2)
    return f, sr

def groups(ann):
    t = ann.cell_type.fillna('').values; side = ann.side.fillna('').values
    g = {}
    for s in ('left', 'right'):
        on = side == s
        g['B', s] = np.where(on & pd.Series(t).str.match('^JO-B').values)[0]
        g['A', s] = np.where(on & pd.Series(t).str.match('^JO-A').values)[0]
        g['CE', s] = np.where(on & pd.Series(t).str.match('^JO-(C|E|F)').values)[0]
        # the looming detectors tile the eye; split them by height: lows come from below, highs from above
        eye = np.where(on & pd.Series(t).isin(['LPLC2', 'LC4']).values)[0]
        eye = eye[np.argsort(-ann.pos_y.values[eye])]
        for name, part in zip(('loomlo', 'loommid', 'loomhi'), np.array_split(eye, 3)):
            g[name, s] = part
    return g

def run(W, g, f, n, seconds, rate=float(os.environ.get('RATE', 200)), seed=1):
    rng = np.random.default_rng(seed)
    steps = int(seconds / DT); dly = int(round(DLY / DT))
    v = np.full(n, V0, np.float32); gg = np.zeros(n, np.float32); rf = np.zeros(n, np.int16)
    ring = [np.zeros(0, np.int64) for _ in range(dly + 1)]
    am, ag = np.float32(DT / TM), np.float32(np.exp(-DT / TAU)); nrf = int(round(TRF / DT))
    spk_t, spk_i = [], []
    # habituation: each sense tires with use and recovers slowly (looming hardest: a fly stops
    # jumping at a shadow that keeps coming back and never lands)
    HAB = {'loomlo': (12.0, 10.0), 'loommid': (12.0, 10.0), 'loomhi': (12.0, 10.0), 'B': (0.8, 3.0), 'A': (0.8, 3.0), 'CE': (0.3, 2.0)}  # (use per second, recovery s)
    res = {key: 1.0 for key in g}; hab_log = {key: [] for key in g}
    t0 = time.time()
    for k in range(steps):
        arrive = ring[k % (dly + 1)]
        if len(arrive):
            sub = W[arrive]
            np.add.at(gg, sub.indices, sub.data)
        free = rf <= 0
        v[free] += am * (V0 - v[free] + gg[free])
        gg[free] *= ag
        rf[~free] -= 1
        fi = k // 2
        forced = []
        for key, idx in g.items():
            if len(idx) == 0 or key[0] in MUTE or (key[0].startswith('loom') and 'loom' in MUTE): continue
            drive = float(f[key][min(fi, len(f[key]) - 1)])
            u, rec = HAB[key[0]]
            res[key] += DT * ((1 - res[key]) / rec - u * drive * res[key])
            if k % 20 == 0: hab_log[key].append(res[key])
            r = rate * drive * res[key]
            hit = idx[rng.random(len(idx)) < r * DT]
            if len(hit): forced.append(hit)
        if forced:
            fz = np.concatenate(forced); v[fz] = VTH + 1
        s = np.where(v > VTH)[0]
        if len(s):
            v[s] = V0; gg[s] = 0; rf[s] = nrf
            spk_t.append(np.full(len(s), k, np.int32)); spk_i.append(s.astype(np.int32))
        ring[k % (dly + 1)] = s
        if k % 4000 == 0: print(f'{k * DT:.1f}s  {time.time() - t0:.0f}s wall  {sum(map(len, spk_i))} spikes', flush=True)
    return np.concatenate(spk_t) * DT, np.concatenate(spk_i), {f'{a}-{b}': np.array(v) for (a, b), v in hab_log.items()}

if __name__ == '__main__':
    wav, out = sys.argv[1], sys.argv[2]; seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 26.0
    ids, W, ann = load(); g = groups(ann)
    print({f'{k[0]}-{k[1]}': len(v) for k, v in g.items()})
    f, sr = features(wav, seconds)
    t, i, hab = run(W, g, f, len(ids), seconds)
    np.savez_compressed(out, t=t, i=i, seconds=seconds, **{'hab_' + k: v for k, v in hab.items()})
    print('spikes', len(t), 'neurons that fired', len(np.unique(i)))
