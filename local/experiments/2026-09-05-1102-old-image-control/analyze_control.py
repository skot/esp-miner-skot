from pathlib import Path
import json,numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=Path(__file__).resolve().parent;rows=[json.loads(l) for l in (p/'api-samples.jsonl').read_text().splitlines()];reboot=json.loads((p/'restart-command.json').read_text())['restart_response_at'];rows=[r for r in rows if len(r['info'].get('asicTemps',[]))==4]
x=np.array([r['host_monotonic']-reboot for r in rows]);temps=np.array([r['info']['asicTemps'] for r in rows]);rates=np.array([r['info']['hashRate']/1000 for r in rows])
fig,ax=plt.subplots(2,1,figsize=(10,6),sharex=True,layout='constrained')
for c in range(4):ax[0].plot(x,temps[:,c],label=f'A{c+1}')
ax[1].plot(x,rates,label='Reported total',color='#007c91');ax[1].axhline(2.9952,color='gray',ls='--',label='600 MHz ideal')
for a in ax:a.axvline(0,color='black',ls=':',label='Warm ESP reboot');a.grid(alpha=.2);a.legend(fontsize=8)
ax[0].set_ylabel('Temperature (°C)');ax[1].set_ylabel('TH/s');ax[1].set_xlabel('Seconds relative to warm ESP reboot request');fig.suptitle('Exact older 1102 firmware: warm reboot restores ~2.97 TH/s\nAll four temperature readings continue updating')
for ext in ['png','pdf']:fig.savefig(p/f'old-firmware-recovery.{ext}',dpi=150)
post=[r for r in rows if r['host_monotonic']>reboot and r['info'].get('uptimeSeconds',1000)<170]
stats={'post_samples':len(post),'first_post':post[0]['info']['asicTemps'],'last_post':post[-1]['info']['asicTemps'],'min_post':np.min([r['info']['asicTemps'] for r in post],axis=0).tolist(),'max_post':np.max([r['info']['asicTemps'] for r in post],axis=0).tolist(),'unique_temperatures_per_asic':[len(set(r['info']['asicTemps'][c] for r in post)) for c in range(4)]}
f=p/'warm-reboot';raw=np.load(f/'raw.npz');pre={k:np.array(v.split(','),float) for k,v in json.loads((f/'preambles.json').read_text()).items()};t=np.arange(700000)*pre['CHAN1'][4]+pre['CHAN1'][5];v={c:(raw[c].astype(float)-pp[8]-pp[9])*pp[7] for c,pp in pre.items()};v['upper']=v['CHAN1']-v['CHAN2']
stats['scope_medians']={name:{c:float(np.median(a[(t>lo)&(t<hi)])) for c,a in v.items()} for name,lo,hi in [('before',-8,-2),('after',50,59)]};(p/'analysis-summary.json').write_text(json.dumps(stats,indent=2));print(json.dumps(stats,indent=2))
fig,ax=plt.subplots(3,1,figsize=(10,7),sharex=True,layout='constrained');n=100;idx=np.arange(700000).reshape(-1,n);xx=t[idx].mean(1)
for a,c in zip(ax,['CHAN1','CHAN2','upper']):
 y=v[c][idx];a.fill_between(xx,y.min(1),y.max(1),alpha=.35);a.plot(xx,np.median(y,axis=1),lw=.7,label=c);a.grid(alpha=.2);a.set_ylabel('V');a.legend()
ax[-1].set_xlabel('Seconds relative to forced scope trigger');fig.suptitle('Old firmware warm ESP reboot: ASIC rail cycles before successful 600 MHz work\n100 µs sampling, uncorrected offsets; CH1 is 50 Ω')
for ext in ['png','pdf']:fig.savefig(f/f'waveform.{ext}',dpi=150)
