from pathlib import Path
import json,re
p=Path(__file__).resolve().parent
for run in sorted(p.glob('run-*')):
 if not run.is_dir():continue
 print(run.name)
 f=run/'result.json'
 if f.exists():
  r=json.loads(f.read_text());print({k:r.get(k) for k in ['success','error','hold_seconds','mean_api_ghs','mean_spd_ghs','accepted_during_hold','rejected_during_hold','max_asic_c']});continue
 f=run/'api-samples.jsonl'
 if f.exists():
  rows=f.read_text().splitlines()
  if rows:
   r=json.loads(rows[-1]);print({k:r.get(k) for k in ['host_seconds','actualFrequency','hashRate','asicTemps','sharesAccepted','sharesRejected','regulatorVoltage','domainVoltages','hardware_fault']})
 f=run/'serial.log'
 if f.exists():
  s=f.read_text();selected=[l for l in s.splitlines() if 'PROTO STARTUP ' in l and 'DOMAIN' not in l and ' CHIP ' not in l];print('\n'.join(selected[-3:]))
  rates=[float(v) for v in re.findall(r'SPDLOG:.*?total=([\d.]+) GH/s',s)]
  if rates:print('SPD windows:',len(rates),'last:',rates[-1],'last20 mean:',round(sum(rates[-20:])/len(rates[-20:]),2))
