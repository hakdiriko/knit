"""The reflex cut for any fly: every neuron that fired in the given sim.py runs, plus every sense neuron and
every neuron dendrite reads (escape, grooming, steering).
(prune.py made the FlyWire fly's cut from its eight pieces by name; this takes the runs as arguments.)
usage (in tools/fly): [FLY=malecns] python3 prune_runs.py keep.npy run1.npz [run2.npz ...]"""
import sys, numpy as np
out, runs = sys.argv[1], sys.argv[2:]
sys.argv = ['x']
import sim
ids, W, ann = sim.load(); g = sim.groups(ann)
keep = set()
for r in runs:
    keep |= set(np.unique(np.load(r)['i']).tolist())
for v in g.values():
    keep |= set(v.tolist())
# every neuron dendrite reads, fired or not: escape, grooming, steering (each side needs all its cells)
ct = ann.cell_type.fillna('').values
keep |= set(np.where(np.isin(ct, ['DNp01', 'DNp02', 'DNp04', 'DNp11', 'DNg11', 'DNa01', 'DNa02']))[0].tolist())
keep = np.array(sorted(keep)); np.save(out, keep)
print('neurons', len(keep), 'synapse pairs', W.tocsr()[keep][:, keep].nnz)
