import numpy as np, pandas as pd
import os
D=os.path.join(os.path.dirname(os.path.abspath(__file__)),'data')+'/'
comp = pd.read_csv(D+'Completeness_783.csv', index_col=0)
ann = pd.read_csv(D+'Supplemental_file1_neuron_annotations.tsv', sep='\t', low_memory=False).set_index('root_id').reindex(comp.index)
CT=ann.cell_type.fillna('').values; SIDE=ann.side.fillna('').values; SC=ann.super_class.fillna('').values
FAM={'escape':['DNp01','DNp02','DNp04','DNp11'], 'groom':['DNg11'], 'back':['MDN'], 'turnL':[], 'turnR':[]}
def idx(types): return np.where(np.isin(CT,types))[0]
def rates(npz, bin_s=0.01):
    z=np.load(npz); t,i,T=z['t'],z['i'],float(z['seconds']); nb=int(T/bin_s)
    out={}
    for k,ty in FAM.items():
        if not ty: continue
        ix=idx(ty); m=np.isin(i,ix)
        out[k]=np.bincount((t[m]/bin_s).astype(int),minlength=nb)[:nb]/(len(ix)*bin_s)
    dn=np.where(SC=='descending')[0]
    for s,k in (('left','turnL'),('right','turnR')):
        ix=dn[SIDE[dn]==s]; m=np.isin(i,ix); out[k]=np.bincount((t[m]/bin_s).astype(int),minlength=nb)[:nb]/(len(ix)*bin_s)
    gf=idx(['DNp01']); m=np.isin(i,gf); out['gf_spikes']=t[m]
    return out
