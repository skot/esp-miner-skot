import time,json,vxi11
from pathlib import Path
p=Path(__file__).resolve().parent
s=vxi11.Instrument('192.168.1.250');s.timeout=8
log=[]
def record(event):
 d={'elapsed':round(time.monotonic()-start,3),'event':event,'state':s.ask(':TRIG:STAT?')};log.append(d);print(d,flush=True)
start=time.monotonic()
try:
 assert s.ask(':CHAN1:IMP?')=='FIFT'
 for c in [':STOP',':TIM:MODE MAIN',':TIM:SCAL 2',':TIM:OFFS 12',':ACQ:MDEP 700000',':ACQ:TYPE NORM',':TRIG:MODE EDGE',':TRIG:EDGE:SOUR CHAN2',':TRIG:EDGE:SLOP POS',':TRIG:EDGE:LEV 2.0',':SINGle']:s.write(c)
 record('single')
 time.sleep(9)
 record('prefilled')
 s.write(':TFORce');record('forced')
 deadline=time.monotonic()+40
 while time.monotonic()<deadline:
  time.sleep(2);record('poll')
  if log[-1]['state']=='STOP':break
 for c in ['CHAN1','CHAN2']:
  for cmd in [f':WAV:SOUR {c}',':WAV:MODE RAW',':WAV:FORM BYTE',':WAV:POIN 700000']:s.write(cmd)
  log.append({c:s.ask(':WAV:PRE?')})
 print(log[-2:],flush=True)
finally:
 s.write(':STOP');assert s.ask(':CHAN1:IMP?')=='FIFT';s.close()
 (p/'scope-preflight.json').write_text(json.dumps(log,indent=2))
