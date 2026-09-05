"""Bounded live mining observation. Uses existing pool settings; never prints account data."""
import argparse,json,re,time,threading,urllib.request,serial
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('--run',required=True);ap.add_argument('--seconds',type=int,default=1800);ap.add_argument('--restart',action='store_true');ap.add_argument('--resume',action='store_true');args=ap.parse_args()
p=Path(__file__).resolve().parent/args.run;p.mkdir(exist_ok=True)
url='http://192.168.30.9';mac='28:84:85:BA:96:08'
keys=['macAddr','boardVersion','version','uptimeSeconds','frequency','actualFrequency','coreVoltage','regulatorVoltage','domainVoltages','asicTemps','temp','vrTemp','hashRate','hashRate_1m','hashRate_10m','sharesAccepted','sharesRejected','poolDifficulty','miningPaused','hardware_fault','power','fanSpeed','fanspeed','fanrpm','minFanSpeed','autofanspeed']
state={'ready':False,'failed':None,'spd':[],'boots':0};done=threading.Event();started=time.monotonic();samples=[];hold_start=None;low_since=None;accepted_baseline=0

def request(path,method='GET'):
 with urllib.request.urlopen(urllib.request.Request(url+path,method=method),timeout=5) as r:
  b=r.read();return json.loads(b) if b else None

def capture(port):
 with (p/'serial.log').open('w',buffering=1) as f:
  while not done.is_set():
   try:raw=port.readline()
   except (serial.SerialException,OSError) as e:
    if not done.is_set() and port.is_open:state['failed']='Serial capture failed: '+str(e)
    break
   line=re.sub(r'\x1b\[[0-9;]*m','',raw.decode(errors='replace'))
   if not line:continue
   f.write(line)
   if 'PROTO STARTUP READY' in line:state['ready']=True
   if any(x in line for x in ['PROTO STARTUP FAILED','PROTO STARTUP STOP','Guru Meditation','THERMAL invalid after','Proto temperature read failed']):state['failed']=line.strip()
   if 'app_init: App version:' in line:state['boots']+=1
   m=re.search(r'SPDLOG:.*total=([\d.]+) GH/s',line)
   if m:state['spd'].append({'host_seconds':time.monotonic()-started,'total_ghs':float(m[1]),'chips_ghs':[float(x) for x in re.findall(r'chip\d=([\d.]+) GH/s',line)]})
   if ('PROTO STARTUP ' in line and ' TEMP ' not in line and ' DOMAIN ' not in line and ' CHIP ' not in line) or 'ASIC initialized successfully' in line:print(line.strip(),flush=True)

result={'run':args.run,'target_seconds':args.seconds,'scope_used':False,'success':False}
try:
 with serial.Serial('/dev/cu.usbmodem11434201',115200,timeout=.2) as port:
  th=threading.Thread(target=capture,args=(port,),daemon=True);th.start()
  if args.restart:request('/api/system/restart','POST')
  if args.resume:
   accepted_baseline=request('/api/system/info').get('sharesAccepted',0)
   request('/api/system/resume','POST')
  api_errors=0
  with (p/'api-samples.jsonl').open('w',buffering=1) as f:
   while time.monotonic()-started<args.seconds+1000:
    if state['failed']:raise RuntimeError(state['failed'])
    if state['boots']>1:raise RuntimeError('Unexpected ESP reboot during observation')
    try:s=request('/api/system/info');api_errors=0
    except Exception:
     api_errors+=1
     if api_errors>=12:raise RuntimeError('API unavailable for about one minute')
     time.sleep(5);continue
    assert s['macAddr'].upper()==mac and str(s['boardVersion'])=='1103','Board identity mismatch'
    row={'host_seconds':time.monotonic()-started,**{k:s.get(k) for k in keys}};samples.append(row);f.write(json.dumps(row)+'\n')
    if row.get('hardware_fault'):raise RuntimeError('Firmware hardware fault: '+str(row['hardware_fault']))
    temps=row.get('asicTemps') or []
    if temps and max(temps)>=70:raise RuntimeError('ASIC temperature reached 70 C')
    if state['ready'] and row.get('hashRate',0)>2800 and row.get('actualFrequency')==600 and (row.get('sharesAccepted') or 0)>accepted_baseline:
     if hold_start is None:hold_start=time.monotonic();print('POOL HOLD BEGIN: 600MHz, near 3TH/s, accepted shares confirmed',flush=True)
    if hold_start is not None:
     if row.get('actualFrequency')!=600 or row.get('miningPaused'):raise RuntimeError('Mining left the requested operating point')
     if row.get('hashRate',0)<2800:
      if low_since is None:low_since=time.monotonic()
      if time.monotonic()-low_since>60:raise RuntimeError('Hashrate below 2.8TH/s for over one minute')
     else:low_since=None
     if time.monotonic()-hold_start>=args.seconds:break
    elif time.monotonic()-started>900:raise RuntimeError('Startup/pool handoff did not reach full hashrate with accepted shares in 15 minutes')
    time.sleep(5)
   else:raise RuntimeError('Observation deadline expired')
  result.update(success=True,hold_seconds=time.monotonic()-hold_start)
  done.set();th.join(timeout=2)
except BaseException as e:
 result['error']=str(e);print('TEST STOP:',str(e),flush=True)
 try:request('/api/system/pause','POST');result['pause_requested']=True
 except Exception:result['pause_requested']=False
 done.set()
finally:
 result['elapsed_host_seconds']=time.monotonic()-started;result['boots']=state['boots'];result['samples']=len(samples)
 hold_rows=[r for r in samples if hold_start and r['host_seconds']>=hold_start-started]
 result['first_sample']=samples[0] if samples else None;result['last_sample']=samples[-1] if samples else None
 if hold_rows:
  result['mean_api_ghs']=sum(r['hashRate'] for r in hold_rows)/len(hold_rows)
  result['min_api_ghs']=min(r['hashRate'] for r in hold_rows)
  result['max_asic_c']=max(t for r in hold_rows for t in r['asicTemps'])
  result['accepted_during_hold']=hold_rows[-1]['sharesAccepted']-hold_rows[0]['sharesAccepted']
  result['rejected_during_hold']=hold_rows[-1]['sharesRejected']-hold_rows[0]['sharesRejected']
 spd=[r for r in state['spd'] if hold_start and r['host_seconds']>=hold_start-started]
 result['spd_windows']=len(spd)
 if spd:result['mean_spd_ghs']=sum(r['total_ghs'] for r in spd)/len(spd)
 (p/'spd-samples.json').write_text(json.dumps(state['spd'],indent=2))
 (p/'result.json').write_text(json.dumps(result,indent=2))
 print(json.dumps({k:v for k,v in result.items() if k not in ['first_sample','last_sample']},indent=2),flush=True)
 if not result['success']:raise SystemExit(1)
