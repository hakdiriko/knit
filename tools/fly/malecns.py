"""The male fly (Janelia's male CNS v1.0, Berg et al. 2026, CC BY 4.0) in the shape sim.py expects:
ids, W (synapse count x sign x WSYN, pre -> post) and an annotation table with FlyWire's column names.
Only the brain is kept (both optic lobes included, the nerve cord left out), so it is the same part of
the animal as the FlyWire fly.

Data, in tools/fly/data/malecns/ (about 570 MB, not in the repo), from the public bucket
gs://flyem-male-cns/v1.0/connectome-data/flat-connectome/ (https://male-cns.janelia.org/download/):
  body-annotations.feather   <- body-annotations-male-cns-v1.0-minconf-0.5.feather
  nt.feather                 <- body-neurotransmitters-male-cns-v1.0.feather
  weights-traced.feather     <- connectome-weights-male-cns-v1.0-minconf-0.5-traced-only.feather
The first load writes cache.npz next to them."""
import os, numpy as np, pandas as pd, scipy.sparse as sp

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'malecns') + '/'

SUPER = {'cb_intrinsic': 'central', 'ol_intrinsic': 'optic', 'visual_projection': 'visual_projection',
         'visual_centrifugal': 'visual_centrifugal', 'descending_neuron': 'descending', 'ascending_neuron': 'ascending',
         'cb_sensory': 'sensory', 'ol_sensory': 'sensory', 'sensory_ascending': 'sensory', 'sensory_descending': 'sensory',
         'cb_motor': 'motor', 'cb_endocrine': 'endocrine', 'cb_efferent': 'motor', 'efferent_ascending': 'ascending',
         'efferent_descending': 'descending', 'ENS': 'endocrine'}
SIDE = {'L': 'left', 'R': 'right', 'M': 'center'}
# Janelia's synapse detection finds more synapses per connection than FlyWire's: over the 4,606 cell types
# found in both brains with at least 50 input synapses, a neuron's total input is 1.77 times larger (median;
# quartiles 1.40 and 2.29). The two atlases' transmitter predictions also differ: 36.7% of the male's synapses
# are inhibitory (GABA, glutamate) against FlyWire's 40.4%, and at 1.77 with that balance the male's central
# complex and the GNG's DNg33 fire on by themselves at up to 300 Hz, and wide awake its mushroom body runs away.
# So (2026-11-21, tools/fly/compare_runs.py on keys, groove and the groove at RATE=400):
#   INH 1.17: an inhibitory synapse weighs 1.17 times an excitatory one, which puts the share back at 40.4%;
#   SCALE 2.0: a synapse is worth 0.275 / 2.0 mV, inside the quartiles, where the whole brain fires as much
#   as the FlyWire brain on the same music (57k against 56k spikes a second on keys) and startles as often
#   (6 escape bursts over 45 Hz in 40 s against 6), with no runaway when wide awake.
SCALE = float(os.environ.get('MALE_SCALE', 2.0))
INH = float(os.environ.get('MALE_INH', 1.17))
WSYN = 0.275 / SCALE
# The senses: the music has to reach the brain as hard as it reached the FlyWire fly, whose levels dendrite was
# tuned on. Each sense group's input rate is scaled so its total output (rate x synapses x WSYN) matches the
# FlyWire fly's (these totals, FlyWire output synapses per group, are the only FlyWire numbers used here). The
# male's right antenna is mostly reconstructed as fragments for JO-A and JO-B (24 and 11 neurons, about 1,000
# synapses each group), so their gain is capped at 3: that ear stays quieter than the left.
REFERENCE = {('B', 'left'): 5004, ('A', 'left'): 1232, ('CE', 'left'): 33391, ('B', 'right'): 4611, ('A', 'right'): 3948,
             ('CE', 'right'): 33583, ('loomlo', 'left'): 18297, ('loommid', 'left'): 20052, ('loomhi', 'left'): 21556,
             ('loomlo', 'right'): 16083, ('loommid', 'right'): 20254, ('loomhi', 'right'): 22162}
GAIN = {}
# The readouts. The plug-in's reflexes were tuned on what the FlyWire fly's output neurons do, so each is
# rescaled to mean the same here. Measured 2026-11-21 with the renderer (--fly-dump, outputs.npy) on keys,
# groove, dubloop and voice (62 s loops, default settings), this cut uncalibrated against the FlyWire plug-in:
#   groomGain 1.63: the grooming neurons (DNg11) swing 1.63 times less (pooled standard deviation);
#   leanGain 10.3: the male's steering neurons (DNa01/02) hardly fire, so its turns are 10 times smaller;
#   excitedScale 0.276: its descending neurons fire about 3.6 times more at rest (18 against 5 Hz, medians);
#     a scale, not an offset, because on the voice it rests at 5 Hz, and an offset would leave it below zero;
#   feelingRest -0.606: its keep-away MBONs (glutamate) outweigh the come-closer ones at rest (-0.47 against +0.14).
READOUT = {'groomGain': 1.63, 'leanGain': 10.3, 'excitedScale': 0.276, 'feelingRest': -0.606}
# Shiu et al. 2024's rule, as their FlyWire table applies it: GABA and glutamate inhibit, the rest excite
INHIBIT = {'gaba', 'glutamate'}


def in_brain(superclass):
    s = '' if superclass is None or (isinstance(superclass, float) and np.isnan(superclass)) else str(superclass)
    return s != '' and not s.startswith('vnc') and not s.endswith('_tbc')


