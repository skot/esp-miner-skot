import json,pathlib,time,urllib.request,urllib.error,hashlib,re,statistics
P=pathlib.Path(__file__).resolve().parent
BASE='http://192.168.30.9'
KEYS=['version','boardVersion','macAddr','actualFrequency','frequency','coreVoltage','regulatorVoltage','hashRate','asicTemps','asicVoltages','domainVoltages','power','uptimeSeconds','sharesAccepted','sharesRejected','miningPaused','fanspeed']
def req(path,data=None,method=None,timeout=15):
 headers={'Content-Type':'application/json'} if method=='PATCH' else {'Content-Type':'application/octet-stream'}
 with urllib.request.urlopen(urllib.request.Request(BASE+path,data=data,method=method,headers=headers),timeout=timeout) as r:return r.read()
def get(path):return json.loads(req(path))
def info():
 s=get('/api/system/info');return {k:s[k] for k in KEYS if k in s}
def save(name,data):(P/name).write_text(json.dumps(data,indent=2)+'\n')
def patch(mv,mhz):req('/api/system',json.dumps({'coreVoltage':mv,'frequency':mhz}).encode(),'PATCH')
def logs():
 data=req('/api/system/logs');(P/'device-latest.log').write_bytes(data)
 with (P/'device-all.log').open('ab') as f:f.write(data+b'\n')
 return re.sub(r'\x1b\[[0-9;]*m','',data.decode(errors='replace'))
start=time.monotonic();results=[];last_print=0
f=(P/'samples.jsonl').open('w')
def sample(phase):
 global last_print
 s=info();d=get('/api/system/diagnostics');row={'phase':phase,'elapsed':time.monotonic()-start,'info':s,'diagnostics':d}
 f.write(json.dumps(row)+'\n');f.flush()
 if time.monotonic()-last_print>25:
  print(json.dumps({'phase':phase,'uptime':s['uptimeSeconds'],'saved':[s['coreVoltage'],s['frequency']],'actual_mhz':s['actualFrequency'],'rail':s['regulatorVoltage'],'ghs':s['hashRate'],'temps':[round(v,1) for v in s['asicTemps']],'status':d['status']}),flush=True);logs();last_print=time.monotonic()
 return row

def startup(phase,before_uptime,saved_mv):
 begin=time.monotonic();ready=None;saw_reboot=False;saw_boost=False
 while time.monotonic()-begin<600:
  try:r=sample(phase)
  except Exception as e:print('Startup:',type(e).__name__,flush=True);time.sleep(5);continue
  s,d=r['info'],r['diagnostics'];saw_reboot|=s['uptimeSeconds']<before_uptime
  if saw_reboot:
   assert s['coreVoltage']==saved_mv and s['frequency']==600
   if d['startupResult']=='failed':raise RuntimeError('Startup failed')
   if d['mining']==1 and d['startupResult']=='passed':
    if ready is None:ready=time.monotonic();saw_boost=s['regulatorVoltage']>2380;save(phase+'-first-mining.json',r)
    if time.monotonic()-ready>40 and abs(s['regulatorVoltage']-2*saved_mv)<10:
     assert saw_boost,'First mining did not retain the 1200 mV startup boost'
     save(phase+'-ready.json',r);return s['uptimeSeconds']
  time.sleep(4)
 raise RuntimeError('Startup/voltage settling timeout')

