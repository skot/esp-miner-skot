from pathlib import Path
import json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=Path(__file__).resolve().parent
rows=json.loads((p/'domain-samples.json').read_text());summary=[]
for freq in sorted(set(r['frequency_mhz'] for r in rows)):
 selected=[r for r in rows if r['frequency_mhz']==freq and r['phase']==f'warm{int(freq)}']
 if not selected:continue
 last=max(r['boot_ms'] for r in selected)
 for window,lo in [('whole_phase',0),('last30s',last-30000)]:
  for chip in range(1,5):
   for domain in range(4):
    a=[r for r in selected if r['asic']==chip and r['domain']==domain and r['dt_us'] and r['boot_ms']>=lo]
    if not a:continue
    dt=sum(r['dt_us'] for r in a);dp=sum(r['delta_pass'] for r in a);df=sum(r['delta_fail'] for r in a)
    summary.append(dict(frequency=freq,window=window,asic=chip,domain=domain,seconds=dt/1e6,pass_percent=dp*16777.216/dt/(freq*1248/4000)*100,fail_fraction_percent=100*df/(dp+df) if dp+df else None,max_zero_cores=max(r['zero_pass_cores'] for r in a),mean_temperature=sum(r['temperature_c'] for r in a)/len(a)))
(p/'rate-windows.json').write_text(json.dumps(summary,indent=2)+'\n')
scopes=[]
for folder in sorted(p.glob('step-*'),key=lambda f:int(f.name.split('-')[1])):
 a=json.loads((folder/'audit.json').read_text());a['frequency']=int(folder.name.split('-')[1]);scopes.append(a)
(p/'scope-summary.json').write_text(json.dumps(scopes,indent=2)+'\n')
if scopes:
 fig,axes=plt.subplots(2,1,figsize=(10,7),layout='constrained')
 for key,label in [('lower_max_moving_mean_v','Lower half'),('upper_max_moving_mean_v','Upper estimate')]:axes[0].plot([a['frequency'] for a in scopes],[a[key] for a in scopes],'-o',label=label)
 axes[0].axhline(1.22,color='gray',ls='--',label='Previous 20 µs limit (not enforced)');axes[0].set_ylabel('Peak sliding 20 µs voltage (V)')
 for c in range(1,5):
  a=[r for r in summary if r['domain']==2 and r['asic']==c and r['window']=='whole_phase']
  axes[1].plot([r['frequency'] for r in a],[r['pass_percent'] for r in a],'-o',label=f'A{c} D2')
 axes[1].set_ylabel('D2 passing throughput (% ideal)');axes[1].axhline(100,color='gray',ls='--')
 for ax in axes:ax.grid(alpha=.2);ax.set_xlabel('PLL frequency (MHz)');ax.legend(fontsize=8)
 fig.suptitle('1102: voltage and throughput report-only\nCH1 TPS output / 50 Ω; CH2 at 0.5 V/div; unequal observation durations')
 for ext in ['png','pdf']:fig.savefig(p/f'ramp-summary.{ext}',dpi=150)
print('Final frequency',max(r['frequency_mhz'] for r in rows))
print(json.dumps([r for r in summary if r['frequency']==600 and r['window']=='last30s'],indent=2))
