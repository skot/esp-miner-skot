"""Unchanged-firmware comparison: established mining -> cooled resume vs reboot."""
from pathlib import Path
import json,subprocess,sys,time,urllib.request

p=Path(__file__).resolve().parent
url='http://192.168.30.9/api/system'
state={'stage':'preflight','finished':False,'success':False,'runs':[]}
def save():
 (p/'comparison-status.json').write_text(json.dumps(state,indent=2));print(json.dumps(state),flush=True)
def request(path='',method='GET',data=None):
 req=urllib.request.Request(url+path,method=method,data=json.dumps(data).encode() if data is not None else None,headers={'Content-Type':'application/json'})
 with urllib.request.urlopen(req,timeout=5) as r:
  b=r.read();return json.loads(b) if b else None
def identity():
 s=request('/info')
 assert s['macAddr'].upper()=='28:84:85:BA:96:08' and str(s['boardVersion'])=='1103','Board identity mismatch'
 assert s['frequency']==600 and s['coreVoltage']==1160,'Operating settings changed'
 return s
def selected(s):
 return {k:s.get(k) for k in ['macAddr','boardVersion','version','uptimeSeconds','frequency','actualFrequency','coreVoltage','miningPaused','regulatorVoltage','domainVoltages','asicTemps','fanspeed','fanrpm','autofanspeed','manualFanSpeed','minFanSpeed','hashRate','sharesAccepted','sharesRejected','hardware_fault']}
def cool(name,require_mining=False):
 s=identity()
 if require_mining:assert not s['miningPaused'] and s['actualFrequency']==600 and s['hashRate']>2800 and s['sharesAccepted']>0,'No established mining session'
 (p/(name+'-before-pause.json')).write_text(json.dumps(selected(s),indent=2))
 state['stage']=name+'-cooldown';save()
 request(method='PATCH',data={'autofanspeed':0,'manualFanSpeed':100})
 request('/pause','POST');deadline=time.monotonic()+30
 while True:
  s=identity()
  if s['miningPaused'] and s['regulatorVoltage']<=100 and s['fanspeed']==100:break
  if time.monotonic()>deadline:raise RuntimeError('ASIC-off / full-fan conditions not established')
  time.sleep(1)
 start=time.monotonic();rows=[]
 for _ in range(36):
  time.sleep(5);s=identity()
  assert s['miningPaused'] and s['regulatorVoltage']<=100 and s['fanspeed']==100,'Cooldown conditions changed'
  rows.append({'cooldown_seconds':time.monotonic()-start,**selected(s)})
  (p/(name+'-cooldown.json')).write_text(json.dumps(rows,indent=2))
 request(method='PATCH',data=fan_settings)
def run(name,action):
 identity();state['stage']=name;save()
 with (p/(name+'-supervisor.log')).open('w') as f:
  proc=subprocess.run([sys.executable,str(p/'monitor_pool.py'),'--run',name,'--seconds','300','--'+action],stdout=f,stderr=subprocess.STDOUT,timeout=1400)
 result=json.loads((p/name/'result.json').read_text())
 state['runs'].append({'name':name,'success':result['success'],'error':result.get('error')});save()
 assert (proc.returncode==0)==result['success'],'Supervisor exit mismatch'
 if action=='restart':
  prefix=result.get('elf_sha256_prefix')
  expected=json.loads((p/'firmware-identity.json').read_text())['elf_sha256']
  assert prefix and len(prefix)>=8 and expected.startswith(prefix),'Running ELF identity mismatch or missing boot log'
 if not result['success']:
  request('/pause','POST');time.sleep(1)
  assert identity()['regulatorVoltage']<=100,'Failed run did not switch off ASIC power'
 return result['success']

try:
 s=identity();assert not s.get('hardware_fault')
 fan_settings={k:s[k] for k in ['autofanspeed','manualFanSpeed']}
 assert fan_settings=={'autofanspeed':1,'manualFanSpeed':100} and s['minFanSpeed']==99
 (p/'before-settings.json').write_text(json.dumps(selected(s),indent=2))
 # Reboot normally, without interrupting initialization, to establish mining.
 baseline=run('run-01-baseline-boot','restart')
 if not baseline:
  cool('baseline-retry')
  baseline=run('run-01b-baseline-retry','restart')
 if not baseline:raise RuntimeError('Could not establish pool mining on the unchanged build; resume comparison not run')
 cool('resume',require_mining=True)
 resumed=run('run-02-cooled-resume','resume')
 # Still perform the matched reboot control if resume fails. The failed run
 # has already been shut down and the same full-fan cooldown is verified.
 cool('reboot',require_mining=resumed)
 rebooted=run('run-03-cooled-reboot','restart')
 state.update(stage='complete',finished=True,success=resumed and rebooted);save()
 subprocess.run([sys.executable,str(p/'analyze_pool.py')],check=True)
except BaseException as e:
 state.update(stage='stopped',finished=True,error=str(e));save()
 try:identity();request('/pause','POST')
 except Exception:pass
 raise
