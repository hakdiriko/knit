"""The cut for the fly with an inner life (2.2): every neuron that fired in sim2.py runs (smell,
restless neurons, memory) plus the 2.1 cut (keep.npy) plus every MBON and DAN of the mushroom body,
without the antennal lobe's own circuit (sim2.py holds it silent: in this model it runs away).
usage (in tools/fly): python3 prune2.py keep.npy keep2.npy inner1.npz [inner2.npz ...]"""
import sys, numpy as np, sim
ids, W, ann = sim.load()
cc = ann.cell_class.fillna('').values
keep = set(np.load(sys.argv[1]).tolist())
for f in sys.argv[3:]: keep |= set(np.unique(np.load(f)['i']).tolist())
keep |= set(np.where(np.isin(cc, ['MBON', 'DAN']))[0].tolist())
keep -= set(np.where(np.isin(cc, ['olfactory', 'ALLN', 'LHLN', 'ALON']))[0].tolist())
keep = np.array(sorted(keep)); np.save(sys.argv[2], keep)
print('neurons', len(keep), 'synapses', W.tocsr()[keep][:, keep].nnz)
