import json,urllib.request,time,threading,re,socket,serial,vxi11,os,math
from pathlib import Path
import numpy as np
p=Path(__file__).resolve().parent;url='http://192.168.30.193';stop=threading.Event();lines=[];samples=[];phase='initial';healthy=False;scope=None

def api(path='/api/system/info',method='GET',body=None):
 data=json.dumps(body).encode() if body is not None else (b'' if method=='POST' else None)
 req=urllib.request.Request(url+path,data=data,method=method,headers={'Content-Type':'application/json'})
 with urllib.request.urlopen(req,timeout=4) as r:
  b=r.read()
  return json.loads(b) if b else None

def sample():
 a=api();row={'host_monotonic':time.monotonic(),'phase':phase,'info':a};samples.append(row)
 with (p/'api-samples.jsonl').open('a') as f:f.write(json.dumps(row)+'\n')
 assert a['boardVersion']=='1102' and a['version']=='v2.14.0-40-gcd82a9e-dirty'
 temps=a.get('asicTemps',[])
 if temps and (not all(isinstance(t,(int,float)) and math.isfinite(t) and 0<t<70 for t in temps)):raise RuntimeError('Invalid/high reported temperature')
 if a.get('hardwareFault',False):raise RuntimeError('Hardware fault reported')
 print('STATE',phase,'uptime',a.get('uptimeSeconds'),'MHz',a.get('actualFrequency'),'GH/s',a.get('hashRate'),'temps',temps,'fan',a.get('fanspeed'),flush=True)
 return a

def soft_sample():
 try:return sample()
 except (urllib.error.URLError,TimeoutError,ConnectionError) as e:
  print('API pending',str(e),flush=True);return None

def read_serial(port):
 with (p/'serial.log').open('w',buffering=1) as f:
  while not stop.is_set():
   line=re.sub(r'\x1b\[[0-9;]*m','',port.readline().decode(errors='replace'));f.write(line);lines.append((time.monotonic(),line))
   if any(x in line for x in ['SPDLOG:', 'App version:', 'PLL0 locked at 600', 'ASIC initialized successfully','Restarting System']):print(line,end='',flush=True)

def capture():
 folder=p/'warm-reboot';folder.mkdir(exist_ok=True);pre={};arrays={}
 for c in ['CHAN1','CHAN2']:
  for cmd in [f':WAV:SOUR {c}',':WAV:MODE RAW',':WAV:FORM BYTE',':WAV:POIN 700000']:scope.write(cmd)
  pre[c]=scope.ask(':WAV:PRE?');assert int(pre[c].split(',')[2])==700000
  arr=np.empty(700000,np.uint8)
  for first in range(1,700001,250000):
   last=min(first+249999,700000)
   for cmd in [f':WAV:STAR {first}',f':WAV:STOP {last}']:scope.write(cmd)
   b=scope.ask_raw(b':WAV:DATA?');assert b[:1]==b'#';n=int(chr(b[1]));length=int(b[2:2+n]);payload=b[2+n:2+n+length];assert len(payload)==last-first+1
   arr[first-1:last]=np.frombuffer(payload,np.uint8)
  arrays[c]=arr
 (folder/'preambles.json').write_text(json.dumps(pre,indent=2));np.savez_compressed(folder/'raw.npz',**arrays)
 audit={}
 for c in arrays:
  pp=np.array(pre[c].split(','),float);a=arrays[c];v=(a.astype(float)-pp[8]-pp[9])*pp[7]
  audit[c]={'clipped':int(((a==0)|(a==255)).sum()),'min':float(v.min()),'max':float(v.max()),'start_s':float(pp[5]-pp[6]*pp[4]),'end_s':float((len(a)-1-pp[6])*pp[4]+pp[5]),'sample_interval_s':float(pp[4])}
 (folder/'audit.json').write_text(json.dumps(audit,indent=2));print('CAPTURE',audit,flush=True)
 assert scope.ask(':CHAN1:IMP?')=='FIFT'

