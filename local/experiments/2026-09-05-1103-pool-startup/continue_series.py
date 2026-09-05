"""Continue the authorized finite test series even if the desktop chat disconnects."""
from pathlib import Path
import hashlib,json,subprocess,sys,time,urllib.request
p=Path(__file__).resolve().parent
url='http://192.168.30.9'
state={'stage':'waiting_for_run_01','success':False}
def save():
 (p/'series-status.json').write_text(json.dumps(state,indent=2));print(json.dumps(state),flush=True)
def request(path,method='GET'):
 with urllib.request.urlopen(urllib.request.Request(url+path,method=method),timeout=5) as r:
  data=r.read();return json.loads(data) if data else None
def identity():
 s=request('/api/system/info');assert s['macAddr'].upper()=='28:84:85:BA:96:08' and str(s['boardVersion'])=='1103','Wrong board';return s
def run(name,*extra):
 identity();state['stage']=name;save()
 with (p/(name+'-supervisor.log')).open('w') as f:
  subprocess.run([sys.executable,str(p/'monitor_pool.py'),'--run',name,'--seconds','300',*extra],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=1400)
 assert json.loads((p/name/'result.json').read_text())['success']
try:
 save();deadline=time.monotonic()+4000
 result=p/'run-01-cold/result.json'
 while not result.exists():
  if time.monotonic()>deadline:raise RuntimeError('Initial hold did not finish by series deadline')
  time.sleep(5)
 assert json.loads(result.read_text())['success'],'Initial long run failed; no further automatic tests'
 # The first supervisor closes USB before writing result.json.
 identity();state['stage']='flash_handoff_build';save()
 request('/api/system/pause','POST')
 for _ in range(20):
  time.sleep(.5)
  s=identity()
  if s['regulatorVoltage']<=100:break
 else:raise RuntimeError('ASIC rail did not discharge before flash')
 firmware=p/'handoff-build/firmware.bin'
 assert hashlib.sha256(firmware.read_bytes()).hexdigest()=='7f37958ed8e9d631b1049090d49730dae68118bc02f54d2b7b32bcfe0ed01b9f'
 with (p/'flash-handoff.log').open('w') as f:
  subprocess.run(['/Users/skot/.espressif/python_env/idf5.5_py3.9_env/bin/python','-m','esptool','--chip','esp32s3','--port','/dev/cu.usbmodem11434201','--baud','460800','--before','default_reset','--after','hard_reset','write_flash','0x10000',str(firmware)],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
 # Attach immediately after flashing, before startup milestones.
 state['stage']='run-02-handoff-flash';save()
 with (p/'run-02-handoff-flash-supervisor.log').open('w') as f:
  subprocess.run([sys.executable,str(p/'monitor_pool.py'),'--run','run-02-handoff-flash','--seconds','300'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=1400)
 assert json.loads((p/'run-02-handoff-flash/result.json').read_text())['success']
 run('run-03-warm-reboot','--restart')
 identity();state['stage']='cooldown_asic_power_off_120s';save();request('/api/system/pause','POST')
 cooldown=[]
 for _ in range(24):
  time.sleep(5);s=identity()
  if s['regulatorVoltage']>100 or not s['miningPaused']:raise RuntimeError('ASIC rail stayed powered during cooldown')
  cooldown.append({k:s.get(k) for k in ['regulatorVoltage','miningPaused','fanspeed','fanrpm','asicTemps']})
 (p/'cooldown.json').write_text(json.dumps(cooldown,indent=2))
 run('run-04-cooled-resume','--resume')
 state.update(stage='complete',success=True);save()
 subprocess.run([sys.executable,str(p/'analyze_pool.py')],check=True)
except BaseException as e:
 state.update(stage='stopped',error=str(e));save()
 try:identity();request('/api/system/pause','POST')
 except Exception:pass
 raise
