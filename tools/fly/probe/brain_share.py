#!/usr/bin/env python3
"""Does the fly's brain matter? The brain-share test, as a number anyone can re-run (numpy only).

Three flies play the same material: the real one, one whose non-plastic synapse strengths are
shuffled (build/render --scramble 1234), and a no-brain stand-in, a linear filter from what reaches
its senses over the last 300 ms to its four outputs (build/render --surrogate). If they play alike,
the brain is not doing the work. A fourth, the real fly with other chance (--seed 1), shows how
alike two real flies play: the floor any likeness is read against.

    python3 tools/fly/probe/material.py build/material                  # the pieces
    python3 tools/fly/probe/brain_share.py run build/material build/baseline   # renders, about 25 min on 4 cores
    python3 tools/fly/probe/brain_share.py report build/baseline        # report.json and report.md there
    python3 tools/fly/probe/brain_share.py same build/baseline build/baseline2  # the gate: two runs, same numbers

The pieces (or single dumps) can also be taken apart by hand:
    analyze DUMP              one render's dump (build/render --fly-dump DUMP): writes DUMP/analysis.json
    fit DUMP... -o FILE       the stand-in, fitted on the given real dumps together

What is measured (every fit trains on the first half of a render and is scored on the second):
  explained   how much of each output (escape: the giant fiber that starts every move; excited: the
              descending neurons' sum that feeds the echo; groom: DNg11, the scratch; lean: left minus
              right, the turn) a ridge regression from the 44 senses over the last six 50 ms bins
              explains (R squared). "senses" are as they reach the sense neurons (tired, moods).
  place       how often a linear readout of one 50 ms bin names the right 16th of the loop, from:
              the raw senses, the output (descending) neurons, the Kenyon cells, the MBONs. Chance is 1/16ths.
  memory      how often a Kenyon cell fires, and what share of them fire in a 50 ms bin
  moves       per fly: moves, kinds, fear at five minutes, scratching; and between the real fly and each
              other one: how alike their move starts sit in the loop (histogram overlap, 1 = the same spots)
              and how many of the real fly's move starts the other one also makes (within one 16th)
"""
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(HERE, '..'))
import goal  # noqa: E402

LAGS = 6          # 6 x 50 ms = 300 ms, as Fly::standLags
OUTPUTS = ['escape', 'excited', 'groom', 'lean']  # report order
STAND_ORDER = ['escape', 'groom', 'lean', 'excited']  # Fly::StandIn order
LAMBDAS = [0.1, 1.0, 10.0, 100.0, 1000.0, 10000.0]
SEED = 1234
FLIES = ['real', 'scrambled', 'surrogate', 'reseeded']


# ---------------------------------------------------------------- reading a dump

def load(dump, spikes=True):
    with open(os.path.join(dump, 'meta.json')) as f:
        meta = json.load(f)
    d = dict(meta=meta, senses=np.load(os.path.join(dump, 'senses.npy')), heard=np.load(os.path.join(dump, 'heard.npy')),
             out=np.load(os.path.join(dump, 'outputs.npy')))
    sp = os.path.join(dump, 'spikes.npy')
    d['spikes'] = np.load(sp, mmap_mode='r') if spikes and os.path.exists(sp) else None
    return d


def column(d, name):
    return d['out'][:, d['meta']['outputs'].index(name)].astype(np.float64)


def lagged(x):
    """rows t >= LAGS-1: [x[t], x[t-1], ..., x[t-LAGS+1]] (lag-major, as Fly::StandIn)"""
    n = len(x)
    return np.hstack([x[LAGS - 1 - k:n - k] for k in range(LAGS)])


# ---------------------------------------------------------------- ridge

def ridge(Xtr, Ytr, lam):
    mu, sd = Xtr.mean(0), Xtr.std(0)
    sd[sd < 1e-9] = np.inf  # a column that never moves gets no weight
    Z = (Xtr - mu) / sd
    ym = Ytr.mean(0)
    A = Z.T @ Z + lam * np.eye(Z.shape[1])
    W = np.linalg.solve(A, Z.T @ (Ytr - ym))
    Wraw = W / sd[:, None]
    b = ym - mu @ Wraw
    return Wraw, b


def r2(y, p):
    sst = np.sum((y - y.mean()) ** 2)
    return float(1.0 - np.sum((y - p) ** 2) / sst) if sst > 1e-12 else float('nan')