port=serial.Serial(os.environ['BITAXE_BENCH_PORT'],115200,timeout=.2);reader=threading.Thread(target=read_serial,args=(port,),daemon=True);reader.start()
try:
 deadline=time.monotonic()+90;a=None
 while time.monotonic()<deadline:
  a=soft_sample()
  if a and len(a.get('asicTemps',[]))==4:break
  time.sleep(2)
 if not a or len(a.get('asicTemps',[]))!=4:raise RuntimeError('No four-ASIC telemetry after boot')
 (p/'initial-api-state.json').write_text(json.dumps(a,indent=2))
 phase='warmup';warm_since=None;deadline=time.monotonic()+600
 while time.monotonic()<deadline:
  a=sample();t=a.get('asicTemps',[])
  if len(t)==4 and max(t)>=54.5 and min(t)>=52:
   warm_since=warm_since or time.monotonic()
   if time.monotonic()-warm_since>=10:break
  else:warm_since=None
  time.sleep(2)
 else:raise RuntimeError('Warmup target timeout')
 scope=vxi11.Instrument('192.168.1.250');scope.timeout=10;scope.open();scope.client.sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1);scope.max_recv_size=262144
 assert scope.ask(':CHAN1:IMP?')=='FIFT'
 for cmd in [':STOP',':TIM:MODE MAIN',':TIM:SCAL 5',':TIM:OFFS 25',':ACQ:MDEP 700000',':ACQ:TYPE NORM',':CHAN1:SCAL 0.5',':CHAN1:OFFS -1.16',':CHAN2:SCAL 0.5',':CHAN2:OFFS -1.16',':TRIG:MODE EDGE',':TRIG:EDGE:SOUR CHAN2',':TRIG:EDGE:SLOP POS',':TRIG:EDGE:LEV 2.0',':SINGle']:scope.write(cmd)
 phase='scope_prefill'
 for i in range(8):sample();time.sleep(2)
 assert scope.ask(':TRIG:STAT?')=='WAIT'
 settings={q:scope.ask(q) for q in [':CHAN1:IMP?',':CHAN2:IMP?',':TIM:SCAL?',':TIM:OFFS?',':ACQ:SRAT?',':ACQ:MDEP?',':CHAN1:BWLimit?',':CHAN2:BWLimit?']};(p/'scope-settings.json').write_text(json.dumps(settings,indent=2))
 (p/'before-reboot-api.json').write_text(json.dumps(sample(),indent=2))
 phase='warm_reboot';force_before=time.monotonic();scope.write(':TFORce');force_after=time.monotonic();print('WARM RESTART requested',flush=True);response=api('/api/system/restart','POST');restart_at=time.monotonic()
 (p/'restart-command.json').write_text(json.dumps({'force_before':force_before,'force_after':force_after,'restart_response_at':restart_at,'response':response},indent=2))
 deadline=time.monotonic()+90;post_start=None
 while time.monotonic()<deadline:
  a=soft_sample()
  if a and a.get('uptimeSeconds',9999)<90 and len(a.get('asicTemps',[]))==4 and a.get('actualFrequency')==600:post_start=post_start or time.monotonic()
  if scope.ask(':TRIG:STAT?')=='STOP':break
  time.sleep(2)
 else:raise RuntimeError('Scope acquisition timeout')
 capture();phase='post_reboot_hold';deadline=(post_start or time.monotonic())+120
 while time.monotonic()<deadline:sample();time.sleep(2)
 a=sample();(p/'final-api-state.json').write_text(json.dumps(a,indent=2))
 rates=[]
 for when,line in lines:
  if when<time.monotonic()-35:continue
  if 'SPDLOG:' in line:
   chips=re.findall(r'chip(\d)=(\d+\.\d+) GH/s',line)
   if len(chips)==4:rates.append([float(v) for _,v in chips])
 means=np.mean(rates,axis=0).tolist() if rates else []
 healthy=len(rates)>=5 and all(v>=720 for v in means) and sum(means)>=2900 and a.get('actualFrequency')==600 and len(a.get('asicTemps',[]))==4
 result={'healthy':bool(healthy),'last35s_windows':len(rates),'mean_chip_ghs':means,'mean_total_ghs':sum(means),'temperatures':a.get('asicTemps'),'final_frequency':a.get('actualFrequency')};(p/'result.json').write_text(json.dumps(result,indent=2));print('RESULT',result,flush=True)
finally:
 if not healthy:
  try:print('PAUSE',api('/api/system/pause','POST'),flush=True)
  except Exception as e:print('PAUSE FAILED',str(e),flush=True)
 if scope:
  scope.write(':STOP');(p/'final-scope-state.json').write_text(json.dumps({q:scope.ask(q) for q in [':TRIG:STAT?',':CHAN1:IMP?',':CHAN2:IMP?']},indent=2));scope.close()
 time.sleep(2);stop.set();reader.join(2);port.close()
