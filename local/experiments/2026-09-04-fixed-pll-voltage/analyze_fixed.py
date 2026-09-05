"""Check continuous PASS/FAIL counts and correlate D2, PVT and temperature."""
import csv,json,re
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=Path(__file__).resolve().parent
log=(p/'transition-serial.log').read_text()
settings=re.search(r'BCAST BEGIN fixed_PLL=(\d+) baseline_voltage=(\d+) test_voltage=(\d+)',log)
assert settings, 'Missing fixed-PLL experiment parameters'
frequency,baseline_mv,test_mv=map(int,settings.groups())
hold=re.search(r'BCAST HOLD frequency=(\d+)',log)
if hold: frequency=int(hold[1])
expected_domain_ghs=frequency*1248/4000
pattern=re.compile(r'BCAST SAMPLE phase=(\w+) A(\d) counter_us=(\d+) dt_us=(\d+) pass=([\d,]+) rate=([\d.,-]+) zero=([\d,]+) D2_mV=([\d.-]+) fail=([\d,]+) temp_C=([\d.-]+)')
rows=[];previous={};totals={}
for m in pattern.finditer(log):
    phase,chip,t,dt,passed,rates,zeros,d2,failed,temp=m.groups()
    chip=int(chip);t=int(t);dt=int(dt)
    passed=list(map(int,passed.split(',')));failed=list(map(int,failed.split(',')))
    rates=list(map(float,rates.split(',')));zeros=list(map(int,zeros.split(',')))
    assert len(passed)==len(failed)==len(rates)==len(zeros)==4
    prior=previous.get(chip) if dt else None
    for d in range(4):
        dp=passed[d]-prior['passed'][d] if prior else None
        df=failed[d]-prior['failed'][d] if prior else None
        pr=fr=None
        if prior:
            assert dt==t-prior['t'] and dt>0 and dp>=0 and df>=0
            pr=dp*16777.216/dt/expected_domain_ghs*100;fr=df*16777.216/dt/expected_domain_ghs*100
            assert abs(pr-rates[d])<.011,(pr,rates[d])
        row=dict(phase=phase,asic=chip,domain=d,counter_us=t,dt_us=dt,passed=passed[d],failed=failed[d],delta_pass=dp,delta_fail=df,pass_percent=pr,fail_percent_of_ideal=fr,activity_percent=pr+fr if prior else None,zero_pass_cores_cumulative=zeros[d],d2_mv=float(d2),temperature_c=float(temp))
        rows.append(row)
        if prior and prior['phase']==phase:
            a=totals.setdefault((phase,chip,d),dict(delta_pass=0,delta_fail=0,dt_us=0,samples=0))
            a['delta_pass']+=dp;a['delta_fail']+=df;a['dt_us']+=dt;a['samples']+=1
    previous[chip]=dict(t=t,passed=passed,failed=failed,phase=phase)
(p/'domain-samples.json').write_text(json.dumps(rows,indent=2)+'\n')
summary=[]
for (phase,chip,d),a in totals.items():
    r=dict(phase=phase,asic=chip,domain=d,**a,pass_percent=a['delta_pass']*16777.216/a['dt_us']/expected_domain_ghs*100,fail_fraction_percent=100*a['delta_fail']/(a['delta_pass']+a['delta_fail']) if a['delta_pass']+a['delta_fail'] else None)
    summary.append(r)
    if d==2:print(r)