def fit_select(Xtr, Ytr, score):
    """choose the ridge strength on the last quarter of the training half, refit on all of it"""
    cut = int(len(Xtr) * 0.75)
    best, lam_best = -np.inf, LAMBDAS[0]
    for lam in LAMBDAS:
        W, b = ridge(Xtr[:cut], Ytr[:cut], lam)
        s = score(Ytr[cut:], Xtr[cut:] @ W + b)
        if s > best:
            best, lam_best = s, lam
    return ridge(Xtr, Ytr, lam_best) + (lam_best,)


def explained(X, y):
    n = len(y) // 2
    W, b, lam = fit_select(X[:n], y[:n, None], lambda a, p: r2(a[:, 0], p[:, 0]))
    return r2(y[n:], (X[n:] @ W + b)[:, 0])


def accuracy(Y, P):
    return float(np.mean(np.argmax(P, 1) == np.argmax(Y, 1)))


def decode_place(X, slot, classes):
    n = len(slot) // 2
    Y = np.eye(classes)[slot]
    keep = X[:n].std(0) > 1e-9
    if not keep.any():
        return 1.0 / classes
    X = X[:, keep]
    W, b, lam = fit_select(X[:n], Y[:n], accuracy)
    return accuracy(Y[n:], X[n:] @ W + b)


# ---------------------------------------------------------------- one dump

def analyze(dump):
    d = load(dump)
    m = d['meta']
    res = dict(bins=int(len(d['out'])), bin=m['bin'])
    # explained: senses over 300 ms -> each output
    for name, src in (('explained', 'heard'), ('explained_raw', 'senses')):
        X = lagged(d[src].astype(np.float64))
        res[name] = {o: round(explained(X, column(d, o)[LAGS - 1:]), 4) for o in OUTPUTS}
    # place in the loop, from one bin
    six = 60.0 / m['bpm'] / 4.0
    classes = max(1, int(round(m['loopSeconds'] / six)))
    centre = column(d, 'seconds') - m['bin'] / 2
    slot = np.minimum(classes - 1, ((centre % m['loopSeconds']) / six).astype(int))
    place = dict(chance=round(1.0 / classes, 4), sixteenths=classes,
                 senses=round(decode_place(d['senses'].astype(np.float64), slot, classes), 4))
    sp = d['spikes']
    if sp is not None:
        groups = dict(outputs=m['escape'] + m['groom'] + m['left'] + m['right'], kenyon=m['kenyon'], mbon=m['mbon'])
        for g, idx in groups.items():
            place[g] = round(decode_place(np.asarray(sp[:, idx], dtype=np.float64), slot, classes), 4)
        kc = np.asarray(sp[:, m['kenyon']], dtype=np.float64)
        res['memory'] = dict(kc_hz=round(float(kc.sum() / (kc.shape[0] * m['bin'] * kc.shape[1])), 4),
                             kc_active_per_bin=round(float(np.mean(kc > 0)), 4),
                             kc_ever=round(float(np.mean(kc.sum(0) > 0)), 4),
                             all_hz=round(float(np.asarray(sp).sum(dtype=np.int64) / (sp.shape[0] * m['bin'] * sp.shape[1])), 4))
    res['place'] = place
    with open(os.path.join(dump, 'analysis.json'), 'w') as f:
        json.dump(res, f, indent=1)
    return res


# ---------------------------------------------------------------- the stand-in

def fit(dumps, path):
    Xs, Ys, Xv, Yv = [], [], [], []
    for dump in dumps:
        d = load(dump, spikes=False)
        X = lagged(d['heard'].astype(np.float64))
        Y = np.stack([column(d, o)[LAGS - 1:] for o in STAND_ORDER], 1)
        cut = int(len(X) * 0.75)
        Xs.append(X[:cut]); Ys.append(Y[:cut]); Xv.append(X[cut:]); Yv.append(Y[cut:])
    Xs, Ys, Xv, Yv = map(np.vstack, (Xs, Ys, Xv, Yv))
    rows, scores = [], {}
    for j, o in enumerate(STAND_ORDER):
        best = None
        for lam in LAMBDAS:
            W, b = ridge(Xs, Ys[:, j:j + 1], lam)
            s = r2(Yv[:, j], (Xv @ W + b)[:, 0])
            if best is None or s > best[0]:
                best = (s, lam)
        W, b = ridge(np.vstack([Xs, Xv]), np.vstack([Ys, Yv])[:, j:j + 1], best[1])
        rows.append(np.concatenate([W[:, 0], b]))
        scores[o] = round(best[0], 4)
    with open(path, 'w') as f:
        for r in rows:
            f.write(' '.join(f'{x:.9g}' for x in r) + '\n')
    return scores