def hold(name,mv,mhz,seconds=60):
 patch(mv,mhz);begin=time.monotonic();settled=None;windows=[];seen=set();previous_uptime=None;bad=0
 while time.monotonic()-begin<240:
  r=sample(name);s,d=r['info'],r['diagnostics']
  if previous_uptime is not None and s['uptimeSeconds']<previous_uptime:raise RuntimeError('Unexpected ESP restart')
  previous_uptime=s['uptimeSeconds']
  if d['mining']!=1 or d['startupResult']!='passed':raise RuntimeError('Mining stopped during '+name)
  if len(s['asicTemps'])!=4 or any(t<=0 or t>=70 for t in s['asicTemps']):raise RuntimeError('Invalid/hot PVT')
  at_target=s['coreVoltage']==mv and s['frequency']==mhz and s['actualFrequency']==mhz and abs(s['regulatorVoltage']-2*mv)<10
  if at_target:
   if settled is None:settled=time.monotonic()
  else:settled=None
  if settled and time.monotonic()-settled>12:
   cs=d['chips']
   if d['source']=='mining' and d['epoch'] not in seen and len(cs)==4 and all(c['valid']==1 and c['windowSeconds']>0 and c['ageSeconds']<15 and c['frequency']==mhz and len(c['corePassCounts'])==156 for c in cs):
    seen.add(d['epoch']);domains=[sum(c['corePassCounts'][39*i:39*(i+1)])*2**d['leadingZeros']/c['windowSeconds']/1e9 for c in cs for i in range(4)]
    zeros=[(c['chipId']+1,i) for c in cs for i,v in enumerate(c['corePassCounts']) if v==0]
    rate=sum(domains);bad=bad+1 if min(domains)<mhz*.312*.9 or any(z!=(1,105) for z in zeros) else 0
    windows.append({'ghs':rate,'domains':domains,'zero_cores':zeros,'power':s['power'],'epoch':d['epoch']})
    if bad>=3:raise RuntimeError('Persistent domain/core failure: '+name)
   if time.monotonic()-settled>=seconds:
    assert len(windows)>=5
    result={'phase':name,'millivolts':mv,'mhz':mhz,'settled_seconds':time.monotonic()-settled,'epochs':len(windows),'mean_ghs':statistics.mean(w['ghs'] for w in windows),'mean_power_w':statistics.mean(w['power'] for w in windows),'zero_core_union':sorted(set(tuple(z) for w in windows for z in w['zero_cores'])),'uptime':s['uptimeSeconds']}
    results.append(result);save('results.json',results);save(name+'-epochs.json',windows);print('PASS',json.dumps(result),flush=True);return
  time.sleep(4)
 raise RuntimeError('Live setting failed to settle: '+name)

try:
 before=info();save('before.json',before)
 assert before['macAddr']=='28:84:85:BA:96:08' and before['boardVersion']=='1103'
 assert before['coreVoltage']==1200 and before['frequency']==600
 manifest={}
 for filename,endpoint in [('www.bin','OTAWWW'),('esp-miner.bin','OTA')]:
  data=pathlib.Path('build',filename).read_bytes();(P/filename).write_bytes(data);manifest[filename]={'sha256':hashlib.sha256(data).hexdigest(),'size':len(data)};save('manifest.json',manifest)

  if filename=='www.bin' and (P/'first-attempt'/'manifest.json').exists() and json.loads((P/'first-attempt'/'manifest.json').read_text())[filename]['sha256']==manifest[filename]['sha256']:
   print('Keeping identical installed web interface',flush=True);continue
  print('Uploading',filename,len(data),flush=True);print(req('/api/system/'+endpoint,data,'POST',240).decode().strip(),flush=True)
 startup('ota-startup',before['uptimeSeconds'],1200)
 # Rejected requests must not partially save a voltage setting.
 try:patch(1180,625)
 except urllib.error.HTTPError as e:assert e.code==400
 else:raise AssertionError('Out-of-range Proto frequency accepted')
 assert info()['coreVoltage']==1200
 save('asic-settings.json',get('/api/system/asic'))
 for name,mv,mhz in [('baseline',1200,600),('frequency-down',1200,575),('frequency-up',1200,600),('combined-down',1180,575),('combined-up',1200,600)]:hold(name,mv,mhz)
 hold('saved-low-before-reboot',1180,600,45)
 before_restart=info();print('Testing explicit reboot with saved 1180 mV / 600 MHz',flush=True);req('/api/system/restart',b'','POST')
 startup('saved-low-startup',before_restart['uptimeSeconds'],1180)
 hold('saved-low-after-reboot',1180,600,90)
 hold('final-restore',1200,600,60)
 save('final.json',sample('final'));logs();print('COMPLETE',flush=True)
except Exception as e:
 save('failure.json',{'error':str(e)});print('FAILED',str(e),flush=True)
 try:patch(1200,600);logs()
 except Exception:pass
 raise
finally:f.close()
