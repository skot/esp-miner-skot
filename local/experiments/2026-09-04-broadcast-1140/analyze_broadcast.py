"""Passive domain-counter rates and calibrated-by-preamble scope decoding."""
import json,re,csv
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=Path(__file__).resolve().parent
log=(p/'transition-serial.log').read_text()
pattern=re.compile(r'BCAST SAMPLE phase=(\w+) A(\d) counter_us=(\d+) dt_us=(\d+) pass=([\d,]+) rate=([\d.,-]+) zero=([\d,]+) D2_mV=([\d.-]+)')
rows=[];prev={};summary={}
for m in pattern.finditer(log):
    phase,chip,t,dt,counts,rates,zeros,d2=m.groups();chip=int(chip);t=int(t);dt=int(dt)
    counts=list(map(int,counts.split(',')));rates=list(map(float,rates.split(',')));zeros=list(map(int,zeros.split(',')))
    assert len(counts)==len(rates)==len(zeros)==4
    prior=prev.get(chip)
    for d in range(4):
        delta=counts[d]-prior['counts'][d] if prior else None
        if prior:
            assert dt==t-prior['t'] and dt>0 and delta>=0
            calc=delta*16777.216/dt/124.8*100
            assert abs(calc-rates[d])<.011,(calc,rates[d])
        row=dict(phase=phase,asic=chip,domain=d,counter_us=t,dt_us=dt,passed=counts[d],delta_pass=delta,
            rate_percent=rates[d] if dt else None,zero_cores_cumulative=zeros[d],d2_mv=float(d2) if chip<3 else None)
        rows.append(row)
        # Exclude first interval of each phase, which straddles its intervention.
        if prior and prior['phase']==phase:
            key=(phase,chip,d)
            a=summary.setdefault(key,dict(delta_pass=0,dt_us=0,samples=0))
            a['delta_pass']+=delta;a['dt_us']+=dt;a['samples']+=1
    prev[chip]=dict(t=t,counts=counts,phase=phase)
