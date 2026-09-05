"""Continuous domain rates, temperatures and fan control during the warm ramp."""
from pathlib import Path
import re,json,csv
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=Path(__file__).resolve().parent;log=(p/'serial.log').read_text()
pattern=re.compile(r'\((\d+)\).*BCAST SAMPLE phase=(\w+) A(\d) counter_us=(\d+) dt_us=(\d+) pass=([\d,]+) rate=([\d.,-]+) zero=([\d,]+) D2_mV=([\d.-]+) fail=([\d,]+) temp_C=([\d.-]+) freq=([\d.]+)')
previous={};epochs={};rows=[];totals={}
for m in pattern.finditer(log):
    boot,phase,chip,t,dt,passed,rates,zeros,d2,failed,temp,freq=m.groups()
    chip=int(chip);t=int(t);dt=int(dt);freq=float(freq)
    passed=list(map(int,passed.split(',')));failed=list(map(int,failed.split(',')));rates=list(map(float,rates.split(',')));zeros=list(map(int,zeros.split(',')))
    assert len(passed)==len(failed)==len(rates)==len(zeros)==4
    prior=previous.get(chip) if dt else None
    if not prior:epochs[chip]=epochs.get(chip,0)+1
    for d in range(4):
        dp=passed[d]-prior['passed'][d] if prior else None;df=failed[d]-prior['failed'][d] if prior else None
        pr=fr=None
        if prior:
            assert dt==t-prior['t'] and dp>=0 and df>=0 and freq==prior['freq']
            pr=dp*16777.216/dt/(freq*1248/4000)*100;fr=df*16777.216/dt/(freq*1248/4000)*100
            assert abs(pr-rates[d])<.011,(pr,rates[d])
        row=dict(boot_ms=int(boot),phase=phase,asic=chip,domain=d,epoch=epochs[chip],frequency_mhz=freq,counter_us=t,dt_us=dt,passed=passed[d],failed=failed[d],delta_pass=dp,delta_fail=df,pass_percent=pr,fail_activity_percent=fr,zero_pass_cores=zeros[d],d2_mv=float(d2),temperature_c=float(temp))
        rows.append(row)
        if prior and prior['phase']==phase and phase!='wait_scope':
            a=totals.setdefault((phase,chip,d,freq),dict(delta_pass=0,delta_fail=0,dt_us=0,samples=0))
            a['delta_pass']+=dp;a['delta_fail']+=df;a['dt_us']+=dt;a['samples']+=1
    previous[chip]=dict(t=t,passed=passed,failed=failed,phase=phase,freq=freq)
(p/'domain-samples.json').write_text(json.dumps(rows,indent=2)+'\n')
summary=[]
for (phase,chip,d,freq),a in totals.items():
    summary.append(dict(phase=phase,asic=chip,domain=d,frequency_mhz=freq,**a,pass_percent=a['delta_pass']*16777.216/a['dt_us']/(freq*1248/4000)*100,fail_fraction_percent=100*a['delta_fail']/(a['delta_pass']+a['delta_fail']) if a['delta_pass']+a['delta_fail'] else None))
