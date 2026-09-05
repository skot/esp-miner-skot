import os,serial,threading,time,json,urllib.request,urllib.error,http.client,re
from pathlib import Path
p=Path(__file__).resolve().parent;stop=threading.Event();base='http://192.168.30.9';lines=[]
def api(path='/api/system/info',method='GET'):
 req=urllib.request.Request(base+path,data=b'' if method=='POST' else None,method=method)
 with urllib.request.urlopen(req,timeout=3) as r:
  b=r.read();return json.loads(b) if b else None
with serial.Serial(os.environ['BITAXE_BENCH_PORT'],115200,timeout=.2) as port:
 def reader():
  with (p/'serial.log').open('w',buffering=1) as f:
   while not stop.is_set():
    line=re.sub(r'\x1b\[[0-9;]*m','',port.readline().decode(errors='replace'));f.write(line);lines.append(line)
    if any(k in line for k in ['App version:','COMPENSATION_CONFIG','VOUT_SCALE_LOOP','FREQUENCY_SWITCH','init response','No MC3','ASIC initialized','SPDLOG:']):print(line,end='',flush=True)
 th=threading.Thread(target=reader,daemon=True);th.start()
 try:
  end=time.monotonic()+45
  while time.monotonic()<end:
   try:
    a=api();assert a['boardVersion']=='1103' and a['macAddr'].lower()=='28:84:85:ba:96:08';break
   except (OSError,urllib.error.URLError,http.client.HTTPException):time.sleep(1)
  else:raise RuntimeError('No board API before comparison reboot')
  (p/'before-api.json').write_text(json.dumps(a,indent=2));print('RESTART',api('/api/system/restart','POST'),flush=True)
  end=time.monotonic()+65
  with (p/'api-samples.jsonl').open('w') as f:
   while time.monotonic()<end:
    try:
     a=api();f.write(json.dumps(a)+'\n');f.flush()
     print({k:a.get(k) for k in ['uptimeSeconds','regulatorVoltage','domainVoltages','actualFrequency','hashRate','asicTemps','power_fault']},flush=True)
     if any(t>=70 for t in a.get('asicTemps',[])):raise RuntimeError('Temperature reached70C')
    except (OSError,urllib.error.URLError,http.client.HTTPException):pass
    time.sleep(2)
  (p/'final-api.json').write_text(json.dumps(a,indent=2))
 finally:
  try:print('PAUSE',api('/api/system/pause','POST'),flush=True)
  finally:time.sleep(1);stop.set();th.join(2)
