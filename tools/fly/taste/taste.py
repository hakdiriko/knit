#!/usr/bin/env python3
"""Taste training for the fly: Hakan picks by ear, the fly's temperament moves toward the pick.

The wiring never changes. What moves is Fly::Taste (Source/dsp/Fly.h): eight numbers for how fast it
learns and forgets, how restless and jumpy it is, how hard it scratches, lurches and leans, and how
fast it calms. Each round is a small evolution strategy in log space around the current centre:

  round:  python3 tools/fly/taste/taste.py round <loop.wav> <outdir> [--n 6] [--repeat 32]
          renders n blind variants (letters A..), one of them the centre itself, and writes
          tools/fly/taste/rounds/NN.json (variants, letters, seed). Renders go to <outdir>.
  pick:   python3 tools/fly/taste/taste.py pick NN <letter> [<letter2>]
          records the pick (a second letter: "between these two") and moves the centre:
          centre += 0.6 * (pick - centre) in log space; the spread shrinks by 0.8.

State lives in tools/fly/taste/centre.json (the centre and the spread). Everything is in the repo,
so the training can be replayed. Needs build/render (see tools/render/render.cpp).
"""
import json, math, os, random, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
CENTRE = os.path.join(HERE, 'centre.json')
ROUNDS = os.path.join(HERE, 'rounds')
DEFAULT = dict(learn=0.06, forget=600.0, restless=10.0, jumpy=1.0, scratch=0.85, lurch=0.4, lean=0.35, calm=40.0)
# how far each number may wander in one round, as a factor (log-space sigma at spread 1)
WIDTH = dict(learn=2.2, forget=2.0, restless=2.0, jumpy=1.5, scratch=1.5, lurch=1.6, lean=1.6, calm=2.0)
LIMIT = dict(learn=(0.005, 0.5), forget=(30, 3600), restless=(0.5, 60), jumpy=(0.4, 2.5),
             scratch=(0.1, 1.5), lurch=(0.05, 1.0), lean=(0.0, 0.8), calm=(5, 300))


def load_centre():
    if os.path.exists(CENTRE):
        return json.load(open(CENTRE))
    return dict(taste=DEFAULT, spread=1.0, history=[])


def clamp(k, v):
    lo, hi = LIMIT[k]
    return min(hi, max(lo, v))


def spec(t):
    return ','.join(f'{k}={t[k]:.6g}' for k in DEFAULT)


def render(loop, out, t, repeat, log):
    cmd = [os.path.join(ROOT, 'build', 'render'), loop, out, '--repeat', str(repeat), '--loop-transport',
           '--taste', spec(t), '--fly-log', log]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def summary(log):
    rows = [l.split(',') for l in open(log).read().split('\n') if l]
    sec = float(rows[-1][0]) if rows else 1.0
    jumps = sum(1 for a, b in zip(rows, rows[1:]) if float(a[1]) == 0.0 and float(b[1]) > 0.0)
    scratch = sum(float(r[5]) for r in rows) / max(1, len(rows))
    fear = float(rows[-1][12]) if rows else 0.0
    return dict(jumps_per_min=round(jumps * 60.0 / sec, 1), scratch_mean=round(scratch, 3), fear_end=round(fear, 2))


def cmd_round(loop, outdir, n=6, repeat=32):
    c = load_centre()
    os.makedirs(ROUNDS, exist_ok=True)
    number = len([f for f in os.listdir(ROUNDS) if f.endswith('.json')]) + 1
    seed = 1000 + number
    rng = random.Random(seed)
    variants = [dict(c['taste'])]
    while len(variants) < n:
        t = {}
        for k, v in c['taste'].items():
            s = math.log(WIDTH[k]) * c['spread']
            t[k] = clamp(k, v * math.exp(rng.gauss(0.0, s))) if v > 0 else clamp(k, abs(rng.gauss(0.0, 0.1 * c['spread'])))
        variants.append(t)
    order = list(range(n))
    rng.shuffle(order)
    letters = 'ABCDEFGHIJ'
    os.makedirs(outdir, exist_ok=True)
    entries = {}
    for slot, v in enumerate(order):
        L = letters[slot]
        wav = os.path.join(outdir, f'round{number:02d}-{L}.wav')
        log = os.path.join(outdir, f'round{number:02d}-{L}.csv')
        render(loop, wav, variants[v], repeat, log)
        entries[L] = dict(taste=variants[v], is_centre=(v == 0), heard=summary(log))
        print(L, entries[L]['heard'])
    json.dump(dict(round=number, seed=seed, loop=os.path.basename(loop), repeat=repeat, spread=c['spread'],
                   variants=entries, pick=None), open(os.path.join(ROUNDS, f'{number:02d}.json'), 'w'), indent=1)
    print(f'round {number:02d}: {n} variants in {outdir}')


def cmd_pick(number, *letters):
    path = os.path.join(ROUNDS, f'{int(number):02d}.json')
    r = json.load(open(path))
    picked = [r['variants'][L.upper()]['taste'] for L in letters]
    target = {k: math.exp(sum(math.log(max(p[k], 1e-6)) for p in picked) / len(picked)) for k in DEFAULT}
    c = load_centre()
    moved = {k: clamp(k, math.exp(math.log(max(c['taste'][k], 1e-6)) + 0.6 * (math.log(target[k]) - math.log(max(c['taste'][k], 1e-6)))))
             for k in DEFAULT}
    r['pick'] = [L.upper() for L in letters]
    json.dump(r, open(path, 'w'), indent=1)
    c['history'].append(dict(round=r['round'], pick=r['pick'], before=c['taste'], after=moved))
    c['taste'], c['spread'] = moved, round(c['spread'] * 0.8, 4)
    json.dump(c, open(CENTRE, 'w'), indent=1)
    print('new centre:', spec(moved), 'spread', c['spread'])


if __name__ == '__main__':
    a = sys.argv[1:]
    if not a:
        print(__doc__)
    elif a[0] == 'round':
        kw = {}
        rest = a[1:]
        for flag in ('--n', '--repeat'):
            if flag in rest:
                i = rest.index(flag)
                kw[flag[2:]] = int(rest[i + 1])
                del rest[i:i + 2]
        cmd_round(rest[0], rest[1], **kw)
    elif a[0] == 'pick':
        cmd_pick(*a[1:])
