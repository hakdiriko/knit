"""The fly with an inner life (offline): the reflex brain of sim.py plus
- smell: the sound's spectrum as 54 smells (one per olfactory glomerulus, low to high), so the mushroom body hears it
- memory: KC->MBON synapses weaken where dopamine meets an active KC (it learns what it keeps smelling); they recover over minutes
- restless neurons: every neuron gets small random kicks
- moods: arousal (wound up by its own jumps) and fatigue (worn by activity) scale its senses and its restlessness
usage: python3 sim2.py in.wav out.npz seconds [--still]  (--still: no noise, no learning, no moods: the 2.1.0 fly)"""
import sys, time, os, numpy as np, pandas as pd, soundfile as sf
import sim
from sim import DT, V0, VTH, TM, TAU, TRF, DLY

def smell_features(wav, seconds, nb):
    x, sr = sf.read(wav, dtype='float32')
    if x.ndim > 1: x = x.mean(1)
    x = x[:int(seconds * sr)]
    hop = int(0.005 * sr); win = 2048
    pad = np.pad(x, (win // 2, win // 2))
    nfr = (len(pad) - win) // hop
    idx = np.arange(win)[None, :] + hop * np.arange(nfr)[:, None]
    S = np.abs(np.fft.rfft(pad[idx] * np.hanning(win), axis=1))
    fr = np.fft.rfftfreq(win, 1 / sr)
    edges = np.geomspace(60, 12000, nb + 1)
    def band(k):  # a band narrower than a bin takes its nearest bin
        m = (fr >= edges[k]) & (fr < edges[k + 1])
        return S[:, m].mean(1) if m.any() else S[:, np.argmin(np.abs(fr - np.sqrt(edges[k] * edges[k + 1])))]
    B = np.stack([band(k) for k in range(nb)], 1)
    B = np.log1p(B / (np.percentile(B, 50) + 1e-9))
    B /= np.percentile(B, 99, axis=0) + 1e-9
    # an odour is a few glomeruli, not all: keep only what stands above the spectrum's own median
    B = np.maximum(0.0, B - np.median(B, axis=1, keepdims=True)) * 2.0
    t5 = np.arange(nfr) * 0.005; t1 = np.arange(int(seconds * 1000)) * 0.001
    return np.stack([np.interp(t1, t5, B[:, k]) for k in range(nb)], 1).clip(0, 1.2)

NR, NA, ETA, SR = (float(os.environ.get(k, d)) for k, d in (('NR', 10), ('NA', 1.5), ('ETA', 0.05), ('SR', 40)))

def main():
    wav, out, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3]); still = '--still' in sys.argv
    ids, W, ann = sim.load(); g = sim.groups(ann); n = len(ids)
    ct = np.array(ann.cell_type.fillna('').tolist(), dtype=object); cc = np.array(ann.cell_class.fillna('').tolist(), dtype=object)
    # the smell enters at the projection neurons, one group per glomerulus (the antennal lobe's own
    # circuit, receptors and local neurons, is left out: in this model it runs away on its own)
    pn = np.array([k for k, c in enumerate(cc) if c == 'ALPN' and ct[k]])
    glom_of = np.array([str(ct[k]).split('_')[0] for k in pn])
    gloms = sorted(set(glom_of))
    for k, gl in enumerate(gloms): g['smell%02d' % k, 'both'] = pn[glom_of == gl]
    silent = np.array([k for k, c in enumerate(cc) if c in ('olfactory', 'ALLN', 'LHLN', 'ALON')])
    f, sr = sim.features(wav, seconds)
    sm = smell_features(wav, seconds, len(gloms))
    for k in range(len(gloms)): f['smell%02d' % k, 'both'] = sm[:, k]
    flen = min(len(v) for v in f.values())
    kc = np.where(cc == 'Kenyon_Cell')[0]; mbon = np.where(cc == 'MBON')[0]; dan = np.where(cc == 'DAN')[0]
    ppl1 = np.array([k for k, c in enumerate(ct) if str(c).startswith('PPL1')])
    W = W.tocsr()
    # the plastic synapses: every KC -> MBON entry of W
    is_mbon = np.zeros(n, bool); is_mbon[mbon] = True
    rows = np.repeat(np.arange(n), np.diff(W.indptr))
    plastic = np.where(np.isin(rows, kc) & is_mbon[W.indices])[0]
    w0 = W.data[plastic].copy(); pre = rows[plastic]; post = W.indices[plastic]
    # dopamine reaching each MBON: DAN -> MBON synapse counts (the compartments, as wired)
    Dm = W[dan][:, mbon].toarray().clip(0, None); Dm /= Dm.sum(0, keepdims=True) + 1e-9
    mpos = {m: k for k, m in enumerate(mbon)}; postk = np.array([mpos[p] for p in post])
    print('glomeruli', len(gloms), 'KC', len(kc), 'MBON', len(mbon), 'DAN', len(dan), 'plastic synapses', len(plastic))
    esc_ix = np.where(np.isin(ct, ['DNp01', 'DNp02', 'DNp04', 'DNp11']))[0]
    rng = np.random.default_rng(1)
    steps = int(seconds / DT); dly = int(round(DLY / DT))
    v = np.full(n, V0, np.float32); gg = np.zeros(n, np.float32); rf = np.zeros(n, np.int16)
    ring = [np.zeros(0, np.int64) for _ in range(dly + 1)]
    am, ag = np.float32(DT / TM), np.float32(np.exp(-DT / TAU)); nrf = int(round(TRF / DT))
    HAB = {'loomlo': (12.0, 10.0), 'loommid': (12.0, 10.0), 'loomhi': (12.0, 10.0), 'B': (0.8, 3.0), 'A': (0.8, 3.0), 'CE': (0.3, 2.0)}
    res = {key: 1.0 for key in g}
    kc_trace = np.zeros(n, np.float32); dan_trace = np.zeros(len(dan), np.float32)
    arousal, fatigue, esc_s = 0.3, 0.0, 0.0
    spk_t, spk_i, moods, wlog = [], [], [], []
    dan_pos = np.full(n, -1); dan_pos[dan] = np.arange(len(dan))
    t0 = time.time()
    for k in range(steps):
        arrive = ring[k % (dly + 1)]
        if len(arrive):
            sub = W[arrive]; np.add.at(gg, sub.indices, sub.data)
        free = rf <= 0
        v[free] += am * (V0 - v[free] + gg[free]); gg[free] *= ag; rf[~free] -= 1
        gain = 1.0 if still else (0.75 + 0.5 * arousal) * (1.0 - 0.35 * fatigue)
        if not still:  # restless: about 30 kicks of 2.5 mV per neuron per second, more when wound up
            kicks = rng.random(n) < (NR + NR * arousal) * DT
            v[kicks] += NA
        fi = min(k // 2, flen - 1); forced = []
        for key, idx in g.items():
            if len(idx) == 0: continue
            drive = float(f[key][fi])
            u, rec = HAB.get(key[0], (0.5, 5.0))
            res[key] += DT * ((1 - res[key]) / rec - u * drive * res[key])
            r = (SR if key[0].startswith('smell') else 200.0) * drive * res[key] * gain
            hit = idx[rng.random(len(idx)) < r * DT]
            if len(hit): forced.append(hit)
        if not still and esc_s > 30:  # a jump is a shock: the punishing dopamine neurons fire
            forced.append(ppl1[rng.random(len(ppl1)) < 0.05])
        if forced: v[np.concatenate(forced)] = VTH + 1
        v[silent] = V0
        s = np.where(v > VTH)[0]
        if len(s):
            v[s] = V0; gg[s] = 0; rf[s] = nrf
            spk_t.append(np.full(len(s), k, np.int32)); spk_i.append(s.astype(np.int32))
        ring[k % (dly + 1)] = s
        # traces, moods and learning every 10 ms
        kc_trace *= np.float32(np.exp(-DT / 1.0)); kc_trace[s] += 1
        dan_trace *= np.float32(np.exp(-DT / 0.5)); ds = dan_pos[s]; dan_trace[ds[ds >= 0]] += 1
        esc_s += (np.isin(s, esc_ix).sum() / (len(esc_ix) * DT) - esc_s) * (DT / 0.03)
        if k % 20 == 0 and not still:
            dt = 20 * DT
            arousal += dt * (min(1.0, esc_s / 60.0) - arousal) / 40.0 + dt * 0.02 * (0.3 - arousal)
            fatigue += dt * (min(1.0, len(s) / 8.0) - fatigue) / 120.0
            dop = dan_trace @ Dm  # per MBON
            wcur = W.data[plastic]
            e = 1.0 - np.exp(-kc_trace[pre] / 5.0); d = 1.0 - np.exp(-dop[postk] / 5.0)
            wcur *= np.exp(-dt * ETA * e * d)   # where dopamine meets an active KC, the synapse weakens
            wcur += (w0 - wcur) * (dt / 600.0)  # and it forgets over ten minutes
            W.data[plastic] = wcur
        if k % 2000 == 0:
            moods.append((k * DT, arousal, fatigue)); wlog.append(float(W.data[plastic].sum() / w0.sum()))
        if k % 20000 == 0: print(f'{k*DT:.0f}s {time.time()-t0:.0f}s wall arousal {arousal:.2f} fatigue {fatigue:.2f} memory {wlog[-1]:.3f}', flush=True)
    np.savez_compressed(out, t=np.concatenate(spk_t) * DT, i=np.concatenate(spk_i), seconds=seconds,
                        moods=np.array(moods), memory=np.array(wlog), kc=kc, mbon=mbon)
main()
