import pathlib,json,time,urllib.request,sys
root=pathlib.Path(__file__).resolve().parent;p=root/sys.argv[1]
base='http://192.168.30.9'
def req(path,method='GET',data=None):
 r=urllib.request.Request(base+path,method=method,data=json.dumps(data).encode() if data is not None else None,headers={'Content-Type':'application/json'})
 with urllib.request.urlopen(r,timeout=15) as s:
  raw=s.read();return json.loads(raw) if raw else None
def info():return req('/api/system/info')
def wait_for(test,timeout=30):
 start=time.monotonic()
 while time.monotonic()-start<timeout:
  d=info()
  if test(d):return d
  time.sleep(1)
 raise RuntimeError('Expected device state did not arrive')
def save(data):(p/'preparation.json').write_text(json.dumps(data,indent=2)+'\n')
d=info();assert d['boardVersion']=='1103' and d['macAddr']=='28:84:85:BA:96:08' and d['coreVoltage']==1200 and d['frequency']==600
prep={'before':{k:d[k] for k in ['coreVoltage','actualFrequency','hashRate','regulatorVoltage','uptimeSeconds','sharesAccepted','sharesRejected']},'original_fan':{k:d[k] for k in ['autofanspeed','manualFanSpeed']},'temporary_blocked_setting':False};save(prep)
if d['regulatorVoltage']>=100:
 req('/api/system/pause','POST');wait_for(lambda v:v['miningPaused'] and v['regulatorVoltage']<100)
if info()['miningPaused']:
 # Paused fan is fixed at 30%; reject startup before power-on to allow fan 100%.
 req('/api/system','PATCH',{'coreVoltage':1201,'autofanspeed':0,'manualFanSpeed':100});req('/api/system/resume','POST')
 prep['temporary_blocked_setting']=True;save(prep)
else:req('/api/system','PATCH',{'autofanspeed':0,'manualFanSpeed':100})
d=wait_for(lambda v:v['fanspeed']==100 and v['regulatorVoltage']<100)
prep.update(cooling_started_unix=time.time(),off_regulator_mv=d['regulatorVoltage'],fan_percent=d['fanspeed']);save(prep)
for remaining in range(120,0,-20):
 print('ASIC power off; fan 100%; cooling seconds remaining:',remaining,flush=True);time.sleep(20)
 d=info();assert d['regulatorVoltage']<100 and d['fanspeed']==100
req('/api/system','PATCH',{**prep['original_fan'],'coreVoltage':1200})
d=info();assert d['coreVoltage']==1200 and d['regulatorVoltage']<100
prep['restart_before_uptime']=d['uptimeSeconds'];prep['restart_unix']=time.time();save(prep)
req('/api/system/restart','POST');print('API restart sent',flush=True)
start=time.monotonic()
while time.monotonic()-start<60:
 try:
  v=info()
  if v['uptimeSeconds']<prep['restart_before_uptime']:
   prep['first_new_uptime']=v['uptimeSeconds'];save(prep);print('Restart confirmed; uptime',v['uptimeSeconds'],flush=True);break
 except Exception:pass
 time.sleep(2)
else:raise RuntimeError('Restart not confirmed')
