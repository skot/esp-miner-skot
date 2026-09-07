import json,time,threading,urllib.request,serial,re
from pathlib import Path
p=Path(__file__).resolve().parent
stop=threading.Event()
def capture():
 port=serial.Serial(port=None,baudrate=115200,timeout=.2)
 port.dtr=False;port.rts=False;port.port='/dev/cu.usbmodem11434201';port.open()
 with port, (p/'serial.log').open('w') as out:
  while not stop.is_set():
   line=port.readline().decode(errors='replace');out.write(line);out.flush()
   if 'PROTO STARTUP QUALIFY' in line or 'PROTO STARTUP READY' in line or 'PROTO STARTUP FAILED' in line or 'Guru Meditation' in line: print(re.sub(r'\x1b\[[0-9;]*m','',line).strip(),flush=True)
def get(path):
 with urllib.request.urlopen('http://192.168.30.9'+path,timeout=6) as r: return json.load(r)
t=threading.Thread(target=capture,daemon=True);t.start();start=time.monotonic();finished=None;last=None;lastprint=0
try:
 with (p/'diagnostic-samples.jsonl').open('w') as out:
  while time.monotonic()-start<1100:
   try:
    d=get('/api/system/diagnostics');s=get('/api/system/info')
    assert s['macAddr']=='28:84:85:BA:96:08'
    safe={k:s.get(k) for k in ['version','axeOSVersion','regulatorVoltage','actualFrequency','asicTemps','hashRate','hashrateMonitor','sharesAccepted','sharesRejected','freeHeap','freeHeapInternal','miningPaused','power','temp','temp2','fanspeed','fanrpm','autofanspeed','frequency','coreVoltage','uptimeSeconds','boardVersion','macAddr']}
    out.write(json.dumps({'elapsed':time.monotonic()-start,'diagnostics':d,'info':safe})+'\n');out.flush()
    status=(d['source'],d['startupResult'],d['status'])
    if status!=last or time.monotonic()-lastprint>30:
     print(json.dumps({'status':status,'chips':len(d['chips']),'temps':safe['asicTemps'],'hashRate':safe['hashRate'],'accepted':safe['sharesAccepted'],'rejected':safe['sharesRejected']}),flush=True);last=status;lastprint=time.monotonic()
    if d['startupResult'] in ['passed','failed']:
     if finished is None: finished=time.monotonic()
     if time.monotonic()-finished>(300 if d['startupResult']=='passed' else 35):
      (p/'final-diagnostics.json').write_text(json.dumps(d,indent=2)+'\n');(p/'final-state.json').write_text(json.dumps(safe,indent=2)+'\n');break
   except Exception as e: print(type(e).__name__,str(e),flush=True)
   time.sleep(5)
finally:
 stop.set();t.join(timeout=2)
 (p/'monitor-status.json').write_text(json.dumps({'elapsed_seconds':time.monotonic()-start,'startup_result_seen':last,'monitor_stopped':True},indent=2)+'\n')