(p/'phase-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
if rows:
    with (p/'domain-samples.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
    origin=min(r['counter_us'] for r in rows)
    fig,axes=plt.subplots(3,1,figsize=(10,8),sharex=True,layout='constrained')
    for chip,color in [(1,'#007c91'),(2,'#ad5100'),(3,'#7464a4'),(4,'#5d7834')]:
        a=[r for r in rows if r['asic']==chip and r['domain']==2 and r['dt_us']]
        x=[(r['counter_us']-origin)/1e6 for r in a]
        axes[0].plot(x,[r['pass_percent'] for r in a],'.-',color=color,label=f'A{chip} PASS')
        axes[0].plot(x,[r['activity_percent'] for r in a],'--',color=color,alpha=.6)
        axes[1].plot(x,[r['d2_mv'] for r in a],'.-',color=color,label=f'A{chip}')
        axes[2].plot(x,[r['temperature_c'] for r in a],'.-',color=color,label=f'A{chip}')
    changed=[(r['counter_us']-origin)/1e6 for r in rows if r['phase']=='voltage_only']
    if changed:
        for ax in axes:ax.axvline(min(changed),color='gray',ls=':',label='First 1.100 V sample')
    axes[0].set_ylabel('% ideal D2 throughput');axes[0].axhline(100,color='gray',lw=.7)
    axes[1].set_ylabel('D2 voltage (mV)');axes[2].set_ylabel('ASIC temperature (°C)')
    axes[2].set_xlabel('Seconds since first A1 baseline counter snapshot')
    for ax in axes:ax.grid(alpha=.2);ax.legend(fontsize=8)
    fig.suptitle((f'{frequency} MHz / {baseline_mv} mV: rejected-baseline observation' if hold else f'{frequency} MHz: {baseline_mv}→{test_mv} mV with fixed PLL/work')+'\nDashed throughput lines include PASS + FAIL')
    for ext in ['png','pdf']:fig.savefig(p/f'd2-timeline.{ext}',dpi=150)
    plt.close(fig)
if (p/'transition-raw.npz').exists():
    raw=np.load(p/'transition-raw.npz');pre={c:np.array(v.split(','),float) for c,v in json.loads((p/'transition-preambles.json').read_text()).items()}
    assert np.array_equal(pre['CHAN1'][4:7],pre['CHAN2'][4:7])
    t=(np.arange(len(raw['CHAN1']))-pre['CHAN1'][6])*pre['CHAN1'][4]+pre['CHAN1'][5]
    v={c:(raw[c].astype(float)-pre[c][8]-pre[c][9])*pre[c][7] for c in pre};v['upper']=v['CHAN1']-v['CHAN2']
    valid=(raw['CHAN1']>0)&(raw['CHAN1']<255)&(raw['CHAN2']>0)&(raw['CHAN2']<255)
    ss=dict(points=len(t),sample_interval_s=float(pre['CHAN1'][4]),clipped_samples=int((~valid).sum()),note='Uncorrected scope volts. These extrema exclude clipped samples.')
    for c in v:ss[c]=dict(min_v=float(v[c][valid].min()),max_v=float(v[c][valid].max()),baseline_v=float(np.median(v[c][valid&(t<0)])))
    (p/'scope-summary.json').write_text(json.dumps(ss,indent=2)+'\n');print(ss)
    fig,axes=plt.subplots(3,1,figsize=(10,7),sharex=True,layout='constrained')
    n=100;count=len(t)//n*n;x=t[:count].reshape(-1,n).mean(1)*1000
    for ax,c,label in zip(axes,['CHAN1','CHAN2','upper'],['Total VDD','Lower half-stack','Upper half-stack']):
        y=np.where(valid,v[c],np.nan)[:count].reshape(-1,n)
        ax.fill_between(x,np.nanmin(y,axis=1),np.nanmax(y,axis=1),alpha=.35)
        ax.plot(x,np.nanmedian(y,axis=1),lw=.8,label=label);ax.set_ylabel('Volts');ax.grid(alpha=.2);ax.legend()
        if c!='CHAN1':ax.axhline(1.2,color='red',ls='--',lw=.8)
    axes[-1].set_xlabel('Milliseconds relative to scope force trigger before USB G')
    fig.suptitle('Fixed 350 MHz, regulator 1.120→1.100 V\nUncorrected scope volts; envelope preserves sampled peaks')
    for ext in ['png','pdf']:fig.savefig(p/f'voltage-transition.{ext}',dpi=150)
