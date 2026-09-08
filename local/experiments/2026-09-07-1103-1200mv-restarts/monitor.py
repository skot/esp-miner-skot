import pathlib,json,time,urllib.request,sys
root=pathlib.Path(__file__).resolve().parent
p=root/sys.argv[1];p.mkdir(exist_ok=True)
base='http://192.168.30.9'
keys=['version','axeOSVersion','boardVersion','macAddr','actualFrequency','hashRate','asicTemps','asicVoltages','domainVoltages','regulatorVoltage','miningPaused','uptimeSeconds','sharesAccepted','sharesRejected','power','fanspeed','coreVoltage','frequency','hardwareFault','autofanspeed','manualFanSpeed']
def get(path):
 with urllib.request.urlopen(base+path,timeout=15) as r:return json.load(r)
def save(name,data):(p/name).write_text(json.dumps(data,indent=2)+'\n')
def logs():
 with urllib.request.urlopen(base+'/api/system/logs',timeout=15) as r:raw=r.read()
 (p/'device-latest.log').write_bytes(raw)
 with (p/'device-all.log').open('ab') as f:f.write(raw+b'\n')
start=time.monotonic();pool_start=None;last_print=-30;n=0;last_uptime=None
with (p/'samples.jsonl').open('w') as f:
 while time.monotonic()-start<900:
  elapsed=time.monotonic()-start
  try:
   raw=get('/api/system/info');state={k:raw[k] for k in keys if k in raw};d=get('/api/system/diagnostics')
   assert state['boardVersion']=='1103' and state['macAddr']=='28:84:85:BA:96:08' and state['coreVoltage']==1200 and state['frequency']==600
   if last_uptime is not None and state['uptimeSeconds']<last_uptime:raise RuntimeError('Unexpected reboot during observation')
   last_uptime=state['uptimeSeconds'];row={'elapsed':elapsed,'info':state,'diagnostics':d};f.write(json.dumps(row)+'\n');f.flush()
   if n==0:save('initial.json',row)
   if d.get('mining')==1 and d.get('startupResult')=='passed' and pool_start is None:pool_start=elapsed
   if elapsed-last_print>=25:
    print(json.dumps({'elapsed':round(elapsed),'uptime':state['uptimeSeconds'],'MHz':state['actualFrequency'],'ghs':state['hashRate'],'temps':state['asicTemps'],'status':d.get('status'),'startupResult':d.get('startupResult'),'accepted':state['sharesAccepted'],'rejected':state['sharesRejected']}),flush=True);last_print=elapsed
   if n%2==0:logs()
   n+=1
   if d.get('startupResult')=='failed':
    save('failure.json',row);logs();print('FAIL: startup failed; state preserved',flush=True);break
   if pool_start is not None and elapsed-pool_start>=300:
    save('final.json',row);save('hold.json',{'pool_seconds':elapsed-pool_start});logs();print('PASS: five-minute pool hold completed',flush=True);break
  except (AssertionError,RuntimeError):raise
  except Exception as exc:print(type(exc).__name__,str(exc)[:160],flush=True)
  time.sleep(5)
 else:raise RuntimeError('Observation timed out')
