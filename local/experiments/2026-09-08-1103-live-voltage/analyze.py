"""Summarize only complete, fresh, distinct mining-counter epochs."""
import collections, json, pathlib, re, statistics
P=pathlib.Path(__file__).resolve().parent
rows=[json.loads(s) for s in (P/'samples.jsonl').read_text().splitlines()]
results=[];baseline=None
for name in dict.fromkeys(r['phase'] for r in rows):
    seen=set();epochs=[];powers=[];temps=[];volts=[];phase_rows=[r for r in rows if r['phase']==name]
    for r in phase_rows:
        d=r['diagnostics'];chips=d['chips']
        if r['elapsed']<15 or d['epoch'] in seen or len(chips)!=4:continue
        if d['source']!='mining' or any(c['valid']!=1 or c['ageSeconds']>15 or c['windowSeconds']<=0 or len(c['corePassCounts'])!=156 or c['frequency']!=600 for c in chips):continue
        seen.add(d['epoch'])
        rates=[n*2**d['leadingZeros']/c['windowSeconds']/1e9 for c in chips for n in c['corePassCounts']]
        epochs.append(rates);powers.append(r['info']['power']);temps.append(r['info']['asicTemps']);volts.append(r['info']['asicVoltages'])
    if not epochs:continue
    cores=[statistics.mean(e[i] for e in epochs) for i in range(624)]
    totals=[sum(e) for e in epochs];rate=statistics.mean(totals)
    if name=='baseline':baseline=cores
    output={'phase':name,'millivolts':phase_rows[0]['target_mv'],'epochs':len(epochs),'mean_ghs':rate,'min_ghs':min(totals),'mean_power_w':statistics.mean(powers),'mean_j_per_th':statistics.mean(p/(r/1000) for p,r in zip(powers,totals)), 'domains_mean_ghs':[sum(cores[i*39:(i+1)*39]) for i in range(16)],'min_active_cores':min(sum(v>0 for v in e) for e in epochs),'max_temp_c':max(max(t) for t in temps),'core_mean_ghs':cores,'cores_below_90_percent_baseline':[{'asic':i//156+1,'domain':i%156//39,'core':i%156,'baseline_ghs':b,'phase_ghs':v,'percent_baseline':v/b*100} for i,(b,v) in enumerate(zip(baseline,cores)) if b>1 and v<.9*b], 'temperature_distinct_values':[len(set(t[i] for t in temps)) for i in range(4)],'voltage_distinct_values':[len(set(v[i] for v in volts)) for i in range(4)]}
    results.append(output)
(P/'analysis.json').write_text(json.dumps(results,indent=2)+'\n')
raw=re.sub(r'\x1b\[[0-9;]*m','',(P/'device-all.log').read_text(errors='replace'))
unique=list(dict.fromkeys(raw.splitlines()))
start=json.loads((P/'startup-ready.json').read_text())['info']['uptimeSeconds']*1000
lines=[s for s in unique if (m:=re.search(r'\((\d+)\)',s)) and int(m[1])>=start]
pvt={'fresh_valid_thermal_readings_per_asic':{str(i):sum(f'A{i} fresh=1' in s and 'valid=1' in s for s in lines if 'THERMAL attempt=' in s) for i in range(1,5)},'thermal_retry_lines':sum('THERMAL retry' in s for s in lines),'thermal_invalid_lines':sum('THERMAL attempt=' in s and 'valid=0' in s for s in lines),'voltage_error_lines':[s for s in lines if 'voltage' in s.lower() and any(t in s.lower() for t in ['failed','timeout'])],'voltage_changes':[s for s in lines if 'LIVE VOLTAGE' in s]}
(P/'pvt-summary.json').write_text(json.dumps(pvt,indent=2)+'\n')
(P/'startup-events.txt').write_text('\n'.join(s for s in unique if any(x in s for x in ['PROTO STARTUP QUALIFY frequency=600','PROTO STARTUP READY','PROTO STARTUP PVT','LIVE VOLTAGE']))+'\n')
for r in results:print(json.dumps({k:v for k,v in r.items() if k not in ['core_mean_ghs','domains_mean_ghs','cores_below_90_percent_baseline']}));print('cores below 90% baseline:',len(r['cores_below_90_percent_baseline']))
print(json.dumps(pvt,indent=2))
