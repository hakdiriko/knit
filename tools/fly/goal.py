#!/usr/bin/env python3
"""The fly's goal, measured on a fly log (build/render --fly-log): python3 tools/fly/goal.py fly.csv <bpm>

  on the grid    share of move starts, throw starts and scratch changes within 10 ms of a 16th line (goal: 95%)
  variety        how many kinds of move in the render, and the commonest one's share (goal: 3 or more, under 60%)
  not a loop     how alike one 4-bar phrase's moves are to the next (goal: under 60% alike)
  breathing      share of 16ths with nothing happening at all (goal: 30% to 80%: it must leave room)
"""
import sys
import numpy as np

NAMES = ['none', 'hop', 'leap', 'duck', 'dive', 'rush']


def measure(path, bpm):
    d = np.loadtxt(path, delimiter=',')
    t, jump, thr, scratch = d[:, 0], d[:, 1], d[:, 3], d[:, 5]
    move = d[:, 13].astype(int) if d.shape[1] > 13 else (jump > 0).astype(int)
    six = 60.0 / bpm / 4.0
    hop = t[1] - t[0]

    def starts(x):
        return t[1:][(x[1:] > 0) & (x[:-1] <= 0)]

    def off(times):  # distance to the nearest 16th line, the log's own step taken off
        return np.maximum(0.0, np.abs((times + six / 2) % six - six / 2) - hop)

    events = np.concatenate([starts(move), starts(thr), starts(scratch - 0.1)])
    on_grid = float(np.mean(off(events) < 0.010)) if len(events) else 1.0
    kinds = move[1:][(move[1:] > 0) & (move[:-1] == 0)]
    counts = {NAMES[k] if k < len(NAMES) else str(k): int(np.sum(kinds == k)) for k in sorted(set(kinds))}
    top = max(counts.values()) / max(1, len(kinds)) if counts else 1.0
    # phrases: a 64-step picture of what happens in each 4 bars (which move, scratching or not)
    n16 = int(t[-1] / six)
    idx = np.minimum(len(t) - 1, ((np.arange(n16) + 0.5) * six / hop).astype(int))
    picture = move[idx] * 2 + (scratch[idx] > 0.1)
    phrases = [picture[i:i + 64] for i in range(0, n16 - 63, 64)]
    alike = float(np.mean([np.mean(a == b) for a, b in zip(phrases[2:], phrases[3:])])) if len(phrases) > 3 else float('nan')
    idle = float(np.mean(picture == 0))
    return dict(moves=len(kinds), per_bar=round(len(kinds) / max(1e-9, t[-1] / (six * 16)), 2), on_grid=round(on_grid, 3),
                kinds=counts, commonest=round(top, 2), phrase_alike=round(alike, 2), idle=round(idle, 2))


if __name__ == '__main__':
    print(measure(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 120.0))
