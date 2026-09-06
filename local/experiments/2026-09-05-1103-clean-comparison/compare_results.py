"""Extract matched startup timing, qualification and pool outcomes from logs."""
from pathlib import Path
import json,re

p=Path(__file__).resolve().parent
out=[]
for run in sorted(p.glob('run-*')):
 if not run.is_dir() or not (run/'serial.log').exists():continue
 log=(run/'serial.log').read_text()
 def event(text):
  m=re.search(r'\((\d+)\)[^\n]*'+re.escape(text),log)
  return int(m[1]) if m else None
 def elapsed(a,b):
  return round((b-a)/1000,3) if a is not None and b is not None else None
 temps=[]
 for m in re.finditer(r'\((\d+)\).*PROTO STARTUP TEMP freq=(\d+) A1=([\d.]+) A2=([\d.]+) A3=([\d.]+) A4=([\d.]+)',log):
  temps.append({'boot_ms':int(m[1]),'frequency':int(m[2]),'c':list(map(float,m.groups()[2:]))})
 pll=[{'boot_ms':int(m[1]),'mhz':int(m[2]),'settle_ms':int(m[3])} for m in re.finditer(r'\((\d+)\).*PROTO STARTUP PLL frequency=(\d+) settle_ms=(\d+)',log)]
 start=event('Starting ASIC initialization')
 reinit=event('PROTO STARTUP REINITIALIZED')
 first=event('PROTO STARTUP FIRST_WORK')
 ready=event('PROTO STARTUP READY')
 domain_lines=[];chips=[];final_domains=[];final_chips=[];final_qualification=None
 for line in log.splitlines():
  if 'PROTO STARTUP DOMAIN ' in line:domain_lines.append(line)
  if 'PROTO STARTUP CHIP ' in line:chips.append(line)
  if 'PROTO STARTUP QUALIFY ' in line:
   if 'frequency=600 ' in line:
    final_qualification=line;final_domains=domain_lines;final_chips=chips
   domain_lines=[];chips=[]
 item={'run':run.name,'initialization_to_ready_seconds':elapsed(start,ready),
  'reinitialized_to_first_work_seconds':elapsed(reinit,first),
  'final_pll_to_first_work_seconds':elapsed(pll[-1]['boot_ms'] if pll else None,first),
  'first_fresh_temperatures_c':temps[0]['c'] if temps else None,
  'last_warmup_temperatures_c':next((t['c'] for t in reversed(temps) if t['frequency']==300),None),
  'pre_first_work_temperatures_c':next((t['c'] for t in reversed(temps) if first and t['boot_ms']<=first and t['frequency']==600),None),
  'pll_steps':pll,'final_qualification':final_qualification,'final_chips':final_chips,'final_domains':final_domains}
 if reinit is not None and first is not None:
  lines=[line for line in log.splitlines() if (m:=re.search(r'\((\d+)\)',line)) and reinit<=int(m[1])<first]
  item['logged_pool_work_writes_during_unloaded_ramp']=sum('Writing MC3 work job_id=' in line for line in lines)
 if (run/'result.json').exists():
  result=json.loads((run/'result.json').read_text())
  item['pool_hold']={k:result.get(k) for k in ['success','error','hold_seconds','mean_spd_ghs','accepted_during_hold','rejected_during_hold','max_asic_c','elf_sha256_prefix']}
 out.append(item)
(p/'comparison-results.json').write_text(json.dumps(out,indent=2))
for r in out:print({k:v for k,v in r.items() if k not in ['pll_steps','final_domains','final_chips']})
