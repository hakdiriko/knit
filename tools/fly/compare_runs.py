"""How a whole-brain run (sim.py) behaves, by the numbers dendrite reads: neurons that fired, the brain's spike
rate, and the rates of the escape neurons, DNg11 (grooming), DNa01/02 (steering) per side, over the run.
usage (in tools/fly): [FLY=malecns] python3 compare_runs.py run.npz [run.npz ...]"""
import sys, numpy as np
runs = sys.argv[1:]; sys.argv = ['x']
import sim
ids, W, ann = sim.load()
ct = ann.cell_type.fillna('').values; side = ann.side.fillna('').values
groups = {'escape': np.isin(ct, ['DNp01', 'DNp02', 'DNp04', 'DNp11']), 'DNp01': ct == 'DNp01', 'DNg11': ct == 'DNg11',
          'steerL': np.isin(ct, ['DNa01', 'DNa02']) & (side == 'left'), 'steerR': np.isin(ct, ['DNa01', 'DNa02']) & (side == 'right'),
          'KC': ann.cell_class.fillna('').values == 'Kenyon_Cell', 'DN': ann.super_class.fillna('').values == 'descending'}
for r in runs:
    z = np.load(r); sec = float(z['seconds']); cnt = np.bincount(z['i'], minlength=len(ids))
    row = [f'{r.split("/")[-1]:24s} fired {int((cnt > 0).sum()):6d}  brain {len(z["i"]) / sec / 1000:7.0f}k/s']
    for k, m in groups.items():
        row.append(f'{k} {cnt[m].sum() / max(m.sum(), 1) / sec:6.1f} Hz')
    print('  '.join(row))
