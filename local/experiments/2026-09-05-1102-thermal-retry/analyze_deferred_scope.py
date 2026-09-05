from pathlib import Path
import re,json,numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=Path(__file__).resolve().parent;f=p/'deferred-start';s=(p/'serial.log').read_text()
raw=np.load(f/'raw.npz');pre={c:np.array(x.split(','),float) for c,x in json.loads((f/'preambles.json').read_text()).items()}
ref=int(re.search(r'\((\d+)\).*DEFER TRIAL',s)[1]);stop=int(re.search(r'\((\d+)\).*WARM END',s)[1])
t=np.arange(len(raw['CHAN1']))*pre['CHAN1'][4]+pre['CHAN1'][5]
v={c:(raw[c].astype(float)-pre[c][8]-pre[c][9])*pre[c][7] for c in pre};v['upper']=v['CHAN1']-v['CHAN2']
clip=(raw['CHAN1']==0)|(raw['CHAN1']==255)|(raw['CHAN2']==0)|(raw['CHAN2']==255)
# Estimate the SCPI/serial trigger offset from the logged GPIO shutdown and
# observed rail fall. This is event alignment, not a hardware trigger timestamp.
off_log=int(re.findall(r'\((\d+)\).*Holding active-high ASIC power enable low',s)[-1])
off_edge=float(t[np.flatnonzero((t>10)&(v['CHAN1']<2.1))[0]])
shift=off_edge-(off_log-ref)/1000
stop_relative=(stop-ref)/1000+shift
locks=[dict(time_s=(int(a)-ref)/1000+shift,mhz=int(b)) for a,b in re.findall(r'\((\d+)\).*DEFER PLL LOCKED target=(\d+)',s)]
summary={'sample_interval_s':float(pre['CHAN1'][4]),'shutdown_log_relative_s':stop_relative,'scope_serial_offset_s':shift,'alignment':'estimated from GPIO power-off log and observed rail fall; not hardware-synchronized','first_clipped_s':float(t[np.flatnonzero(clip)[0]]),'locks':locks,'windows':{}}
for name,lo,hi in [('powered_before_shutdown',-.1,off_edge-.05),('550_transition',locks[-1]['time_s']-.07,locks[-1]['time_s']+.15),('550_settled_before_stop',locks[-1]['time_s']+.3,off_edge-.05)]:
 mask=(t>=lo)&(t<=hi);valid=mask&~clip
 summary['windows'][name]={'start_s':lo,'end_s':hi,'clipped':int(clip[mask].sum()),'values':{c:{'min':float(x[valid].min()),'max':float(x[valid].max()),'median':float(np.median(x[valid]))} for c,x in v.items()}}
(f/'event-analysis.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
fig,axes=plt.subplots(3,1,figsize=(11,8),sharex=True,layout='constrained')
mask=(t>=-.2)&(t<summary['shutdown_log_relative_s']+.2)
idx=np.flatnonzero(mask);n=20;idx=idx[:len(idx)//n*n].reshape(-1,n);xx=t[idx].mean(1)
for ax,c,label in zip(axes,['CHAN1','CHAN2','upper'],['TPS output (CH1, 50 Ω)','GND0 to GND (CH2)','CH1 − CH2 (upper-half estimate)']):
 y=np.where(~clip,v[c],np.nan)[idx]
 # Exclude groups entirely after the range falls below the CH1 display window.
 keep=np.any(np.isfinite(y),axis=1)
 ax.fill_between(xx[keep],np.nanmin(y[keep],axis=1),np.nanmax(y[keep],axis=1),alpha=.35)
 ax.plot(xx[keep],np.nanmedian(y[keep],axis=1),lw=.6,label=label)
 for event in locks:ax.axvline(event['time_s'],color='gray',alpha=.3,lw=.6)
 ax.axvline(locks[-1]['time_s'],color='red',ls='--',label='550 MHz lock')
 ax.axvline((328088-ref)/1000+shift,color='orange',ls=':',label='First logged A3 timeout / raw zero')
 ax.axvline(off_edge,color='black',ls=':',label='Power off')
 valid_idx=np.flatnonzero(mask&~clip&(t<off_edge-.05))
 for extreme in [np.argmin(v[c][valid_idx]),np.argmax(v[c][valid_idx])]:
  j=valid_idx[extreme];ax.plot(t[j],v[c][j],'o',ms=4,color='#0072B2')
 ax.set_ylabel('V');ax.grid(alpha=.2);ax.legend(fontsize=7,loc='best')
axes[-1].set_xlabel('Seconds relative to forced trigger (serial events aligned using shutdown; approximate)')
fig.suptitle('1102: warm reset and no-work ramp stopped at 550 MHz\n50 µs sampling; uncorrected probe offsets; post-shutdown CH1 clipping excluded')
for ext in ['png','pdf']:fig.savefig(f/f'annotated-waveform.{ext}',dpi=150)
