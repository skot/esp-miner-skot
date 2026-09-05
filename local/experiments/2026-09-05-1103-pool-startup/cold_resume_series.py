"""Flash paused-fan refinement, cool at 100%, then test the authorized resume."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,time,urllib.request,threading,serial

ap=argparse.ArgumentParser()
ap.add_argument('--skip-flash',action='store_true')
ap.add_argument('--build',default='paused-fan-build')
ap.add_argument('--run',default='run-04-cooled-resume')
args=ap.parse_args()

p=Path(__file__).resolve().parent
url='http://192.168.30.9/api/system'
state={'stage':'preparing_cold_resume','success':False}
capture_stop=threading.Event();boot_finished=threading.Event();capture_thread=None;port=None
def capture_boot():
 with (p/(args.run+'-preparation-serial.log')).open('w') as f:
  while not capture_stop.is_set():
   line=port.readline().decode(errors='replace')
   if line:f.write(line);f.flush()
   if 'Proto ASIC startup incomplete; keeping pool tasks available for resume' in line:boot_finished.set()
def close_capture():
 capture_stop.set()
 if capture_thread:capture_thread.join(timeout=2)
 if port:port.close()
def save():
 (p/'series-status.json').write_text(json.dumps(state,indent=2));print(json.dumps(state),flush=True)
def request(path='',method='GET',data=None):
 req=urllib.request.Request(url+path,method=method,data=json.dumps(data).encode() if data is not None else None,headers={'Content-Type':'application/json'})
 with urllib.request.urlopen(req,timeout=5) as r:
  b=r.read();return json.loads(b) if b else None
def identity():
 s=request('/info');assert s['macAddr'].upper()=='28:84:85:BA:96:08' and str(s['boardVersion'])=='1103';return s
try:
 assert identity()['miningPaused']
 assert identity()['regulatorVoltage']<=100
 build=p/args.build;firmware=build/'firmware.bin'
 assert hashlib.sha256(firmware.read_bytes()).hexdigest()==json.loads((build/'sha256.json').read_text())['firmware.bin']
 state['stage']='flash_paused_fan_build';save()
 if not args.skip_flash:
  with (p/('flash-'+args.build+'.log')).open('w') as f:
   subprocess.run(['/Users/skot/.espressif/python_env/idf5.5_py3.9_env/bin/python','-m','esptool','--chip','esp32s3','--port','/dev/cu.usbmodem11434201','--baud','460800','--before','default_reset','--after','hard_reset','write_flash','0x10000',str(firmware)],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
 port=serial.Serial('/dev/cu.usbmodem11434201',115200,timeout=.2)
 capture_thread=threading.Thread(target=capture_boot,daemon=True);capture_thread.start()
 # Pause as soon as the API is available, before completing the automatic startup.
 deadline=time.monotonic()+60
 while True:
  try:
   identity();request('/pause','POST');request(method='PATCH',data={'autofanspeed':0,'manualFanSpeed':100});break
  except Exception:
   if time.monotonic()>deadline:raise
   time.sleep(1)
 deadline=time.monotonic()+120
 stable_since=None
 while True:
  s=identity()
  # The API starts before boot ASIC initialization finishes. Its initial off
  # reading is not proof that the requested pause has been processed yet.
  if (boot_finished.is_set() or s['uptimeSeconds']>=90) and s['miningPaused'] and s['regulatorVoltage']<=100 and s['fanspeed']==100:
   if stable_since is None:stable_since=time.monotonic()
   if time.monotonic()-stable_since>=10:break
  else:stable_since=None
  if time.monotonic()>deadline:raise RuntimeError('Full-fan ASIC-off cooldown was not established')
  time.sleep(1)
 state['stage']='cooldown_100_percent_fan_180s';save();samples=[]
 for _ in range(36):
  time.sleep(5);s=identity()
  assert s['miningPaused'] and s['regulatorVoltage']<=100 and s['fanspeed']==100,'Cooldown conditions changed'
  samples.append({k:s.get(k) for k in ['uptimeSeconds','regulatorVoltage','miningPaused','fanspeed','fanrpm','asicTemps','autofanspeed','manualFanSpeed']})
  (p/(args.run+'-cooldown-full-fan.json')).write_text(json.dumps(samples,indent=2))
 # Restore the original automatic setting for the unchanged warmup/mining test.
 original=json.loads((p/'before-cold-fan-settings.json').read_text())
 request(method='PATCH',data={k:original[k] for k in ['autofanspeed','manualFanSpeed']})
 close_capture()
 state['stage']=args.run;save()
 with (p/(args.run+'-supervisor.log')).open('w') as f:
  subprocess.run([sys.executable,str(p/'monitor_pool.py'),'--run',args.run,'--seconds','300','--resume'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=1400)
 assert json.loads((p/args.run/'result.json').read_text())['success']
 state.update(stage='complete',success=True);save()
 subprocess.run([sys.executable,str(p/'analyze_pool.py')],check=True)
except BaseException as e:
 close_capture()
 state.update(stage='stopped',error=str(e));save()
 try:identity();request('/pause','POST')
 except Exception:pass
 raise
