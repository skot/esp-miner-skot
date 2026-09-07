"""App-only bench trial. Keeps existing NVS except a temporary fan setting."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,time,urllib.request
p=Path(__file__).resolve().parent;w=Path('/tmp/bitaxe-1103-regression')
a=argparse.ArgumentParser();a.add_argument('name');a.add_argument('--cool-seconds',type=int,default=120);args=a.parse_args()
out=p/args.name;out.mkdir(exist_ok=False)
def api(path='info',data=None):
 req=urllib.request.Request('http://192.168.30.9/api/system'+('/'+path if path else ''),data=json.dumps(data).encode() if data is not None else None,method='PATCH' if data is not None else 'GET',headers={'Content-Type':'application/json'})
 with urllib.request.urlopen(req,timeout=10) as r:
  body=r.read();return json.loads(body) if body else {}
def state():
 a=api();assert a['macAddr'].upper()=='28:84:85:BA:96:08' and str(a['boardVersion'])=='1103'
 return {k:a.get(k) for k in ['boardVersion','macAddr','version','regulatorVoltage','frequency','coreVoltage','fanspeed','fanrpm','autofanspeed','hashRate']}
files={'probe.bin':w/'build/esp-miner.bin','probe.elf':w/'build/esp-miner.elf','asic-init-source.c':w/'main/power/asic_init.c','mc3-source.c':w/'components/asic/mc3.c','proto_compare.h':w/'main/power/proto_compare.h','sdkconfig-snapshot':w/'sdkconfig'}
manifest={}
for name,source in files.items():
 shutil.copyfile(source,out/name);manifest[name]=hashlib.sha256((out/name).read_bytes()).hexdigest()
(out/'firmware-identity.json').write_text(json.dumps({'sha256':manifest,'app_only':True,'scope_used':False,'config':(out/'proto_compare.h').read_text()},indent=2)+'\n')
for f in ['capture_serial.py','analyze_warm.py','analyze_thermal.py','analyze_result.py','status.py']:
 shutil.copyfile(p.parent/'2026-09-05-1103-scale-before-limits'/f,out/f)
initial=state();assert initial['regulatorVoltage']<100,initial
(out/'initial-state.json').write_text(json.dumps(initial,indent=2)+'\n')
api('',{'autofanspeed':0,'fanspeed':100})
for seconds in range(0,args.cool_seconds,30):
 print(f'Cooling: ASIC power off, fan 100%, {seconds}/{args.cool_seconds}s',flush=True)
 time.sleep(min(30,args.cool_seconds-seconds))
cooled=state();assert cooled['regulatorVoltage']<100 and cooled['fanspeed']>=99,cooled
(out/'cooled-state.json').write_text(json.dumps(cooled,indent=2)+'\n')
# Restore the original automatic setting before startup, matching the saved image.
original=json.loads((p/'original-fan-settings.json').read_text())
api('',{'autofanspeed':original['autofanspeed'],'fanspeed':original['fanspeed']})
print('Flashing',args.name,manifest['probe.bin'],flush=True)
with (out/'flash.log').open('w') as log:
 subprocess.run(['/Users/skot/.espressif/python_env/idf5.5_py3.9_env/bin/python','-m','esptool','--chip','esp32s3','--port','/dev/cu.usbmodem11434201','--baud','460800','--before','default_reset','--after','hard_reset','write_flash','0x10000',str(out/'probe.bin')],stdout=log,stderr=subprocess.STDOUT,check=True)
print('Flash verified; recording startup',flush=True)
with (out/'capture.log').open('w') as log:
 result=subprocess.run(['/tmp/bitaxe-bench-env/bin/python','-u',str(out/'capture_serial.py')],env={**os.environ,'BITAXE_BENCH_PORT':'/dev/cu.usbmodem11434201'},stdout=log,stderr=subprocess.STDOUT)
final=state()
for _ in range(10):
 if final['regulatorVoltage']<100:break
 time.sleep(1);final=state()
(out/'final-state.json').write_text(json.dumps(final,indent=2)+'\n')
assert final['regulatorVoltage']<100,final
api('',{'autofanspeed':0,'fanspeed':100})
print('Capture finished; ASIC power off; fan at 100% for next trial',result.returncode,flush=True)
raise SystemExit(result.returncode)
