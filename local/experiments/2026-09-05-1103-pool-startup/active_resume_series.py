"""Confirm full-fan cold resume from an established pool-mining session."""
from pathlib import Path
import json,subprocess,sys,time,urllib.request
p=Path(__file__).resolve().parent
url='http://192.168.30.9/api/system'
name='run-06-active-cold-resume'
state={'stage':'waiting_for_run_05','success':False}
def save():
 (p/'active-series-status.json').write_text(json.dumps(state,indent=2))
 if state['stage']!='waiting_for_run_05':(p/'series-status.json').write_text(json.dumps(state,indent=2))
 print(json.dumps(state),flush=True)
def request(path='',method='GET',data=None):
 req=urllib.request.Request(url+path,method=method,data=json.dumps(data).encode() if data is not None else None,headers={'Content-Type':'application/json'})
 with urllib.request.urlopen(req,timeout=5) as r:
  b=r.read();return json.loads(b) if b else None
def identity():
 s=request('/info');assert s['macAddr'].upper()=='28:84:85:BA:96:08' and str(s['boardVersion'])=='1103';return s
try:
 save();deadline=time.monotonic()+1600
 prior=p/'run-05-cold-resume-recovery/result.json'
 while not prior.exists():
  if time.monotonic()>deadline:raise RuntimeError('Previous recovery test did not finish')
  time.sleep(5)
 assert json.loads(prior.read_text())['success'],'Previous recovery failed'
 s=identity();assert s['actualFrequency']==600 and s['hashRate']>2800 and s['sharesAccepted']>0 and not s['miningPaused']
 original={k:s[k] for k in ['autofanspeed','manualFanSpeed']}
 (p/(name+'-before-pause.json')).write_text(json.dumps({k:s.get(k) for k in ['uptimeSeconds','actualFrequency','hashRate','sharesAccepted','sharesRejected','asicTemps','regulatorVoltage','autofanspeed','manualFanSpeed','fanspeed']},indent=2))
 request(method='PATCH',data={'autofanspeed':0,'manualFanSpeed':100})
 request('/pause','POST');deadline=time.monotonic()+30
 while True:
  s=identity()
  if s['miningPaused'] and s['regulatorVoltage']<=100 and s['fanspeed']==100:break
  if time.monotonic()>deadline:raise RuntimeError('Could not establish full-fan ASIC-off cooldown')
  time.sleep(1)
 state['stage']=name+'-cooldown-100-percent-180s';save();rows=[]
 for _ in range(36):
  time.sleep(5);s=identity()
  assert s['miningPaused'] and s['regulatorVoltage']<=100 and s['fanspeed']==100
  rows.append({k:s.get(k) for k in ['uptimeSeconds','miningPaused','regulatorVoltage','fanspeed','fanrpm']})
  (p/(name+'-cooldown.json')).write_text(json.dumps(rows,indent=2))
 request(method='PATCH',data=original)
 state['stage']=name;save()
 with (p/(name+'-supervisor.log')).open('w') as f:
  subprocess.run([sys.executable,str(p/'monitor_pool.py'),'--run',name,'--seconds','300','--resume'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=1400)
 assert json.loads((p/name/'result.json').read_text())['success']
 state.update(stage='complete',success=True);save()
 subprocess.run([sys.executable,str(p/'analyze_pool.py')],check=True)
except BaseException as e:
 state.update(stage='stopped',error=str(e));save()
 try:identity();request('/pause','POST')
 except Exception:pass
 raise
