import json,pathlib,re,subprocess,sys
root=pathlib.Path(__file__).resolve().parent;results=[]
for p in sorted(root.glob('attempt-*')):
 final=p/'final.json' if (p/'final.json').exists() else p/'failure.json'
 if not final.exists():continue
 subprocess.run([sys.executable,str(p/'summarize.py')],check=True,stdout=subprocess.DEVNULL)
 raw=re.sub(r'\x1b\[[0-9;]*m','',(p/'device-latest.log').read_text(errors='replace'))
 marker='PROTO STARTUP BEGIN target=600 voltage=1200; warmup=300 coldest>=40C';raw=raw[raw.rfind(marker):]
 d=json.loads(final.read_text());diag=d['diagnostics'];summary=json.loads((p/'domain-summary.json').read_text());eff=json.loads((p/'core-efficiency-summary.json').read_text())
 pll=raw[raw.rfind('PROTO STARTUP PLL frequency=600'):]
 thermal=[{'attempt':int(a),'asic':int(c),'valid':int(v)} for a,c,v in re.findall(r'THERMAL attempt=(\d+) A(\d+).*?valid=(\d+)',pll)]
 q=[x for x in summary['qualifications'] if x['MHz']==600]
 chips=diag['chips'];valid=bool(chips) and len(chips)==4 and all(c['valid']==1 and len(c['corePassCounts'])==156 for c in chips)
 result={'attempt':p.name,'outcome':'passed' if (p/'final.json').exists() else 'thermal_abort_before_work' if not q and 'THERMAL invalid' in pll else 'qualification_failed' if q else 'other_startup_failure','first_temperatures':summary['first_temperatures'],'pre_work_temperatures':summary['first_600_temperatures'],'qualifications_600':q,'post_ramp_thermal_reads':thermal,'active_domains':sum(sum(c['corePassCounts'][39*i:39*(i+1)])>0 for c in chips for i in range(4)) if valid else None,'active_cores':sum(v>0 for c in chips for v in c['corePassCounts']) if valid else None,'sampled_peak_halfstack_mv':summary['sampled_max_halfstack_mv'],'final_info':d['info'],'efficiency':eff,'hold_seconds':json.loads((p/'hold.json').read_text())['pool_seconds'] if (p/'hold.json').exists() else None,'mean_counter_ghs':summary['mean_counter_ghs']}
 (p/'result.json').write_text(json.dumps(result,indent=2)+'\n');results.append(result)
 print(json.dumps({k:result[k] for k in ['attempt','outcome','first_temperatures','pre_work_temperatures','qualifications_600','active_domains','active_cores','sampled_peak_halfstack_mv','hold_seconds','mean_counter_ghs']}))
(root/'results.json').write_text(json.dumps(results,indent=2)+'\n')