(p/'domain-samples.json').write_text(json.dumps(rows,indent=2)+'\n')
if rows:
    with (p/'domain-samples.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=rows[0]);writer.writeheader();writer.writerows(rows)
phase_summary=[]
for (phase,chip,d),a in summary.items():
    r=dict(phase=phase,asic=chip,domain=d,**a,percent=a['delta_pass']*16777.216/a['dt_us']/124.8*100)
    phase_summary.append(r)
    if d==2:print(phase,'A'+str(chip),'D2',round(r['percent'],3),'percent',round(a['dt_us']/1e6,3),'s')
(p/'phase-summary.json').write_text(json.dumps(phase_summary,indent=2)+'\n')
if rows:
    origin=min(r['counter_us'] for r in rows)
    recovery=[]
    for chip in [1,2]:
        samples=[r for r in rows if r['asic']==chip and r['domain']==2]
        first=next((i for i,r in enumerate(samples) if r['passed']>0),None)
        if first is not None:
            # Skip both the first nonzero interval and the following interval
            # when calculating the conservative fully recovered tail.
            tail=samples[first+2:]
            recovery.append(dict(asic=chip,
                last_zero_s=(samples[first-1]['counter_us']-origin)/1e6 if first else None,
                first_passing_s=(samples[first]['counter_us']-origin)/1e6,
                tail_seconds=sum(r['dt_us'] for r in tail)/1e6,
                tail_percent=sum(r['delta_pass'] for r in tail)*16777.216/sum(r['dt_us'] for r in tail)/124.8*100 if tail else None))
    (p/'recovery-summary.json').write_text(json.dumps(recovery,indent=2)+'\n')
    fig,axes=plt.subplots(2,1,figsize=(10,6),sharex=True,layout='constrained')
    for chip,color in [(1,'#007c91'),(2,'#ad5100'),(3,'#777777'),(4,'#999999')]:
        a=[r for r in rows if r['asic']==chip and r['domain']==2 and r['dt_us']]
        t=[(r['counter_us']-origin)/1e6 for r in a]
        axes[0].plot(t,[r['rate_percent'] for r in a],'.-',color=color,label=f'A{chip} D2',alpha=1 if chip<3 else .5)
        if chip<3:axes[1].plot(t,[r['d2_mv'] for r in a],'.-',color=color,label=f'A{chip} D2')
    for phase,t in re.findall(r'BCAST ACTION phase=(\w+) call_us=(\d+)',log):
        x=(int(t)-origin)/1e6
        for ax in axes:ax.axvline(x,color='black',ls=':',lw=1)
        axes[0].text(x,110,phase.replace('_',' '),rotation=90,va='top',ha='right',fontsize=8)
    axes[0].axhline(100,color='gray',ls='--',lw=.8);axes[0].set_ylabel('% ideal domain throughput');axes[0].set_ylim(-3,115)
    axes[1].set_ylabel('D2 voltage (mV)');axes[1].set_xlabel('Seconds since first A1 counter snapshot')
    for ax in axes:ax.grid(alpha=.2);ax.legend(loc='best')
    fig.suptitle('Broadcast 375→400 MHz at 1.140 V per half-stack\nOne counter arm; subsequent samples are passive',fontsize=13)
    for ext in ['png','pdf']:fig.savefig(p/f'd2-timeline.{ext}',dpi=150)
    plt.close(fig)

if not (p/'transition-raw.npz').exists():raise SystemExit('No raw scope capture yet')
raw=np.load(p/'transition-raw.npz');pre={c:np.array(v.split(','),dtype=float) for c,v in json.loads((p/'transition-preambles.json').read_text()).items()}
assert np.array_equal(pre['CHAN1'][4:7],pre['CHAN2'][4:7])
t=(np.arange(len(raw['CHAN1']))-pre['CHAN1'][6])*pre['CHAN1'][4]+pre['CHAN1'][5]
v={c:(raw[c].astype(float)-pre[c][8]-pre[c][9])*pre[c][7] for c in pre}
valid={c:(raw[c]>0)&(raw[c]<255) for c in pre};v['upper']=v['CHAN1']-v['CHAN2'];valid['upper']=valid['CHAN1']&valid['CHAN2']
ss=dict(points=len(t),sample_interval_s=float(pre['CHAN1'][4]),time_start_s=float(t[0]),time_end_s=float(t[-1]),note='Uncorrected scope voltages; clipped samples excluded.')
for c in v:
    ss[c]=dict(min_v=float(v[c][valid[c]].min()),max_v=float(v[c][valid[c]].max()),baseline_v=float(np.median(v[c][valid[c]&(t<0)])))
(p/'scope-summary.json').write_text(json.dumps(ss,indent=2)+'\n');print(json.dumps(ss,indent=2))
fig,axes=plt.subplots(3,1,figsize=(10,7),sharex=True,layout='constrained')
width=max(1,len(t)//9000);ix=np.arange(len(t)//width*width).reshape(-1,width);xt=t[ix].mean(axis=1)*1000
for ax,c,label,color in zip(axes,['CHAN1','CHAN2','upper'],['Total VDD','Lower half-stack','Upper half-stack'],['#a97600','#008ea1','#903bb0']):
    y=np.where(valid[c][ix],v[c][ix],np.nan);good=np.any(np.isfinite(y),axis=1)
    lo=np.full(len(y),np.nan);hi=lo.copy();mid=lo.copy()
    lo[good]=np.nanmin(y[good],axis=1);hi[good]=np.nanmax(y[good],axis=1);mid[good]=np.nanmedian(y[good],axis=1)
    ax.fill_between(xt,lo,hi,color=color,alpha=.3);ax.plot(xt,mid,color=color,lw=.8,label=label)
    ax.set_ylabel('Volts');ax.grid(alpha=.2);ax.legend(loc='best')
    if c!='CHAN1':ax.axhline(1.2,color='#b33333',ls='--',lw=1)
axes[-1].set_xlabel('Milliseconds relative to scope trigger immediately before USB launch command')
fig.suptitle('Broadcast 375→400 MHz at 1.140 V per half-stack\nMeasured scope volts; min/max envelopes retain excursions',fontsize=13)
for ext in ['png','pdf']:fig.savefig(p/f'broadcast-transition.{ext}',dpi=150)
axes[-1].set_xlim(34,40)
for ext in ['png','pdf']:fig.savefig(p/f'broadcast-transition-zoom.{ext}',dpi=150)

# Report pulse duration on explicitly defined 20 us block means, separately
# from sample peaks. Do not interpret single-sample peaks as sustained levels.
n=round(20e-6/pre['CHAN1'][4]); count=len(t)//n*n
bt=t[:count].reshape(-1,n).mean(axis=1); pulses={}
for c in ['CHAN2','upper']:
    mean=v[c][:count].reshape(-1,n).mean(axis=1)
    pulses[c]={}
    for threshold in [1.2,1.3]:
        selected=np.flatnonzero(mean>threshold)
        groups=np.split(selected,np.where(np.diff(selected)!=1)[0]+1)
        pulses[c][str(threshold)]=[dict(start_block_center_s=float(bt[g[0]]),
            end_block_center_s=float(bt[g[-1]]),duration_s=float(len(g)*n*pre['CHAN1'][4]),
            median_block_mean_v=float(np.median(mean[g]))) for g in groups if len(g)]
(p/'scope-pulses.json').write_text(json.dumps(pulses,indent=2)+'\n')
