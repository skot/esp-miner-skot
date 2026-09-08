from pathlib import Path
import json,re
p=Path(__file__).resolve().parent
raw=re.sub(r'\x1b\[[0-9;]*m','',(p/'device-all.log').read_text(errors='replace'))
marker='PROTO STARTUP BOOST warmup_mv=1160 ramp_mv=1200 mining_mv=1200; PVT reinit then pool work'
start=raw.rfind(marker)
assert start>=0,'Current firmware marker missing'
current=raw[start:]
ready=current.find("PROTO STARTUP READY")
if ready>=0:current=current[:ready]
events=[line for line in current.splitlines() if any(x in line for x in ['PROTO STARTUP','PVT_1200','PVT_REINIT','VOLT_DIAG','TEMP_INVALID'])]
(p/'startup-events.txt').write_text('\n'.join(events)+'\n')
temperatures=[];voltage=[];phase=None
for line in events:
 m=re.search(r'PVT_1200 TEMP phase=(\w+) A(\d) valid=(\d) temperature=(\S+)',line)
 if m:
  stage,asic,valid,temp=m.groups();temperatures.append({'phase':stage,'asic':int(asic),'valid':bool(int(valid)),'temperature_c':float(temp) if int(valid) else None})
 m=re.search(r'PVT_1200 VOLTAGE phase=(\w+)',line)
 if m:phase=m[1]
 m=re.search(r'VOLT_DIAG phase=after A(\d).*?done=(0x\w+).*?status=(0x\w+).*?raw13=(0x\w+).*?raw14=(0x\w+).*?completed=(\d+) fresh=(\d) decoded_mv=([\d.-]+)',line)
 if m:
  a,done,status,r13,r14,completed,fresh,mv=m.groups();voltage.append({'phase':phase,'asic':int(a),'done':done,'status':status,'raw13':r13,'raw14':r14,'completed':int(completed),'fresh':bool(int(fresh)),'decoded_mv':float(mv) if int(fresh) else None})
rail=re.search(r'PVT_1200 RAIL requested_half_mv=1200 measured_total_mv=(\d+)',current)
result={'complete':'PVT_1200 END' in current,'frequency_mhz':600,'requested_half_mv':1200,'regulator_total_mv':int(rail[1]) if rail else None,'first_600_work_sent':'PROTO STARTUP FIRST_WORK' in current,'temperatures':temperatures,'voltages':voltage,'pvt_readback':[l for l in events if 'PVT_REINIT A' in l],'qualification':re.findall(r'PROTO STARTUP QUALIFY frequency=(\d+) total=([\d.]+) result=(\w+)',current),'first_temperatures':next((l for l in events if 'PROTO STARTUP TEMP' in l),None)}
(p/'sensor-result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
