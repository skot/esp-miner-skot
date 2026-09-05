"""Compare baseline-referenced output movement; align on the CH2 averaged peak."""
from pathlib import Path
import json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=Path(__file__).resolve().parent
out={};fig,axes=plt.subplots(2,1,figsize=(10,6),sharex=True,layout='constrained')
for name,folder,label,color in [
 ('prior',p.parent/'2026-09-04-warm-ramp-tolerant'/'step-325','Prior CH1 setup (termination not recorded)','#ba671e'),
 ('tps_local_50ohm',p/'step-325','TPS output caps, CH1 50 Ω','#087c90')]:
 r=np.load(folder/'raw.npz');pr={k:np.array(v.split(','),float) for k,v in json.loads((folder/'preambles.json').read_text()).items()}
 n=round(20e-6/pr['CHAN1'][4]);v={k:(r[k].astype(float)-a[8]-a[9])*a[7] for k,a in pr.items()}
 y={k:np.convolve(a,np.ones(n)/n,'valid') for k,a in v.items()}
 t=(np.arange(len(y['CHAN1']))+(n-1)/2)*pr['CHAN1'][4]+pr['CHAN1'][5]
 base={k:float(a[(t>0)&(t<.008)].mean()) for k,a in y.items()};origin=t[np.argmax(y['CHAN2'])];relative=(t-origin)*1000;mask=(relative>-.3)&(relative<3.2)
 out[name]=dict(baseline_total_v=base['CHAN1'],raw_total_min_v=float(v['CHAN1'].min()),raw_total_max_v=float(v['CHAN1'].max()),mean_20us_total_min_v=float(y['CHAN1'].min()),mean_20us_total_max_v=float(y['CHAN1'].max()),mean_overshoot_mv=float((y['CHAN1'].max()-base['CHAN1'])*1000),mean_undershoot_mv=float((y['CHAN1'].min()-base['CHAN1'])*1000),alignment_time_ms=float(origin*1000))
 for ax,k in zip(axes,['CHAN1','CHAN2']):ax.plot(relative[mask],(y[k][mask]-base[k])*1000,color=color,label=label)
for ax,title in zip(axes,['Total supply change (CH1)','Lower-half change (CH2)']):
 ax.set_ylabel(title+'\n(mV)');ax.axhline(0,color='gray',lw=.7);ax.grid(alpha=.2);ax.legend(fontsize=8)
axes[-1].set_xlabel('Milliseconds relative to lower-half 20 µs averaged peak')
fig.suptitle('Warm 300 → 325 MHz at 1140 mV\n20 µs moving averages; baseline removed independently for each capture')
for ext in ['png','pdf']:fig.savefig(p/f'tps-comparison.{ext}',dpi=150)
(p/'tps-comparison.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps(out,indent=2))