# ---------------------------------------------------------------- moves

def move_starts(log):
    d = np.loadtxt(log, delimiter=',', ndmin=2)
    t, move = d[:, 0], d[:, 13].astype(int)
    on = (move[1:] > 0) & (move[:-1] == 0)
    return t[1:][on], move[1:][on]


def compare(piece, a_log, b_log):
    six = 60.0 / piece['bpm'] / 4.0
    loop = piece['loopSeconds']
    classes = max(1, int(round(loop / six)))
    ta, _ = move_starts(a_log)
    tb, _ = move_starts(b_log)
    if len(ta) == 0 and len(tb) == 0:
        return dict(spots_alike=None, same_moments=None)
    ha = np.bincount((np.round((ta % loop) / six).astype(int)) % classes, minlength=classes) / max(1, len(ta))
    hb = np.bincount((np.round((tb % loop) / six).astype(int)) % classes, minlength=classes) / max(1, len(tb))
    alike = float(np.sum(np.minimum(ha, hb))) if len(ta) and len(tb) else 0.0
    same = float(np.mean([np.min(np.abs(tb - x)) <= six * 1.01 for x in ta])) if len(ta) and len(tb) else 0.0
    return dict(spots_alike=round(alike, 3), same_moments=round(same, 3))


# ---------------------------------------------------------------- the whole run

def render(piece, wav, outdir, extra):
    os.makedirs(outdir, exist_ok=True)
    cmd = [os.environ.get('RENDER', os.path.join(ROOT, 'build', 'render')), wav, '-', '--bpm', str(piece['bpm']), '--bar', str(piece['barBeats']),
           '--repeat', str(piece['repeat']), '--fly-log', os.path.join(outdir, 'fly.csv'), '--fly-dump', outdir,
           '--stats', os.path.join(outdir, 'stats.json')] + extra
    subprocess.run(cmd, check=True, stderr=subprocess.DEVNULL)


def run(material, out, jobs=4, keep_spikes=False):
    with open(os.path.join(material, 'pieces.json')) as f:
        pieces = json.load(f)
    os.makedirs(out, exist_ok=True)

    def one(piece, fly, extra):
        where = os.path.join(out, piece['name'], fly)
        render(piece, os.path.join(material, piece['file']), where, extra)
        analyze(where)
        if not keep_spikes:
            os.remove(os.path.join(where, 'spikes.npy'))
        print(f"  {piece['name']:10s} {fly}", flush=True)

    print('real, scrambled and reseeded flies')
    with ThreadPoolExecutor(jobs) as pool:
        list(pool.map(lambda a: one(*a), [(p, f, x) for p in pieces for f, x in
                                           (('real', []), ('scrambled', ['--scramble', str(SEED)]), ('reseeded', ['--seed', '1']))]))
    print('fitting the stand-in on every real fly together')
    stand = os.path.join(out, 'standin.txt')
    scores = fit([os.path.join(out, p['name'], 'real') for p in pieces], stand)
    with open(os.path.join(out, 'standin.json'), 'w') as f:
        json.dump(dict(validation_r2=scores), f, indent=1)
    print('the stand-in', scores)
    with ThreadPoolExecutor(jobs) as pool:
        list(pool.map(lambda p: one(p, 'surrogate', ['--surrogate', stand]), pieces))
    with open(os.path.join(out, 'pieces.json'), 'w') as f:
        json.dump(pieces, f, indent=1)
    subprocess.run(['git', '-C', ROOT, 'rev-parse', 'HEAD'], stdout=open(os.path.join(out, 'commit.txt'), 'w'))


