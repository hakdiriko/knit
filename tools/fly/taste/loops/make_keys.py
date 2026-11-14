# The round-1 loop: warm saw keys, four dub chords with rests between them (Dm7, Gm7, Bb, A7), two bars at 127 bpm, no drums.
# The dense variants (with kick, rim, hats) are left in as empty loops: on them the fly jumped once in 90 s (its eyes habituate).
# Deterministic: python3 make_keys.py writes fly-keys_127bpm.wav here.
import numpy as np, wave
sr=48000; bpm=127; beat=60/bpm; N=int(round(8*beat*sr)); rs=np.random.RandomState(7)
L=np.zeros(N); R=np.zeros(N)
def add(buf,x,at):
    i=int(at*sr)%N; idx=(i+np.arange(len(x)))%N; np.add.at(buf,idx,x)
def lp(x,fc):
    # time-varying one-pole x2
    y=np.zeros_like(x); a=1-np.exp(-2*np.pi*fc/sr); s1=s2=0.0
    for n in range(len(x)):
        s1+=a[n]*(x[n]-s1); s2+=a[n]*(s1-s2); y[n]=s2
    return y
def saw(f,t,ph): return 2*((f*t+ph)%1)-1
def stab(notes,dur,vel):
    n=int(dur*sr); t=np.arange(n)/sr; l=np.zeros(n); r=np.zeros(n)
    for m in notes:
        f=440*2**((m-69)/12)
        for det,pan in ((-7,-0.6),(0,0),(6,0.6)):
            s=saw(f*2**(det/1200),t,rs.rand()); l+=s*(1-pan)/2; r+=s*(1+pan)/2
    env=np.minimum(t/0.004,1)*np.exp(-t/ (dur*0.45))
    fc=300+2600*vel*np.exp(-t/0.12)
    return lp(l*env,fc)*vel*0.11, lp(r*env,fc)*vel*0.11
Dm7=[50,57,60,65,69]; Gm7=[55,58,62,65,70]; Bb=[46,58,62,65,69]; A7=[45,57,61,64,67]
# dub stabs on the offbeats, a held chord at the end of each bar
plan=[(0.5,Dm7,0.35,0.9),(3.25,Gm7,0.9,0.75),(4.5,Bb,0.35,0.9),(7.25,A7,0.9,0.8)]
plan_old=[(0.5,Dm7,0.35,0.9),(1.5,Dm7,0.3,0.7),(2.5,Dm7,0.35,0.85),(3.25,Gm7,0.9,0.75),
      (4.5,Bb,0.35,0.9),(5.5,Bb,0.3,0.7),(6.5,A7,0.35,0.85),(7.25,A7,0.9,0.8)]
for b,ch,d,v in plan:
    l,r=stab(ch,d*beat*2.2,v); add(L,l,b*beat); add(R,r,b*beat)
# soft kick on 1 and 3, a ghost on the "and" of 4 in bar two; rim on 2 and 4; hats on eighths
def kick(v):
    t=np.arange(int(0.28*sr))/sr; ph=2*np.pi*np.cumsum(48+110*np.exp(-t/0.03))/sr
    return v*0.75*np.sin(ph)*np.exp(-t/0.11)
def rim(v):
    t=np.arange(int(0.07*sr))/sr; return v*0.25*(np.sin(2*np.pi*1750*t)+0.5*np.sin(2*np.pi*430*t))*np.exp(-t/0.012)
def hat(v):
    n=int(0.05*sr); x=rs.randn(n); x=np.diff(x,prepend=0); return v*0.07*x*np.exp(-np.arange(n)/sr/0.012)
for b,v in ():
    k=kick(v); add(L,k,b*beat); add(R,k,b*beat)
for b in ():
    x=rim(1); add(L,x*0.8,b*beat); add(R,x,b*beat)
for i in range(0):
    x=hat(1.0 if i%2 else 0.55); add(L,x,i*beat/2); add(R,x*0.8,i*beat/2)
y=np.stack([L,R],1); y*=0.7/np.abs(y).max()
w=wave.open('fly-keys_127bpm.wav','wb'); w.setnchannels(2); w.setsampwidth(3); w.setframerate(sr)
i=(y*8388607).astype('<i4'); w.writeframes(i.view(np.uint8).reshape(-1,4)[:,:3].tobytes()); w.close()
print(N/sr)
