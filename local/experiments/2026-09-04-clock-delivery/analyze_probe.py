"""Analyze passive global-counter snapshots and the matched Rigol capture."""
import json
import re
from pathlib import Path

p = Path(__file__).resolve().parent
log = (p / 'transition-serial.log').read_text()
pattern = re.compile(r'CLOCK_PROBE SAMPLE stage=(\S+) A(\d) CFG=(0x\w+)->(0x\w+) EN=(0x\w+)->(0x\w+) pass=(\d+)->(\d+) fail=(\d+)->(\d+) sample_us=(\d+)->(\d+) GHs=([\d.]+)')
rows = []
for match in pattern.finditer(log):
    stage, chip, cfg0, cfg1, en0, en1, pa, pb, fa, fb, ta, tb, _ = match.groups()
    pa, pb, fa, fb, ta, tb = map(int, (pa, pb, fa, fb, ta, tb))
    assert tb > ta and pb >= pa and fb >= fa, 'Counter reset/wrap or invalid timing'
    rate = (pb - pa) * 16777.216 / (tb - ta)
    total = ((pb-pa)+(fb-fa)) * 16777.216 / (tb-ta)
    rows.append(dict(stage=stage, asic=int(chip), cfg_before=cfg0, cfg_after=cfg1,
        en_before=en0, en_after=en1, pass_delta=pb-pa, fail_delta=fb-fa,
        sample_start_us=ta, sample_end_us=tb, interval_ms=(tb-ta)/1000,
        pass_ghs=rate, total_ghs=total))
assert len(rows) == 16 and len({(r['stage'], r['asic']) for r in rows}) == 16, 'Incomplete or duplicate probe stages'
(p/'counter-results.json').write_text(json.dumps(rows, indent=2)+'\n')
for row in rows:
    print(f"{row['stage']:20} A{row['asic']} EN {row['en_before']}->{row['en_after']} pass {row['pass_delta']:4} fail {row['fail_delta']:3} {row['interval_ms']:.3f}ms {row['pass_ghs']:.3f}GH/s")

if not (p/'transition-raw.npz').exists():
    raise SystemExit('Counters analyzed; scope download not yet present')
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
raw=np.load(p/'transition-raw.npz')
pre={c:np.array(v.split(','),dtype=float) for c,v in json.loads((p/'transition-preambles.json').read_text()).items()}
assert np.array_equal(pre['CHAN1'][4:7], pre['CHAN2'][4:7])
assert len(raw['CHAN1'])==len(raw['CHAN2'])
t=(np.arange(len(raw['CHAN1']))-pre['CHAN1'][6])*pre['CHAN1'][4]+pre['CHAN1'][5]
v={c:(raw[c].astype(float)-pre[c][8]-pre[c][9])*pre[c][7] for c in pre}
valid={c:(raw[c]>0)&(raw[c]<255) for c in pre}
v['upper']=v['CHAN1']-v['CHAN2']; valid['upper']=valid['CHAN1']&valid['CHAN2']
summary=dict(points=len(t), sample_interval_s=float(pre['CHAN1'][4]),
    time_start_s=float(t[0]),time_end_s=float(t[-1]),
    note='Uncorrected scope voltages. Saturated samples excluded. Full record includes deliberate shutdown.')
for c in v:
    mask=valid[c]&(t>-.03)
    summary[c]=dict(min_valid_v=float(v[c][mask].min()),max_valid_v=float(v[c][mask].max()),
        baseline_v=float(np.median(v[c][valid[c]&(t<-.03)])))
(p/'scope-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
fig,axes=plt.subplots(3,1,figsize=(11,8),sharex=True,layout='constrained')
# Min/max envelopes retain transients when plotting the complete acquisition.
ix=np.flatnonzero((t>-.03)&(t<1.2)); width=max(1,len(ix)//7000); ix=ix[:len(ix)//width*width].reshape(-1,width)
xt=t[ix].mean(axis=1)*1000
for ax,c,label,color in zip(axes,['CHAN1','CHAN2','upper'],['Total VDD (CH1)','Lower half-stack (CH2)','Upper half-stack (CH1 − CH2)'],['#a97600','#008ea1','#903bb0']):
    y=np.where(valid[c][ix],v[c][ix],np.nan);good=np.any(np.isfinite(y),axis=1)
    low=np.full(len(y),np.nan);high=low.copy();mid=low.copy()
    low[good]=np.nanmin(y[good],axis=1);high[good]=np.nanmax(y[good],axis=1);mid[good]=np.nanmedian(y[good],axis=1)
    ax.fill_between(xt,low,high,color=color,alpha=.35)
    ax.plot(xt,mid,color=color,lw=.9,label=label);ax.legend(loc='upper right');ax.grid(alpha=.2);ax.set_ylabel('Volts')
    if c!='CHAN1':ax.axhline(1.2,color='#bb3333',ls='--',lw=1)
axes[-1].set_xlabel('Time relative to host-forced scope trigger (ms)\nClock delivery inferred from passive hash-counter increments, not a measured clock pin.')
axes[-1].set_xlim(-30,1200)
fig.suptitle('MC3 clock-delivery probe • 100 → 125 MHz at 960 mV per half-stack\nLive EN=1, restart into EN=1, then EN=3; deliberate power-off at end',fontsize=13)
for ext in ['png','pdf']:fig.savefig(p/f'clock-delivery.{ext}',dpi=160)
