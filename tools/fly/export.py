"""Writes Source/dsp/FlyData.cpp: the part of the fly's brain that answers music, for the plug-in.
keep.npy picks the neurons: every neuron that fired on the test material (the reflex brain, and the
brain with an inner life: smell, restless neurons, memory), plus the senses, every MBON and DAN.
usage: python3 tools/fly/export.py keep.npy song.npz"""
import sys, os, numpy as np
sys.argv, args = ['x'], sys.argv[1:]
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import sim
ids, W, ann = sim.load(); g = sim.groups(ann)
keep = np.load(args[0]); song = np.load(args[1])
N = len(keep); pos = {n: k for k, n in enumerate(keep)}
Wk = W[keep][:, keep].tocsr(); Wk.sort_indices()
INH = getattr(sim, 'INH', 1.0)  # inhibitory synapses' extra weight (malecns.py); counts stay whole synapse counts
counts = np.rint(Wk.data / sim.WSYN / np.where(Wk.data < 0, INH, 1.0)).astype(np.int32)
assert np.abs(counts).max() < 32768 and N < 65536
ct = ann.cell_type.fillna('').values; sc = ann.super_class.fillna('').values; side = ann.side.fillna('').values
def local(full): return [pos[n] for n in full if n in pos]
senses = ['B', 'A', 'CE', 'loomlo', 'loommid', 'loomhi']
inputs = [local(g[s, sd]) for s in senses for sd in ('left', 'right')]
esc = local(np.where(np.isin(ct, ['DNp01', 'DNp02', 'DNp04', 'DNp11']))[0])
dng11 = np.where(ct == 'DNg11')[0]
act = np.bincount(song['i'], minlength=len(ids))
cc = ann.cell_class.fillna('').values; nt = ann.top_nt.fillna('').values
# smell: the projection neurons by glomerulus; glomerulus k (alphabetical) smells band k * BANDS / glomeruli
BANDS = 32
pn = np.array([k for k, c in enumerate(cc) if c == 'ALPN' and ct[k]])
glom_of = np.array([str(ct[k]).split('_')[0] for k in pn]); gloms = sorted(set(glom_of))
smell = [[] for _ in range(BANDS)]
for k, gl in enumerate(gloms): smell[k * BANDS // len(gloms)] += local(pn[glom_of == gl])
kc = local(np.where(cc == 'Kenyon_Cell')[0]); mbonG = np.array([n for n in np.where(cc == 'MBON')[0] if n in pos])
mbon = local(mbonG); danG = np.array([n for n in np.where(cc == 'DAN')[0] if n in pos]); dan = local(danG)
ppl1 = local(np.array([k for k, c in enumerate(ct) if str(c).startswith('PPL1')]))
# the rewarding dopamine neurons (PAM), for relief: the moment after pain ends is good (Tanimoto et al. 2004)
pam = local(np.array([k for k, c in enumerate(ct) if str(c).startswith('PAM')]))
# what each MBON tells the fly (Aso et al. 2014): glutamate = keep away (-1), acetylcholine or GABA = come closer (+1)
mbonSign = [-1 if nt[n] == 'glutamate' else 1 for n in mbonG]
# the plastic synapses: every KC -> MBON connection (positions in the CSR), and who they join
rows = np.repeat(np.arange(N), np.diff(Wk.indptr))
kcPos = np.full(N, -1); kcPos[kc] = np.arange(len(kc)); mbPos = np.full(N, -1); mbPos[mbon] = np.arange(len(mbon))
plastic = np.where((kcPos[rows] >= 0) & (mbPos[Wk.indices] >= 0))[0]
# dopamine reaching each MBON: DAN -> MBON synapse counts, each MBON's share summing to 1
Dm = W[danG][:, mbonG].toarray().clip(0, None); Dm /= Dm.sum(0, keepdims=True) + 1e-9
dmD, dmM = np.nonzero(Dm)
legs = local(sorted(dng11, key=lambda n: -act[n])[:2])
dn = np.where(sc == 'descending')[0]
dnL, dnR = local(dn[side[dn] == 'left']), local(dn[side[dn] == 'right'])
# the steering neurons (DNa01, DNa02: Rayshubskiy et al. 2020, Yang et al. 2023), each side
steer = {sd: local(np.where(np.isin(ct, ['DNa01', 'DNa02']) & (side == sd))[0]) for sd in ('left', 'right')}
px, py = ann.pos_x.values, ann.pos_y.values
x0, x1, y0, y1 = 22000.0, 226000.0, 12800.0, 111000.0
VOXEL_NM = 4.0  # FlyWire
if sim.FLY == 'malecns':
    import malecns
    px, py = malecns.places(ids, W)  # the soma, or where its synapses are when the soma is outside the volume
    px = -px  # the male's x runs from the fly's right to its left: mirrored, so it faces the way the FlyWire fly does
    VOXEL_NM = 8.0
    ok_ = ~np.isnan(px)
    x0, x1 = np.percentile(px[ok_], [0.05, 99.95]); y0, y1 = np.percentile(py[ok_], [0.05, 99.95])
    mx, my = 0.02 * (x1 - x0), 0.02 * (y1 - y0); x0, x1, y0, y1 = x0 - mx, x1 + mx, y0 - my, y1 + my
nx = lambda v: np.clip((v - x0) / (x1 - x0), 0, 1); ny = lambda v: np.clip((v - y0) / (y1 - y0), 0, 1)
kx, ky = nx(np.nan_to_num(px[keep], nan=(x0 + x1) / 2)), ny(np.nan_to_num(py[keep], nan=(y0 + y1) / 2))
GW, GH = 240, 116
ok = ~np.isnan(px)
grid = np.zeros((GH, GW)); np.add.at(grid, ((ny(py[ok]) * (GH - 1)).astype(int), (nx(px[ok]) * (GW - 1)).astype(int)), 1)
grid = np.rint(255 * np.sqrt(grid / grid.max())).astype(int)
def centroid(ix): return float(nx(np.nanmean(px[ix]))), float(ny(np.nanmean(py[ix])))
jo = np.array([k for k, c in enumerate(ct) if str(c).startswith('JO-')])
kcAll = np.where(cc == 'Kenyon_Cell')[0]; kcLeft = kcAll[px[kcAll] < np.nanmedian(px[kcAll])]  # one side's mushroom body
anchors = [('ears', jo), ('eyeLeft', np.where((ct == 'LPLC2') & (side == 'left'))[0]),
           ('eyeRight', np.where((ct == 'LPLC2') & (side == 'right'))[0]), ('giantFiber', np.where(ct == 'DNp01')[0]),
           ('grooming', dng11), ('mushroomBody', kcLeft)]
def arr(name, typ, vals, per=24):
    vals = list(vals); lines = []
    for k in range(0, len(vals), per): lines.append('    ' + ', '.join(str(v) for v in vals[k:k + per]) + ',')
    return f'const {typ} {name}[{max(1, len(vals))}] = {{\n' + '\n'.join(lines or ['    0']) + '\n};\n'
SOURCE = {'flywire': ['// Generated by tools/fly/export.py from the FlyWire connectome (783 release; Dorkenwald et al. 2024,',
                      '// Schlegel et al. 2024) as Shiu et al. 2024 model it. Do not edit by hand.'],
          'malecns': ['// Generated by tools/fly/export.py from Janelia\'s male CNS connectome (v1.0, Berg et al. 2026,',
                      '// CC BY 4.0; the brain only) as Shiu et al. 2024 model it. Do not edit by hand.']}[sim.FLY]
out = SOURCE + [
       '#include "FlyData.h"', '', 'namespace dendrite::flydata', '{',
       f'static_assert (neurons == {N} && synapses == {Wk.nnz});']
out.append(arr('rowStart', 'uint32_t', Wk.indptr))
out.append(arr('target', 'uint16_t', Wk.indices))
out.append(arr('weight', 'int16_t', counts))
inputs = inputs + smell
starts = np.cumsum([0] + [len(v) for v in inputs])
out.append(arr('senseStart', 'uint16_t', starts))
out.append(arr('senseNeuron', 'uint16_t', [n for v in inputs for n in v]))
out.append(arr('escapeNeuron', 'uint16_t', esc)); out.append(arr('legNeuron', 'uint16_t', legs))
out.append(arr('leftNeuron', 'uint16_t', dnL)); out.append(arr('rightNeuron', 'uint16_t', dnR))
out.append(arr('steerLeft', 'uint16_t', steer['left'])); out.append(arr('steerRight', 'uint16_t', steer['right']))
out.append(arr('kenyon', 'uint16_t', kc)); out.append(arr('mbon', 'uint16_t', mbon)); out.append(arr('dan', 'uint16_t', dan))
out.append(arr('ppl1', 'uint16_t', ppl1)); out.append(arr('pam', 'uint16_t', pam)); out.append(arr('mbonSign', 'int8_t', mbonSign))
out.append(arr('plastic', 'uint32_t', plastic)); out.append(arr('plasticKenyon', 'uint16_t', kcPos[rows[plastic]]))
out.append(arr('plasticMbon', 'uint8_t', mbPos[Wk.indices[plastic]]))
out.append(arr('dopamineDan', 'uint16_t', dmD)); out.append(arr('dopamineMbon', 'uint8_t', dmM))
out.append(arr('dopamineShare', 'float', [f'{v:.5e}f' for v in Dm[dmD, dmM]], 12))
out.append(arr('neuronX', 'uint16_t', np.rint(kx * 65535).astype(int))); out.append(arr('neuronY', 'uint16_t', np.rint(ky * 65535).astype(int)))
out.append(arr('backdrop', 'uint8_t', grid.ravel(), 40))
out.append('const Anchor anchors[anchorCount] = {\n' + '\n'.join(f'    {{ "{n}", {centroid(ix)[0]:.4f}f, {centroid(ix)[1]:.4f}f }},' for n, ix in anchors) + '\n};')
out.append('} // namespace dendrite::flydata\n')
src = os.path.join(HERE, '../../Source/flies/' + sim.FLY + '/'); os.makedirs(src, exist_ok=True)
open(src + 'FlyData.cpp', 'w').write('\n'.join(out))
ATLAS = {'flywire': 'the FlyWire connectome', 'malecns': "Janelia's male CNS connectome (the brain)"}[sim.FLY]
h = f'''// The fly's brain, the part that answers music: {N} neurons and {Wk.nnz} connections cut from
// {ATLAS} (every neuron that fired while the whole brain heard the test material, with and
// without its inner life, plus the senses and the mushroom body's output and dopamine neurons; silent
// neurons change nothing). Generated by tools/fly/export.py.
#pragma once

#include <cstdint>

namespace dendrite::flydata
{{
constexpr int neurons = {N}, synapses = {Wk.nnz};
constexpr int senses = 12; // B, A, CE, loom low, loom mid, loom high; each left then right
constexpr int smells = {BANDS};  // then the smell bands, low to high, each a few glomeruli's projection neurons
// the memory: the mushroom body's Kenyon cells, its output neurons (MBONs) and the dopamine neurons that teach them
constexpr int kenyonCount = {len(kc)}, mbonCount = {len(mbon)}, danCount = {len(dan)}, ppl1Count = {len(ppl1)}, pamCount = {len(pam)};
constexpr int plasticCount = {len(plastic)}, dopamineCount = {len(dmD)};
constexpr int escapeCount = {len(esc)}, legCount = {len(legs)}, leftCount = {len(dnL)}, rightCount = {len(dnR)}, steerCount = {len(steer['left'])};
// rates are counted over these, as over the whole brain: every DNg11, and every descending neuron per side
constexpr int groomAll = {len(dng11)}, leftAll = {int((side[dn] == 'left').sum())}, rightAll = {int((side[dn] == 'right').sum())};
constexpr int backdropWidth = {GW}, backdropHeight = {GH}; // the whole brain from the front, 0..255 density
constexpr float aspect = {(x1 - x0) / (y1 - y0):.4f}f;  // width / height of the brain's box
struct Anchor {{ const char* name; float x, y; }};
constexpr int anchorCount = {len(anchors)};

extern const uint32_t rowStart[neurons + 1];
extern const uint16_t target[synapses];
extern const int16_t weight[synapses]; // synapse count, negative for inhibitory
extern const uint16_t senseStart[senses + smells + 1];
extern const uint16_t senseNeuron[];
extern const uint16_t escapeNeuron[escapeCount], legNeuron[legCount], leftNeuron[leftCount], rightNeuron[rightCount];
extern const uint16_t steerLeft[steerCount], steerRight[steerCount]; // DNa01 and DNa02, each side: they turn the fly
extern const uint16_t kenyon[kenyonCount], mbon[mbonCount], dan[danCount], ppl1[ppl1Count], pam[pamCount];
extern const int8_t mbonSign[mbonCount]; // -1 keep away (glutamate), +1 come closer (acetylcholine, GABA)
extern const uint32_t plastic[plasticCount];  // KC -> MBON synapses, as positions in target/weight
extern const uint16_t plasticKenyon[plasticCount]; // which Kenyon cell (index into kenyon)
extern const uint8_t plasticMbon[plasticCount];    // which MBON (index into mbon)
extern const uint16_t dopamineDan[dopamineCount];  // DAN -> MBON: index into dan,
extern const uint8_t dopamineMbon[dopamineCount];  // index into mbon,
extern const float dopamineShare[dopamineCount];   // and its share of that MBON's dopamine
extern const uint16_t neuronX[neurons], neuronY[neurons]; // 0..65535 across the brain's box
extern const uint8_t backdrop[backdropWidth * backdropHeight];
extern const Anchor anchors[anchorCount];
}} // namespace dendrite::flydata
'''
open(src + 'FlyData.h', 'w').write(h)
print('kc', len(kc), 'mbon', len(mbon), 'dan', len(dan), 'ppl1', len(ppl1), 'pam', len(pam), 'plastic', len(plastic), 'dopamine', len(dmD), 'smell', [len(v) for v in smell])
print('neurons', N, 'synapses', Wk.nnz, 'senses', [len(v) for v in inputs], 'escape', len(esc), 'legs', legs, 'L/R', len(dnL), len(dnR))

# the atlas facts the model and the window need (Atlas.h), and for the male fly who each neuron is
# (Identities: FlyWire's are matched and proven by tools/rig/identities.py instead)
order = [(s, sd) for s in senses for sd in ('left', 'right')]
gain = [sim.GAIN.get(k, 1.0) for k in order]
READ = sim.READOUT
facts = {'flywire': ('adult female', 'flywire 783', 'FlyWire'), 'malecns': ('adult male', 'male cns v1.0', 'Janelia')}[sim.FLY]
atlas = f'''// The {sim.FLY} fly's atlas facts. Generated by tools/fly/export.py.
#pragma once

namespace dendrite::flydata
{{
// what one synapse is worth (Shiu et al. 2024: 0.275 mV per FlyWire synapse)
constexpr float synapseMv = {sim.WSYN:.9g}f;
// and the extra weight of an inhibitory synapse (GABA, glutamate), which holds the brain's balance of
// excitation and inhibition where the FlyWire model was validated (tools/fly/malecns.py)
constexpr float inhibitoryScale = {INH:.9g}f;
// each sense group's input rate scale (B, A, CE, loom low, mid, high; each left then right), so the music
// reaches this brain as hard as it reached the FlyWire fly (tools/fly/malecns.py)
constexpr float senseGain[12] = {{ {', '.join(f'{v:.6f}f' for v in gain)} }};
// the readouts, as the reflexes read them: the grooming neurons' gain, the steering neurons' gain, the
// descending neurons' scale for the excitement and the MBONs' resting feeling, so each means what it
// meant for the FlyWire fly the reflexes were tuned on (tools/fly/malecns.py)
constexpr float groomGain = {READ['groomGain']:.6f}f, leanGain = {READ['leanGain']:.6f}f, excitedScale = {READ['excitedScale']:.6f}f, feelingRest = {READ['feelingRest']:.6f}f;
// the cut's box, front view
constexpr float widthMicrons = {(x1 - x0) * VOXEL_NM / 1000:.1f}f, heightMicrons = {(y1 - y0) * VOXEL_NM / 1000:.1f}f;
// for the window: what the specimen is and where its neurons are named
constexpr const char* specimenSex = "{facts[0]}";
constexpr const char* atlasName = "{facts[1]}";
constexpr const char* idSource = "{facts[2]}";
}} // namespace dendrite::flydata
'''
open(src + 'Atlas.h', 'w').write(atlas)
if sim.FLY == 'malecns':
    def text(col):
        return [str(v) if isinstance(v, str) else '' for v in ann[col].values[keep]]
    types, nts, classes, sides = text('cell_type'), text('top_nt'), text('super_class'), text('side')
    def table(values):
        names = sorted(set(values)); at = {v: i for i, v in enumerate(names)}
        return names, [at[v] for v in values]
    (tN, tI), (nN, nI), (cN, cI), (sN, sI) = table(types), table(nts), table(classes), table(sides)
    def strings(name, vs):
        return 'const char* const %s[%d] = {\n%s\n};\n' % (name, len(vs), '\n'.join('    "%s",' % v.replace('"', '') for v in vs))
    ih = f'''// Who each of the cut's neurons is in Janelia's male CNS v1.0 (generated by tools/fly/export.py).
// Indexed like FlyData: neuron n of the plug-in is body rootId[n] in the male CNS.
#pragma once

#include <cstdint>

namespace dendrite::rig::identities
{{
constexpr int count = {N};
extern const uint64_t rootId[count];     // the male CNS body id
extern const uint16_t type[count];       // into typeName ("" when it has none)
extern const uint8_t transmitter[count]; // into transmitterName (the consensus transmitter)
extern const uint8_t superClass[count];  // into className
extern const uint8_t side[count];        // into sideName
extern const char* const typeName[];
extern const char* const transmitterName[];
extern const char* const className[];
extern const char* const sideName[];
}} // namespace dendrite::rig::identities
'''
    ic = ['// Generated by tools/fly/export.py from Janelia\'s male CNS v1.0 (Berg et al. 2026, CC BY 4.0). Do not edit.',
          '#include "Identities.h"', '', 'namespace dendrite::rig::identities', '{', 'static_assert (count == %d);' % N,
          arr('rootId', 'uint64_t', [int(ids[k]) for k in keep], 8),
          strings('typeName', tN), arr('type', 'uint16_t', tI), strings('transmitterName', nN), arr('transmitter', 'uint8_t', nI),
          strings('className', cN), arr('superClass', 'uint8_t', cI), strings('sideName', sN), arr('side', 'uint8_t', sI),
          '} // namespace dendrite::rig::identities', '']
    open(src + 'Identities.h', 'w').write(ih); open(src + 'Identities.cpp', 'w').write('\n'.join(ic))
    print('identities:', len(tN), 'types,', nN, cN, sN)
print('atlas: synapse', sim.WSYN, 'gain', [round(v, 3) for v in gain], 'box um', (x1 - x0) * VOXEL_NM / 1000, (y1 - y0) * VOXEL_NM / 1000)
