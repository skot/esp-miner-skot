"""Analyze unchanged counter epochs and compare early versus late qualification."""
from pathlib import Path
import argparse,json,re,subprocess
p=Path(__file__).resolve().parent;a=argparse.ArgumentParser();a.add_argument('name');args=a.parse_args();out=p/args.name
with (out/'analysis.log').open('w') as log:
 for script in ['analyze_warm.py','analyze_thermal.py','analyze_result.py']:
  subprocess.run(['/tmp/bitaxe-bench-env/bin/python',str(out/script)],stdout=log,stderr=subprocess.STDOUT,check=True)
s=(out/'serial.log').read_text();rows=json.loads((out/'domain-samples.json').read_text());rows=[r for r in rows if r['phase']=='warm600']
launch=int(re.search(r'\((\d+)\).*DEFER LAUNCHED',s)[1])
excluded=0
config=(out/'proto_compare.h').read_text()
if '#define PROTO_COMPARE_PASSIVE_LAUNCH 1' in config and 'mc3_spdlog_timer_value(1431)' in (out/'mc3-source.c').read_text():
 # This first trial copied production's 120.04s timer. Exclude the end of
 # that epoch, rather than treating frozen counters as a throughput loss.
 work=int(re.search(r'\((\d+)\).*DEFER FIRST_WORK',s)[1])
 filtered=[r for r in rows if r['boot_ms']<=work+119000]
 excluded=len(rows)-len(filtered);rows=filtered
last=max(r['boot_ms'] for r in rows)
result={}
for label,lo,hi in [('first10s',launch,launch+10000),('10to30s',launch+10000,launch+30000),('30to60s',launch+30000,launch+60000),('last30s',last-30000,last),('last10s',last-10000,last),('whole',launch,last)]:
 chips=[];zero_domains=[];passes=True
 for c in range(1,5):
  ds=[]
  for d in range(4):
   rs=[r for r in rows if r['asic']==c and r['domain']==d and r['dt_us'] and lo<=r['boot_ms']<=hi]
   assert rs,(label,c,d)
   dt=sum(r['dt_us'] for r in rs);dp=sum(r['delta_pass'] for r in rs);df=sum(r['delta_fail'] for r in rs)
   ghs=dp*16777.216/dt
   ds.append(dict(domain=d,ghs=ghs,percent_ideal=ghs/187.2*100,passed=dp,failed=df,seconds=dt/1e6,max_zero_cores=max(r['zero_pass_cores'] for r in rs),min_zero_cores=min(r['zero_pass_cores'] for r in rs)))
   if dp==0:zero_domains.append(f'A{c}D{d}')
  ghs=sum(d['ghs'] for d in ds);dp=sum(d['passed'] for d in ds);df=sum(d['failed'] for d in ds)
  passed=ghs/748.8*100>=97.5 and dp/(dp+df)*100>=97 and all((d['ghs']/ghs*400 if ghs else 0)>=90 for d in ds)
  passes &= passed
  chips.append(dict(asic=c,ghs=ghs,qualify=passed,domains=ds))
 result[label]=dict(total_ghs=sum(c['ghs'] for c in chips),qualify=passes,zero_domains=zero_domains,chips=chips)
result['metadata']={'launch_ms':launch,'cooling_control':[l for l in s.splitlines() if 'COMPARE COOL begin' in l or 'COMPARE COOL READY' in l],'excluded_rows_near_timer_expiry':excluded,'first_work_temperature_line':next(l for l in s.splitlines() if 'DEFER TEMP freq=600' in l),'warm_qualified_temperatures':[r['temperature_c'] for r in json.loads((out/'domain-samples.json').read_text()) if r['phase']=='warm_qualified300' and r['domain']==0],'completion':[l for l in s.splitlines() if 'WARM END' in l or 'DEFER END' in l],'config':(out/'proto_compare.h').read_text(),'interpretation':'Qualification is calculated from interval rates using production thresholds; sampling cadence differs from production. No actual shutdown or rearm at ten seconds.'}
(out/'window-comparison.json').write_text(json.dumps(result,indent=2)+'\n')
print(args.name)
for label,v in result.items():
 if label!='metadata':print(label,round(v['total_ghs'],2),'qualify',v['qualify'],'zero',v['zero_domains'])
print(result['metadata'])
