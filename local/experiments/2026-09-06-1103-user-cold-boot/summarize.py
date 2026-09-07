from pathlib import Path
import json,re
p=Path(__file__).resolve().parent
s=re.sub(r'\x1b\[[0-9;]*m','',(p/'serial.log').read_text())
rows=[json.loads(l) for l in (p/'diagnostic-samples.jsonl').read_text().splitlines()]
ready=re.search(r'\((\d+)\).*PROTO STARTUP READY frequency=([\d.]+)',s)
first=re.search(r'\((\d+)\).*PROTO STARTUP FIRST_WORK frequency=(\d+)',s)
temps=[{'boot_ms':int(m[0]),'frequency':float(m[1]),'temperatures':list(map(float,m[2:]))} for m in re.findall(r'\((\d+)\).*PROTO STARTUP TEMP freq=([\d.]+) A1=([\d.]+) A2=([\d.]+) A3=([\d.]+) A4=([\d.]+)',s)]
qual=[{'boot_ms':int(t),'frequency':float(f),'ghs':float(h),'result':r} for t,f,h,r in re.findall(r'\((\d+)\).*PROTO STARTUP QUALIFY frequency=([\d.]+) total=([\d.]+) result=(PASS|FAIL)',s)]
epochs={}
for r in rows:
 d=r['diagnostics'];chips=d['chips']
 if d['source']!='mining' or len(chips)!=4 or any(not c['valid'] or c['windowSeconds']<=0 or len(c['corePassCounts'])!=156 for c in chips):continue
 cs=[]
 for c in chips:
  factor=2**d['leadingZeros']/c['windowSeconds']/1e9
  domains=[]
  for n in range(4):
   counts=c['corePassCounts'][n*39:(n+1)*39];ghs=sum(counts)*factor
   domains.append({'domain':n,'ghs':ghs,'zero_cores':sum(x==0 for x in counts),'percent_ideal':ghs/(c['frequency']*.312)*100})
  cs.append({'asic':c['chipId']+1,'ghs':sum(x['ghs'] for x in domains),'domains':domains})
 epochs[d['epoch']]={'epoch':d['epoch'],'host_elapsed':r['elapsed'],'ghs':sum(c['ghs'] for c in cs),'chips':cs}
e=list(epochs.values());mining=[r for r in rows if r['diagnostics']['mining']==1 and r['diagnostics']['startupResult']=='passed']
result={'startup_qualifications':qual,'first_temperature':temps[0] if temps else None,'temperature_before_first_work':next((x for x in reversed(temps) if first and x['boot_ms']<int(first[1])),None),'ready_boot_ms':int(ready[1]) if ready else None,'final_pll_to_first_work_ms':int(first[1])-int(re.findall(r'\((\d+)\).*PROTO STARTUP PLL frequency=600 ',s)[-1]) if first else None,'pool_observed_seconds':mining[-1]['elapsed']-mining[0]['elapsed'] if mining else 0,'pool_epochs':len(e),'pool_ghs':{'min':min(x['ghs'] for x in e),'mean':sum(x['ghs'] for x in e)/len(e),'max':max(x['ghs'] for x in e),'last':e[-1]} if e else None,'mining_peak_temperature_c':max((t for r in mining for t in (r['info'].get('asicTemps') or [])),default=None),'mining_min_domain_percent':min((d['percent_ideal'] for x in e for c in x['chips'] for d in c['domains']),default=None),'final_info':rows[-1]['info'] if rows else None,'log_faults':[l for l in s.splitlines() if any(x in l for x in ['PROTO STARTUP FAILED','PROTO STARTUP STOP','too cool before first work','Guru Meditation','THERMAL invalid','THERMAL retry'])]}
global_rates=[float(h) for t,h in re.findall(r'\((\d+)\).*SPDLOG:.*total=([\d.]+) GH/s',s) if ready and int(t)>=int(ready[1])]
result['pool_global_spdlog']={'windows':len(global_rates),'min_ghs':min(global_rates),'mean_ghs':sum(global_rates)/len(global_rates),'max_ghs':max(global_rates)} if global_rates else None
result['boot_observation']=json.loads((p/'attachment.json').read_text())
result['captured_reset_banners']=re.findall(r'rst:[^\n]+',s)
result['pool_rejections']=[{'boot_ms':int(t),'reason':r.strip()} for t,r in re.findall(r'\((\d+)\).*stratum_v1_task: message result rejected: ([^\n]+)',s)]
result['pool_difficulty_updates']=[{'boot_ms':int(t),'difficulty':float(v)} for t,v in re.findall(r'\((\d+)\).*stratum_api: Set pool difficulty: ([\d.]+)',s)]
(p/'result.json').write_text(json.dumps(result,indent=2)+'\n');(p/'pool-epochs.json').write_text(json.dumps(e,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='pool_ghs'},indent=2));print('Pool GH/s summary', {k:v for k,v in result['pool_ghs'].items() if k!='last'} if e else None)