def report(out):
    with open(os.path.join(out, 'pieces.json')) as f:
        pieces = json.load(f)
    with open(os.path.join(out, 'standin.json')) as f:
        stand = json.load(f)
    rep = dict(standin=stand, pieces={})
    for p in pieces:
        r = dict(bpm=p['bpm'], barBeats=p['barBeats'])
        for fly in FLIES:
            where = os.path.join(out, p['name'], fly)
            with open(os.path.join(where, 'analysis.json')) as f:
                a = json.load(f)
            g = goal.measure(os.path.join(where, 'fly.csv'), p['bpm'], os.path.join(where, 'stats.json'), p['barBeats'])
            r[fly] = dict(analysis=a, goal={k: (float(v) if isinstance(v, np.floating) else v) for k, v in g.items()})
        for fly in FLIES[1:]:
            r['real_vs_' + fly] = compare(p, os.path.join(out, p['name'], 'real', 'fly.csv'), os.path.join(out, p['name'], fly, 'fly.csv'))
        rep['pieces'][p['name']] = r

    def mean(f):
        v = [f(r) for r in rep['pieces'].values()]
        v = [x for x in v if x is not None and not (isinstance(x, float) and np.isnan(x))]
        return round(float(np.mean(v)), 3) if v else None

    rep['mean'] = dict(
        explained={o: mean(lambda r, o=o: r['real']['analysis']['explained'][o]) for o in OUTPUTS},
        explained_scrambled={o: mean(lambda r, o=o: r['scrambled']['analysis']['explained'][o]) for o in OUTPUTS},
        place={g: mean(lambda r, g=g: r['real']['analysis']['place'].get(g)) for g in ('senses', 'outputs', 'kenyon', 'mbon', 'chance')},
        kc_hz=mean(lambda r: r['real']['analysis']['memory']['kc_hz']),
        moves={f: mean(lambda r, f=f: r[f]['goal']['moves']) for f in FLIES},
        fear_5min={f: mean(lambda r, f=f: r[f]['goal']['fear_5min']) for f in FLIES},
        spots_alike={f: mean(lambda r, f=f: r['real_vs_' + f]['spots_alike']) for f in FLIES[1:]},
        same_moments={f: mean(lambda r, f=f: r['real_vs_' + f]['same_moments']) for f in FLIES[1:]},
    )
    with open(os.path.join(out, 'report.json'), 'w') as f:
        json.dump(rep, f, indent=1)
    with open(os.path.join(out, 'report.md'), 'w') as f:
        f.write(markdown(rep))
    return rep


def pct(x):
    return '–' if x is None or (isinstance(x, float) and np.isnan(x)) else f'{100 * x:.0f}%'