def annotations():
    a = pd.read_feather(D + 'body-annotations.feather').set_index('bodyId')
    nt = pd.read_feather(D + 'nt.feather').set_index('body')
    return a, nt


def load(WSYN):
    cache = D + 'cache.npz'
    a, nt = annotations()
    if os.path.exists(cache):
        z = np.load(cache)
        ids, pre, post, cnt = z['ids'], z['pre'], z['post'], z['cnt']
    else:
        import pyarrow.feather as pf
        t = pf.read_table(D + 'weights-traced.feather', columns=['body_pre', 'body_post', 'weight'])
        bpre, bpost, cnt = (t.column(k).to_numpy() for k in range(3))
        bodies = np.union1d(bpre, bpost)
        keep = np.array([in_brain(s) for s in a.superclass.reindex(bodies).values])
        ids = bodies[keep]
        ix = pd.Index(ids)
        pre, post = ix.get_indexer(bpre), ix.get_indexer(bpost)
        ok = (pre >= 0) & (post >= 0) & (pre != post)
        pre, post, cnt = pre[ok].astype(np.int32), post[ok].astype(np.int32), cnt[ok].astype(np.int32)
        np.savez(cache, ids=ids, pre=pre, post=post, cnt=cnt)
    n = len(ids)
    ann = a.reindex(ids)
    ntr = nt.reindex(ids)
    top = ntr.consensus_nt.fillna(ntr.celltype_predicted_nt).fillna(ntr.predicted_nt).fillna('')
    sign = np.where(np.isin(top.values, list(INHIBIT)), -INH, 1).astype(np.float32)
    W = sp.csr_matrix((cnt.astype(np.float32) * sign[pre] * WSYN, (pre, post)), shape=(n, n))
    loc = np.array([x if x is not None and len(x) == 3 else [np.nan] * 3 for x in ann.somaLocation.values], float)
    side = ann.somaSide.where(ann.somaSide.notna(), ann.rootSide).fillna('').astype(object).values.copy()
    # a sensory axon with no side noted (no soma in the volume): the side of the cells it talks to.
    # The male's x runs from the fly's right to its left, so left is above the midline.
    mid = np.nanmedian(loc[:, 0])
    unsided = np.where(~np.isin(side, ['L', 'R', 'M']))[0]
    if len(unsided):
        A = sp.csr_matrix((cnt.astype(np.float32), (pre, post)), shape=(n, n))
        px = np.nan_to_num(loc[:, 0], nan=mid) - mid; has = (~np.isnan(loc[:, 0])).astype(np.float32)
        sub = A[unsided]
        lean = sub @ px / np.maximum(sub @ has, 1)
        side[unsided] = np.where(np.asarray(sub @ has).ravel() > 0, np.where(lean > 0, 'L', 'R'), '')
    out = pd.DataFrame({
        'cell_type': ann.type.values,
        'flywire_type': ann.flywireType.values,
        'cell_class': ann['class'].values,
        'super_class': [SUPER.get(s, '') for s in ann.superclass.fillna('').values],
        'side': [SIDE.get(s, '') for s in side],
        'top_nt': top.values,
        'pos_x': loc[:, 0], 'pos_y': loc[:, 1], 'pos_z': loc[:, 2],
    }, index=pd.Index(ids, name='root_id'))
    import sim
    out_syn = np.asarray(abs(W).sum(1)).ravel() / 0.275  # what its synapses weigh, in FlyWire synapses
    for key, idx in sim.groups(out).items():
        if key in REFERENCE and len(idx):
            GAIN[key] = float(np.clip(REFERENCE[key] / max(out_syn[idx].sum(), 1.0), 0.05, 3.0))
    return ids, W, out


def places(ids, W=None):
    """Each neuron's place (x, y in 8 nm voxels): its soma, or for a neuron whose soma lies outside the
    volume (the ear's and the eye's sense neurons) the mean of its synapses. Needs, once,
    syn-partners.feather <- syn-partners-male-cns-v1.0-minconf-0.5-traced-only.feather (3 GB); the result
    is cached in places.npz."""
    cache = D + 'places.npz'
    if os.path.exists(cache):
        z = np.load(cache)
        if np.array_equal(z['ids'], ids):
            return z['x'].copy(), z['y'].copy()
    a, _ = annotations()
    loc = np.array([x if x is not None and len(x) == 3 else [np.nan] * 3 for x in a.somaLocation.reindex(ids).values], float)
    x, y = loc[:, 0].copy(), loc[:, 1].copy()
    need = np.isnan(x)
    import pyarrow.ipc as ipc
    ix = pd.Index(ids)
    sx = np.zeros(len(ids)); sy = np.zeros(len(ids)); sn = np.zeros(len(ids))
    r = ipc.open_file(D + 'syn-partners.feather')
    for b in range(r.num_record_batches):
        t = r.get_batch(b)
        for side in ('pre', 'post'):
            k = ix.get_indexer(t.column('body_' + side).to_numpy())
            ok = k >= 0
            k = k[ok]; keep = need[k]; k = k[keep]
            sx += np.bincount(k, t.column('x_' + side).to_numpy()[ok][keep], len(ids))
            sy += np.bincount(k, t.column('y_' + side).to_numpy()[ok][keep], len(ids))
            sn += np.bincount(k, None, len(ids))
    has = need & (sn > 0)
    x[has], y[has] = sx[has] / sn[has], sy[has] / sn[has]
    np.savez(cache, ids=ids, x=x, y=y)
    print('places: soma', int((~need).sum()), 'from synapses', int(has.sum()), 'none', int((need & ~has).sum()))
    return x, y
