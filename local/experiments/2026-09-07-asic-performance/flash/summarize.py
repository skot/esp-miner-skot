import pathlib,json,re
p=pathlib.Path(__file__).resolve().parent
rows=[json.loads(x) for x in (p/'samples.jsonl').read_text().splitlines()]
log=(p/'device-all.log').read_text(errors='replace')
log=re.sub(r'\x1b\[[0-9;]*m','',log)
qual=list(dict.fromkeys(re.findall(r'PROTO STARTUP QUALIFY frequency=(\d+) total=([\d.]+) result=(\w+)',log)))
last_epoch=None; windows=[]
for row in rows:
 d=row['diagnostics']
 if d['mining']!=1 or d['source']!='mining' or d['epoch']==last_epoch or len(d['chips'])!=4:continue
 if any(c['valid']!=1 or c['windowSeconds']<=0 or len(c['corePassCounts'])!=156 for c in d['chips']):continue
 last_epoch=d['epoch'];chips=[]
 for c in d['chips']:
  domains=[]
  for i in range(4):
   counts=c['corePassCounts'][39*i:39*(i+1)]
   rate=sum(counts)*2**d['leadingZeros']/c['windowSeconds']/1e9
   domains.append({'id':i,'ghs':rate,'zero_cores':counts.count(0)})
  chips.append({'id':c['chipId'],'domains':domains})
 windows.append({'epoch':d['epoch'],'chips':chips})
summary={'startup_qualifications':[{'MHz':int(f),'ghs':float(h),'result':r} for f,h,r in qual], 'complete_mining_epochs':len(windows),'zero_domains_observed':sum(d['ghs']==0 for w in windows for c in w['chips'] for d in c['domains']),'last_domain_window':windows[-1] if windows else None}
(p/'domain-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
