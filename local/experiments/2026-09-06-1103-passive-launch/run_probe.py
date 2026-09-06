"""Bounded cold-start passive counter comparison; app-only flash, sanitized API log."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,threading,time,urllib.request
import serial
p=Path(__file__).resolve().parent;root=p.parents[2]
a=argparse.ArgumentParser();a.add_argument('--flash',action='store_true');a.add_argument('--run',default='run-01');args=a.parse_args()
out=p/args.run;out.mkdir(exist_ok=False)
url='http://192.168.30.9';portname='/dev/cu.usbmodem11434201'
keys=['macAddr','boardVersion','version','uptimeSeconds','frequency','actualFrequency','coreVoltage','regulatorVoltage','domainVoltages','asicTemps','temp','vrTemp','miningPaused','hardware_fault','power','fanspeed','fanrpm','minFanSpeed','autofanspeed','manualFanSpeed']
def request(path,method='GET',data=None):
 req=urllib.request.Request(url+path,method=method,data=None if data is None else json.dumps(data).encode(),headers={'Content-Type':'application/json'})
 with urllib.request.urlopen(req,timeout=5) as r:
  b=r.read();return json.loads(b) if b else None
s=request('/api/system/info');assert s['macAddr'].upper()=='28:84:85:BA:96:08' and str(s['boardVersion'])=='1103'
assert s['coreVoltage']==1160 and s['frequency']==600 and s['regulatorVoltage']<100
(out/'before-settings.json').write_text(json.dumps({k:s.get(k) for k in keys},indent=2))
state={'completed':False,'error':None,'elf_prefix':None};stop=threading.Event();start=time.monotonic()
def capture(port):
 with (out/'serial.log').open('w',buffering=1) as f:
  while not stop.is_set():
   try:raw=port.readline()
   except Exception as e:state['error']=str(e);break
   line=re.sub(r'\x1b\[[0-9;]*m','',raw.decode(errors='replace'));f.write(line)
   if 'ELF file SHA256:' in line:state['elf_prefix']=line.split('ELF file SHA256:',1)[1].strip().rstrip('.')
   if 'PASSIVE END completed=1' in line:state['completed']=True
   if 'PASSIVE END completed=0' in line or 'Guru Meditation' in line:state['error']=line.strip()
   if 'PROTO STARTUP FAILED' in line and not state['completed']:state['error']=line.strip()
   if 'PASSIVE ' in line and ' COUNT ' not in line and ' PLL ' not in line or 'PROTO STARTUP ' in line and not any(x in line for x in [' TEMP ',' CHIP ',' DOMAIN ']):print(line.strip(),flush=True)
try:
 if args.flash:
  binary=root/'build/esp-miner.bin';elf=root/'build/esp-miner.elf'
  for source in [binary,elf]:
   (out/source.name).write_bytes(source.read_bytes())
  (out/'firmware-identity.json').write_text(json.dumps({f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in [binary,elf]},indent=2))
  for source in ['main/power/asic_init.c','components/asic/mc3.c']:(out/(Path(source).stem+'-source.c')).write_text((root/source).read_text())
  with (out/'flash.log').open('w') as f:
   subprocess.run(['/Users/skot/.espressif/python_env/idf5.5_py3.9_env/bin/python','-m','esptool','--chip','esp32s3','--port',portname,'--baud','460800','--before','default_reset','--after','hard_reset','write_flash','0x10000',str(binary)],stdout=f,stderr=subprocess.STDOUT,check=True)
  print('Application flashed; capturing cold startup',flush=True)
 with serial.Serial(portname,115200,timeout=.2) as port:
  th=threading.Thread(target=capture,args=(port,),daemon=True);th.start()
  if not args.flash:request('/api/system/restart','POST')
  with (out/'api-samples.jsonl').open('w',buffering=1) as f:
   while time.monotonic()-start<900:
    if state['error']:raise RuntimeError(state['error'])
    if state['completed']:break
    try:s=request('/api/system/info')
    except Exception:time.sleep(3);continue
    assert s['macAddr'].upper()=='28:84:85:BA:96:08' and str(s['boardVersion'])=='1103'
    f.write(json.dumps({'host_seconds':time.monotonic()-start,**{k:s.get(k) for k in keys}})+'\n')
    if s.get('hardware_fault') or max(s.get('asicTemps') or [0])>=70:raise RuntimeError('Hardware/thermal guard')
    time.sleep(3)
   else:raise RuntimeError('900-second probe deadline')
  time.sleep(2);stop.set();th.join(2)
except BaseException as e:
 state['error']=str(e);print('TEST STOP:',str(e),flush=True)
finally:
 stop.set()
 try:
  request('/api/system/pause','POST')
  # User requested maximum fan while ASICs are off to prepare cold comparisons.
  request('/api/system','PATCH',{'autofanspeed':0,'manualFanSpeed':100})
  time.sleep(2);s=request('/api/system/info')
  state['final_state']={k:s.get(k) for k in keys}
  state['asic_power_off']=s['regulatorVoltage']<100
 except Exception as e:state['cleanup_error']=str(e)
 state['elapsed_seconds']=time.monotonic()-start
 (out/'result.json').write_text(json.dumps(state,indent=2))
 print(json.dumps(state,indent=2),flush=True)
 if not state['completed'] or state['error']:raise SystemExit(1)
