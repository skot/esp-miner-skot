import pathlib,json,re
p=pathlib.Path(__file__).resolve().parent
rows=[json.loads(x) for x in (p/'samples.jsonl').read_text().splitlines()]
# HTTP log downloads may contain older boots: require the unique 1180 mV startup marker.
raw=(p/'device-all.log').read_text(errors='replace')
raw=re.sub(r'\x1b\[[0-9;]*m','',raw)
marker='PROTO STARTUP BEGIN target=600 voltage=1180; warmup=300 coldest>=45C'
start=raw.rfind(marker)
current=raw[start+len(marker):] if start>=0 else ''
quals=list(dict.fromkeys(re.findall(r'PROTO STARTUP QUALIFY frequency=(\d+) total=([\d.]+) result=(\w+)',current)))
temps=list(dict.fromkeys(re.findall(r'PROTO STARTUP TEMP freq=(\d+) A1=([\d.]+) A2=([\d.]+) A3=([\d.]+) A4=([\d.]+)',current)))
seen=set();windows=[]
for row in rows:
 d=row['diagnostics']
 if d['mining']!=1 or d['source']!='mining' or d['epoch'] in seen or len(d['chips'])!=4:continue
 if any(c['valid']!=1 or c['windowSeconds']<=0 or len(c['corePassCounts'])!=156 for c in d['chips']):continue
 seen.add(d['epoch']);chips=[]
 for c in d['chips']:
  domains=[]
  for i in range(4):
   counts=c['corePassCounts'][39*i:39*(i+1)];rate=sum(counts)*2**d['leadingZeros']/c['windowSeconds']/1e9
   domains.append({'domain':i,'ghs':rate,'zero_cores':counts.count(0),'percent_ideal':rate/(c['frequency']*.312)*100})
  chips.append({'asic':c['chipId']+1,'domains':domains})
 windows.append({'epoch':d['epoch'],'chips':chips})
rails=[v for row in rows for v in row['info'].get('domainVoltages',[]) if v>0]
pool=[row['info'] for row in rows if row['diagnostics']['mining']==1 and row['diagnostics']['startupResult']=='passed']
rates=[sum(d['ghs'] for c in w['chips'] for d in c['domains']) for w in windows]
summary={'mean_counter_ghs':sum(rates)/len(rates) if rates else None,'min_counter_ghs':min(rates,default=None),'max_counter_ghs':max(rates,default=None),'qualifications':[{'MHz':int(f),'ghs':float(h),'result':r} for f,h,r in quals], 'first_temperatures':temps[0] if temps else None,'first_600_temperatures':next((t for t in temps if t[0]=='600'),None),'complete_mining_epochs':len(windows),'zero_domains_observed':sum(d['ghs']==0 for w in windows for c in w['chips'] for d in c['domains']),'min_domain_percent':min((d['percent_ideal'] for w in windows for c in w['chips'] for d in c['domains']),default=None),'sampled_max_halfstack_mv':max(rails,default=None),'sampled_halfstack_readings_above_1200mv':sum(v>1200 for v in rails),'pool_peak_temperature':max((v for info in pool for v in info.get('asicTemps',[])),default=None),'last_domain_window':windows[-1] if windows else None}
(p/'domain-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
(p/'pool-epochs.json').write_text(json.dumps(windows,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k!='last_domain_window'},indent=2))
