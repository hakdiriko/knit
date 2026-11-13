import numpy as np, scipy.sparse as sp, sys
sys.argv=['x']; import sim
ids, W, ann = sim.load(); g = sim.groups(ann)
keep = set()
for m in ['loop','drums','chords','song','pad','voice','pink','hot']:
    keep |= set(np.unique(np.load(m+'.npz')['i']).tolist())
for v in g.values(): keep |= set(v.tolist())
keep = np.array(sorted(keep)); print('neurons', len(keep))
Wk = W[keep][:, keep].tocsr(); print('synapse pairs', Wk.nnz)
np.save('keep.npy', keep)