def markdown(rep):
    L = []
    L.append('### The senses alone explain the outputs (real fly, R² on the second half; senses over the last 300 ms)\n')
    L.append('| piece | escape | excited | groom | lean | escape, scrambled |')
    L.append('|---|---|---|---|---|---|')
    for n, r in rep['pieces'].items():
        e = r['real']['analysis']['explained']
        L.append(f"| {n} | {pct(e['escape'])} | {pct(e['excited'])} | {pct(e['groom'])} | {pct(e['lean'])} | {pct(r['scrambled']['analysis']['explained']['escape'])} |")
    m = rep['mean']['explained']
    L.append(f"| **mean** | {pct(m['escape'])} | {pct(m['excited'])} | {pct(m['groom'])} | {pct(m['lean'])} | {pct(rep['mean']['explained_scrambled']['escape'])} |\n")
    L.append('### Place in the loop, read from one 50 ms bin (real fly, second half)\n')
    L.append('| piece | 16ths | chance | raw senses | output neurons | Kenyon cells | MBONs | KC Hz | KCs active per bin |')
    L.append('|---|---|---|---|---|---|---|---|---|')
    for n, r in rep['pieces'].items():
        p, mem = r['real']['analysis']['place'], r['real']['analysis']['memory']
        L.append(f"| {n} | {p['sixteenths']} | {pct(p['chance'])} | {pct(p['senses'])} | {pct(p['outputs'])} | {pct(p['kenyon'])} | {pct(p['mbon'])} | {mem['kc_hz']:.2f} | {pct(mem['kc_active_per_bin'])} |")
    m = rep['mean']['place']
    L.append(f"| **mean** | | {pct(m['chance'])} | {pct(m['senses'])} | {pct(m['outputs'])} | {pct(m['kenyon'])} | {pct(m['mbon'])} | {rep['mean']['kc_hz']:.2f} | |\n")
    L.append('### Four flies, five minutes each (r real, s scrambled, st stand-in, r2 real with other chance)\n')
    L.append('| piece | moves r / s / st / r2 | per bar (r) | fear at 5 min r / s / st / r2 | scratching r / s / st / r2 | spots alike r~s / r~st / r~r2 | same moments r~s / r~st / r~r2 | longest same move (r) |')
    L.append('|---|---|---|---|---|---|---|---|')

    def f3(r, key, fmt):
        return ' / '.join(fmt(r[f]['goal'][key]) for f in FLIES)
    for n, r in rep['pieces'].items():
        L.append(f"| {n} | {f3(r, 'moves', str)} | {r['real']['goal']['per_bar']:.2f} | {f3(r, 'fear_5min', lambda x: '–' if x is None else f'{x:.2f}')} | "
                 f"{f3(r, 'scratching', pct)} | {' / '.join(pct(r['real_vs_' + f]['spots_alike']) for f in FLIES[1:])} | "
                 f"{' / '.join(pct(r['real_vs_' + f]['same_moments']) for f in FLIES[1:])} | {r['real']['goal']['same_in_a_row']} |")
    m = rep['mean']
    L.append(f"| **mean** | {' / '.join(f'{m['moves'][f]:.1f}' for f in FLIES)} | | {' / '.join(f'{m['fear_5min'][f]:.2f}' for f in FLIES)} | | "
             f"{' / '.join(pct(m['spots_alike'][f]) for f in FLIES[1:])} | {' / '.join(pct(m['same_moments'][f]) for f in FLIES[1:])} | |\n")
    L.append('### Move kinds\n')
    L.append('| piece | real | scrambled | stand-in | real, other chance |')
    L.append('|---|---|---|---|---|')
    for n, r in rep['pieces'].items():
        L.append(f"| {n} | " + ' | '.join(', '.join(f'{k} {v}' for k, v in r[f]['goal']['kinds'].items()) or '–' for f in FLIES) + ' |')
    L.append('')
    L.append('### Cost (render machine, not the plug-in: one run, timing is not reproducible)\n')
    L.append('| piece | mean load real | 1 block in 1000 real | slowest block real | mean load stand-in |')
    L.append('|---|---|---|---|---|')
    for n, r in rep['pieces'].items():
        g = r['real']['goal']
        L.append(f"| {n} | {pct(g['mean_load'])} | {pct(g['block_999'])} | {pct(g['slowest_block'])} | {pct(r['surrogate']['goal']['mean_load'])} |")
    L.append('')
    L.append(f"The stand-in's own fit (pooled, last quarter of each render held out): " +
             ', '.join(f'{k} {pct(v)}' for k, v in rep['standin']['validation_r2'].items()) + '.\n')
    return '\n'.join(L)


TIMING = {'slowest_block', 'block_999', 'mean_load'}


def strip(x):
    if isinstance(x, dict):
        return {k: strip(v) for k, v in x.items() if k not in TIMING}
    return x


def same(a, b):
    ra, rb = (json.load(open(os.path.join(d, 'report.json'))) for d in (a, b))
    sa, sb = strip(ra), strip(rb)
    diffs = []

    def walk(x, y, path):
        if isinstance(x, dict):
            for k in set(x) | set(y):
                walk(x.get(k), y.get(k), path + '.' + k)
        elif x != y and not (isinstance(x, float) and isinstance(y, float) and np.isnan(x) and np.isnan(y)):
            diffs.append((path, x, y))  # nan is nan (an output that never moved has no R squared)
    walk(sa, sb, '')
    for d in diffs[:40]:
        print('differs:', *d)
    print('SAME: every number matches (timing left out)' if not diffs else f'{len(diffs)} numbers differ')
    return not diffs


if __name__ == '__main__':
    a = sys.argv[1:]
    if not a:
        print(__doc__); sys.exit(1)
    if a[0] == 'analyze':
        print(json.dumps(analyze(a[1]), indent=1))
    elif a[0] == 'fit':
        o = a.index('-o'); print(fit(a[1:o], a[o + 1]))
    elif a[0] == 'run':
        run(a[1], a[2], keep_spikes='--keep-spikes' in a)
    elif a[0] == 'report':
        report(a[1]); print(open(os.path.join(a[1], 'report.md')).read())
    elif a[0] == 'same':
        sys.exit(0 if same(a[1], a[2]) else 1)
    else:
        print(__doc__); sys.exit(1)
