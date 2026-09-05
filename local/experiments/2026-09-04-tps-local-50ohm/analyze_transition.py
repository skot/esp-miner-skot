"""Zoom the warm 300→325 MHz event and retain duration estimates."""
from pathlib import Path
import json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=Path(__file__).resolve().parent/'step-325'
r=np.load(p/'raw.npz');pre={k:np.array(v.split(','),float) for k,v in json.loads((p/'preambles.json').read_text()).items()}
v={k:(r[k].astype(float)-a[8]-a[9])*a[7] for k,a in pre.items()}
v['lower']=v['CHAN2'];v['upper']=v['CHAN1']-v['CHAN2']
t=np.arange(len(v['lower']))*pre['CHAN1'][4]+pre['CHAN1'][5]
n=round(20e-6/pre['CHAN1'][4]);size=len(t)//n*n;tt=t[:size].reshape(-1,n).mean(1)
result={};fig,axes=plt.subplots(3,1,figsize=(10,8),sharex=True,layout='constrained')
for ax,key,label in zip(axes,['CHAN1','lower','upper'],['TPS output caps','Lower half-stack','CH1 minus CH2 estimate']):
 x=v[key];means=x[:size].reshape(-1,n).mean(1)
 mask=(t>.008)&(t<.016);m=(tt>.008)&(tt<.016)
 ax.plot(t[mask]*1e3,x[mask],alpha=.35,lw=.6,label='Raw samples')
 ax.plot(tt[m]*1e3,means[m],color='black',lw=1,label='20 µs block mean')
 ax.set_ylabel(label+' (V)');ax.grid(alpha=.2)
 if key!='CHAN1':
  ax.axhline(1.25,color='#be3a34',ls='--',label='Raw limit 1.25 V')
  ax.axhline(1.22,color='#c18b32',ls=':',label='Moving-mean limit 1.22 V')
  result[key]={}
  for limit in [1.2,1.22,1.25,1.3]:
   above=means>=limit;edges=np.flatnonzero(np.diff(np.r_[False,above,False]))
   result[key][str(limit)]=dict(total_block_duration_us=float(above.sum()*n*pre['CHAN1'][4]*1e6),runs=[dict(first_block_center_ms=float(tt[a]*1e3),duration_us=float((b-a)*n*pre['CHAN1'][4]*1e6)) for a,b in zip(edges[::2],edges[1::2])])
 ax.legend(fontsize=8,loc='upper right')
axes[-1].set_xlabel('Milliseconds relative to forced scope trigger')
fig.suptitle('Warm 300 → 325 MHz, CH1 at TPS output caps / 50 ohms\n2 MSa/s, 20 MHz bandwidth; uncorrected scope volts')
for ext in ['png','pdf']:fig.savefig(p/f'transition-zoom.{ext}',dpi=150)
(p/'durations.json').write_text(json.dumps(result,indent=2)+'\n')
