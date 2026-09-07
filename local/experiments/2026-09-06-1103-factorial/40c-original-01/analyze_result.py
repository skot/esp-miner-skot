import json,re
from pathlib import Path
p=Path(__file__).resolve().parent
s=(p/'serial.log').read_text()
assert 'WARM END result=COMPLETE' in s
rows=json.loads((p/'domain-samples.json').read_text())
thermal=json.loads((p/'thermal-summary.json').read_text())
a=[r for r in rows if r['phase']=='warm600' and r['dt_us']]
last=max(r['boot_ms'] for r in a)
windows={}
for label,minimum in [('whole_hold',0),('last30s',last-30000)]:
 chips=[]
 for chip in range(1,5):
  domains=[]
  for domain in range(4):
   b=[r for r in a if r['asic']==chip and r['domain']==domain and r['boot_ms']>=minimum]
   elapsed=sum(r['dt_us'] for r in b)
   rate=sum(r['delta_pass'] for r in b)*16777.216/elapsed
   domains.append(dict(domain=domain,ghs=rate,percent_ideal=rate/187.2*100,max_zero_cores=max(r['zero_pass_cores'] for r in b),seconds=elapsed/1e6))
  chips.append(dict(asic=chip,ghs=sum(d['ghs'] for d in domains),domains=domains))
 windows[label]=dict(chips=chips,total_ghs=sum(c['ghs'] for c in chips))
launch=int(re.search(r'\((\d+)\).*DEFER LAUNCHED',s)[1])
t=json.loads((p/'thermal-samples.json').read_text())
t=[r for r in t if r['boot_ms']>=launch]
result=dict(windows=windows,thermal_invalid=thermal['invalid'],post_launch_temperatures={str(c):dict(min=min(r['temperature'] for r in t if r['asic']==c),max=max(r['temperature'] for r in t if r['asic']==c),final=[r['temperature'] for r in t if r['asic']==c][-1],distinct=len(set(r['temperature'] for r in t if r['asic']==c))) for c in range(1,5)},completion=[l for l in s.splitlines() if 'DEFER END' in l or 'WARM END' in l],scope={'captured':False,'reason':'User requested log-only test; no probes attached'})
(p/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
