from pathlib import Path
import json,re
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=Path(__file__).resolve().parent
log=(p/'serial.log').read_text();rows=json.loads((p/'domain-samples.json').read_text())
m=re.search(r'\((\d+)\).*WARM ZERO DETECTED target=(\d+) mask=0x([0-9a-f]+)',log)
if not m:raise SystemExit('No zero-PASS hold detected')
start,freq,mask=int(m[1]),int(m[2]),int(m[3],16)
endmatch=re.search(r'\((\d+)\).*WARM ZERO HOLD END elapsed_seconds=([\d.]+) recovered_all=(\d)',log)
end=int(endmatch[1]) if endmatch else max(r['boot_ms'] for r in rows)
a=[r for r in rows if r['phase']==f'warm{freq}']
result=dict(frequency_mhz=freq,detected_boot_ms=start,end_boot_ms=end,completed=bool(endmatch),duration_seconds=float(endmatch[2]) if endmatch else None,initial_mask=hex(mask),domains=[])
for chip in range(1,5):
 for dom in range(4):
  samples=[r for r in a if r['asic']==chip and r['domain']==dom]
  valid=[r for r in samples if r['dt_us'] and r['boot_ms']>=start]
  initial=bool(mask & (1<<((chip-1)*4+dom)))
  positive=next((r for r in valid if r['delta_pass']>0),None)
  healthy=next((r for r in valid if r['pass_percent']>=97),None)
  windows={}
  for label,cutoff in [('hold',start),('last30s',end-30000),('last10s',end-10000)]:
   w=[r for r in valid if r['boot_ms']-r['dt_us']/1000>=cutoff]
   dp=sum(r['delta_pass'] for r in w);df=sum(r['delta_fail'] for r in w);dt=sum(r['dt_us'] for r in w)
   windows[label]=dict(seconds=dt/1e6,delta_pass=dp,delta_fail=df,throughput_percent=dp*16777.216/dt/(freq*1248/4000)*100 if dt else None,pass_fraction_percent=100*dp/(dp+df) if dp+df else None)
  d=dict(asic=chip,domain=dom,initially_zero=initial,first_positive_seconds=(positive['boot_ms']-start)/1000 if positive else None,first_97_percent_seconds=(healthy['boot_ms']-start)/1000 if healthy else None,final_zero_cores=samples[-1]['zero_pass_cores'],start_d2_mv=samples[0]['d2_mv'],end_d2_mv=samples[-1]['d2_mv'],start_temperature_c=samples[0]['temperature_c'],end_temperature_c=samples[-1]['temperature_c'],windows=windows)
  result['domains'].append(d)
  print(f"A{chip} D{dom}: initially_zero={initial} first_PASS={d['first_positive_seconds']}s first_97%={d['first_97_percent_seconds']}s last30={windows['last30s']['throughput_percent']:.2f}% last_zero={d['final_zero_cores']}")
result['last30_estimated_ghs']=sum(d['windows']['last30s']['throughput_percent']/100*freq*1248/4000 for d in result['domains'])
(p/'hold-summary.json').write_text(json.dumps(result,indent=2)+'\n')
fig,axes=plt.subplots(4,1,figsize=(11,11),sharex=True,layout='constrained')
colors=['#007c91','#ad5100','#7464a4','#5d7834']
for chip,color in zip(range(1,5),colors):
 for dom in range(4):
  ss=[r for r in a if r['asic']==chip and r['domain']==dom and r['dt_us']]
  x=[(r['boot_ms']-start)/1000 for r in ss]
  y=[]
  for i,r in enumerate(ss):
   w=ss[max(0,i-4):i+1]
   y.append(sum(v['delta_pass'] for v in w)*16777.216/sum(v['dt_us'] for v in w)/(freq*1248/4000)*100)
  axes[chip-1].plot(x,y,label=f'D{dom}',color=['#007c91','#ba4d30','#6b4fa1','#47783e'][dom],lw=1.4,linestyle=['-', '--', ':', '-.'][dom])
 axes[chip-1].set_ylabel(f'A{chip} throughput\n(% ideal)');axes[chip-1].set_ylim(-5,110);axes[chip-1].axvline(0,color='black',lw=.8);axes[chip-1].axhline(100,color='gray',lw=.6);axes[chip-1].grid(alpha=.2);axes[chip-1].legend(ncol=4,loc='center right')
axes[0].set_title(f'1102: {freq} MHz held after first zero-PASS domain\n5-sample counter intervals; no PLL/work/reset/rearm during hold')
axes[-1].set_xlabel('Seconds since first zero-PASS detection')
fig.savefig(p/'hold-domain-throughput.png',dpi=170);fig.savefig(p/'hold-domain-throughput.pdf');plt.close(fig)
fig,axes=plt.subplots(2,1,figsize=(11,6),sharex=True,layout='constrained')
for chip,color in zip(range(1,5),colors):
 ss=[r for r in a if r['asic']==chip and r['domain']==2];x=[(r['boot_ms']-start)/1000 for r in ss]
 axes[0].plot(x,[r['d2_mv'] for r in ss],label=f'A{chip}',color=color)
 axes[1].plot(x,[r['temperature_c'] for r in ss],label=f'A{chip}',color=color)
for ax in axes:ax.grid(alpha=.2);ax.legend(ncol=4);ax.axvline(0,color='black',lw=.8)
axes[0].set_ylabel('Internal D2 voltage (mV)');axes[1].set_ylabel('ASIC temperature (°C)');axes[1].set_xlabel('Seconds since first zero-PASS detection')
axes[0].set_title(f'1102: {freq} MHz fixed-frequency hold')
fig.savefig(p/'hold-voltage-temperature.png',dpi=170);fig.savefig(p/'hold-voltage-temperature.pdf')
print('Hold complete:',result['completed'],'seconds:',result['duration_seconds'],'last30 estimated GH/s:',result['last30_estimated_ghs'])