(p/'phase-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
warm_rows = [r for r in rows if r['phase'] == 'warm300']
windows = []
if warm_rows:
    first_ms = min(r['boot_ms'] for r in warm_rows)
    last_ms = max(r['boot_ms'] for r in warm_rows)
    for label, lo, hi in [('first30s', first_ms, first_ms + 30000), ('last30s', last_ms - 30000, last_ms)]:
        for chip in range(1,5):
            for d in range(4):
                a = [r for r in warm_rows if r['asic']==chip and r['domain']==d and r['dt_us'] and lo<=r['boot_ms']<=hi]
                if not a: continue
                dp = sum(r['delta_pass'] for r in a); df = sum(r['delta_fail'] for r in a); dt = sum(r['dt_us'] for r in a)
                windows.append(dict(window=label,asic=chip,domain=d,seconds=dt/1e6,
                    pass_percent=dp*16777.216/dt/(a[0]['frequency_mhz']*1248/4000)*100,
                    fail_fraction_percent=100*df/(dp+df),
                    mean_temperature_c=sum(r['temperature_c'] for r in a)/len(a)))
(p/'warmup-windows.json').write_text(json.dumps(windows,indent=2)+'\n')
if rows:
    with (p/'domain-samples.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
    origin=min(x['boot_ms'] for x in rows)/1000
    fig,axes=plt.subplots(4,1,figsize=(11,10),sharex=True,layout='constrained')
    for chip,color in [(1,'#007c91'),(2,'#ad5100'),(3,'#7464a4'),(4,'#5d7834')]:
        a=[r for r in rows if r['asic']==chip and r['domain']==2]
        x=[r['boot_ms']/1000-origin for r in a]
        axes[0].plot(x,[r['temperature_c'] for r in a],color=color,label=f'A{chip}')
        axes[1].plot(x,[r['d2_mv'] for r in a],color=color,label=f'A{chip}')
        smooth=[]
        for i,r in enumerate(a):
            b=[v for v in a[max(0,i-4):i+1] if v['epoch']==r['epoch'] and v['phase']==r['phase'] and v['dt_us']]
            smooth.append(sum(v['delta_pass'] for v in b)*16777.216/sum(v['dt_us'] for v in b)/(r['frequency_mhz']*1248/4000)*100 if b and r['phase']!='wait_scope' else np.nan)
        axes[2].plot(x,smooth,color=color,label=f'A{chip} D2')
    fans=re.findall(r'\((\d+)\).*BCAST RAIL.*fan=([\d.]+)% rpm=(\d+)',log)
    axes[3].step([int(b)/1000-origin for b,_,_ in fans],[float(f) for _,f,_ in fans],where='post',color='#444444',label='Fan PWM')
    axes[0].axhline(55,color='gray',ls='--',lw=1);axes[2].axhline(100,color='gray',ls='--',lw=.8)
    for time,target in re.findall(r'\((\d+)\).*WARM TRIAL target=(\d+)',log):
        for ax in axes:ax.axvline(int(time)/1000-origin,color='black',ls=':',lw=.8)
    for ax,label in zip(axes,['ASIC temperature (°C)','D2 voltage (mV)','D2 throughput (% ideal)\n5-sample weighted mean','Fan command (%)']):
        ax.set_ylabel(label);ax.grid(alpha=.2);ax.legend(loc='best',fontsize=8)
    axes[-1].set_xlabel('Seconds since first cold 300 MHz snapshot (serial log time)')
    fig.suptitle('1103x warmup and broadcast frequency ramp at 1.140 V\nSensor/counter samples are sequential; counter re-arms are separate epochs')
    for ext in ['png','pdf']:fig.savefig(p/f'warmup-timeline.{ext}',dpi=150)
    plt.close(fig)
    for chip in range(1,5):
        a=[r for r in rows if r['asic']==chip and r['domain']==2]
        print('A'+str(chip),'first/last/max C',a[0]['temperature_c'],a[-1]['temperature_c'],max(r['temperature_c'] for r in a),'last MHz',a[-1]['frequency_mhz'])
for folder in p.iterdir():
    if not folder.is_dir() or not (folder/'raw.npz').exists():continue
    raw=np.load(folder/'raw.npz');pre={c:np.array(v.split(','),float) for c,v in json.loads((folder/'preambles.json').read_text()).items()}
    assert np.array_equal(pre['CHAN1'][4:7],pre['CHAN2'][4:7])
    t=(np.arange(len(raw['CHAN1']))-pre['CHAN1'][6])*pre['CHAN1'][4]+pre['CHAN1'][5]
    v={c:(raw[c].astype(float)-pre[c][8]-pre[c][9])*pre[c][7] for c in pre};v['upper']=v['CHAN1']-v['CHAN2']
    statistics = {}
    for name, values in [('total', v['CHAN1']), ('lower', v['CHAN2']), ('upper', v['upper'])]:
        above = values >= 1.2
        edges = np.flatnonzero(np.diff(np.r_[False, above, False]))
        runs = edges[1::2] - edges[::2]
        statistics[name] = dict(mean_v=float(values.mean()),
            percentiles_v=dict(zip(['0','1','50','99','100'], map(float, np.percentile(values,[0,1,50,99,100])))),
            samples_ge_1_2=int(above.sum()),
            longest_consecutive_samples_ge_1_2=int(runs.max()) if len(runs) else 0,
            max_20us_block_mean_v=float(values[:len(values)//40*40].reshape(-1,40).mean(1).max()))
    (folder/'statistics.json').write_text(json.dumps(statistics,indent=2)+'\n')
    valid=(raw['CHAN1']>0)&(raw['CHAN1']<255)&(raw['CHAN2']>0)&(raw['CHAN2']<255)
    fig,axes=plt.subplots(3,1,figsize=(10,7),sharex=True,layout='constrained');n=100;size=len(t)//n*n;x=t[:size].reshape(-1,n).mean(1)*1000
    for ax,c,label in zip(axes,['CHAN1','CHAN2','upper'],['Total VDD','Lower half-stack','Upper half-stack']):
        y=np.where(valid,v[c],np.nan)[:size].reshape(-1,n)
        ax.fill_between(x,np.nanmin(y,axis=1),np.nanmax(y,axis=1),alpha=.35);ax.plot(x,np.nanmedian(y,axis=1),label=label,lw=.8)
        ax.set_ylabel('Volts');ax.legend();ax.grid(alpha=.2)
        if c!='CHAN1':ax.axhline(1.2,color='red',ls='--',lw=.8)
    axes[-1].set_xlabel('Milliseconds relative to forced scope trigger')
    fig.suptitle(folder.name+' — uncorrected scope volts; sampled min/max envelope')
    for ext in ['png','pdf']:fig.savefig(folder/f'waveform.{ext}',dpi=150)
    plt.close(fig)
