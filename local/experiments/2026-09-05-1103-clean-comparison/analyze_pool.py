"""Summarize actual pool work, startup milestones and per-ASIC SPDLOG windows."""
from pathlib import Path
import json,re,statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=Path(__file__).resolve().parent
summaries=[]
for run in sorted(p.glob('run-*')):
 if not run.is_dir() or not (run/'serial.log').exists():continue
 log=(run/'serial.log').read_text()
 records=[];ready_seen=False
 for line in log.splitlines():
  if 'app_init: App version:' in line:ready_seen=False;records=[]
  if 'PROTO STARTUP READY' in line:ready_seen=True
  m=re.search(r'\((\d+)\).*SPDLOG:.*total=([\d.]+) GH/s',line)
  if m and ready_seen:
   chips=[float(v) for v in re.findall(r'chip\d=([\d.]+) GH/s',line)]
   if len(chips)==4:records.append(dict(boot_ms=int(m[1]),total=float(m[2]),chips=chips))
 milestones=[line for line in log.splitlines() if any(x in line for x in ['PROTO STARTUP BEGIN','PROTO STARTUP DISCHARGED','PROTO STARTUP REINITIALIZED','PROTO STARTUP FIRST_WORK','PROTO STARTUP READY','PROTO STARTUP FAILED'])]
 rows=[json.loads(line) for line in (run/'api-samples.jsonl').read_text().splitlines()] if (run/'api-samples.jsonl').exists() else []
 ready=re.search(r'\((\d+)\).*PROTO STARTUP READY',log)
 begin=re.search(r'\((\d+)\).*PROTO STARTUP BEGIN',log)
 first_temp=re.search(r'PROTO STARTUP TEMP freq=100 A1=([\d.]+) A2=([\d.]+) A3=([\d.]+) A4=([\d.]+)',log)
 # Exclude the first minute following handoff from the steady SPD summary.
 steady=[r for r in records if ready and r['boot_ms']>=int(ready[1])+60000]
 summary=dict(run=run.name,milestones=milestones,first_temperatures_c=list(map(float,first_temp.groups())) if first_temp else None,
  ready_boot_seconds=int(ready[1])/1000 if ready else None,
  warm_sequence_seconds=(int(ready[1])-int(begin[1]))/1000 if ready and begin else None,
  spd_windows=len(records),steady_spd_windows=len(steady),
  pool_job_writes=log.count('Writing MC3 work job_id='),persistent_thermal_failures=log.count('THERMAL invalid after bounded retries'),
  thermal_retries=log.count('THERMAL retry after invalid sample'),fault=bool(re.search('Guru Meditation|PROTO STARTUP FAILED|Proto temperature read failed',log)))
 if steady:
  summary.update(steady_mean_ghs=statistics.mean(r['total'] for r in steady),steady_min_ghs=min(r['total'] for r in steady),
   per_asic_mean_ghs=[statistics.mean(r['chips'][c] for r in steady) for c in range(4)],per_asic_min_ghs=[min(r['chips'][c] for r in steady) for c in range(4)])
 if (run/'result.json').exists():summary['supervisor']=json.loads((run/'result.json').read_text())
 (run/'analysis-summary.json').write_text(json.dumps(summary,indent=2));summaries.append(summary)
 if rows and records:
  fig,axes=plt.subplots(3,1,figsize=(10,8),layout='constrained',sharex=True)
  x=[r['host_seconds']/60 for r in rows]
  captured=json.loads((run/'spd-samples.json').read_text()) if (run/'spd-samples.json').exists() else []
  captured=captured[-len(records):] if records else []
  if captured:
   axes[0].plot([r['host_seconds']/60 for r in captured],[r['total_ghs']/1000 for r in captured],lw=.7)
  else:
   # Live captures have not saved the host-timed SPD table yet.
   offset=next((r['uptimeSeconds']-r['host_seconds'] for r in reversed(rows) if r.get('uptimeSeconds') is not None),0)
   axes[0].plot([(r['boot_ms']/1000-offset)/60 for r in records],[r['total']/1000 for r in records],lw=.7)
  axes[0].axhline(2.9952,color='gray',ls='--',label='600 MHz ideal');axes[0].set_ylabel('ASIC counters (TH/s)');axes[0].legend()
  for c in range(4):
   a=[r for r in rows if len(r.get('asicTemps') or [])==4]
   axes[1].plot([r['host_seconds']/60 for r in a],[r['asicTemps'][c] for r in a],label=f'A{c+1}')
  axes[1].set_ylabel('ASIC temperature (°C)');axes[1].legend(ncols=4)
  axes[2].plot(x,[r.get('sharesAccepted') or 0 for r in rows],label='Accepted')
  axes[2].plot(x,[r.get('sharesRejected') or 0 for r in rows],label='Rejected');axes[2].set_ylabel('Pool shares');axes[2].legend()
  for ax in axes:ax.set_xlabel('Minutes since capture started');ax.grid(alpha=.2)
  fig.suptitle('Stock 1103 — '+run.name+' — 600 MHz, 1160 mV/half; no scope')
  fig.savefig(run/'pool-timeline.png',dpi=140);plt.close(fig)
(p/'run-summary.json').write_text(json.dumps(summaries,indent=2))
for r in summaries:print({k:v for k,v in r.items() if k not in ['milestones','supervisor']})
