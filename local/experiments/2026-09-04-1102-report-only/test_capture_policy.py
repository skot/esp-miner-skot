"""Exercise the actual capture audit with the previously rejected 1102 trace."""
import ast,json,re,tempfile,time
from pathlib import Path
from threading import Event
import numpy as np
from scope_limits import assess,MEAN_WINDOW_S
here=Path(__file__).resolve().parent
fixture=here.parent/'2026-09-04-1102-warm-ramp'/'step-325'
pre=json.loads((fixture/'preambles.json').read_text())
raw=np.load(fixture/'raw.npz')
class Port:
 def __init__(self):self.sent=[]
 def write(self,b):self.sent.append(b)
 def flush(self):pass
class Scope:
 def __init__(self,clipped):
  self.data={k:v.copy() for k,v in raw.items()};self.source='CHAN1';self.start=1;self.stop=700000
  if clipped:self.data['CHAN2'][:100]=0
 def write(self,cmd):
  k,_,v=cmd.partition(' ')
  if k==':WAV:SOUR':self.source=v
  elif k==':WAV:STAR':self.start=int(v)
  elif k==':WAV:STOP':self.stop=int(v)
 def ask(self,q):
  if q==':WAV:PRE?':return pre[self.source]
  if q==':TRIG:STAT?':return 'STOP'
  if q.endswith(':IMP?'):return 'FIFT' if q.startswith(':CHAN1') else 'OMEG'
  if q.endswith(':COUP?'):return 'DC'
  if q.endswith(':PROB?'):return '1'
  raise AssertionError(q)
 def ask_raw(self,q):
  assert q==b':WAV:DATA?'
  b=self.data[self.source][self.start-1:self.stop].tobytes();n=str(len(b)).encode()
  return b'#'+str(len(n)).encode()+n+b
src=ast.parse((here/'capture_warm.py').read_text())
funcs=ast.Module(body=[n for n in src.body if isinstance(n,ast.FunctionDef) and n.name in ['block','check_inputs','read_capture']],type_ignores=[])
for clipped in [False,True]:
 with tempfile.TemporaryDirectory() as td:
  ns=dict(np=np,json=json,re=re,time=time,p=Path(td),s=Scope(clipped),port=Port(),ended=Event(),lines=['(0) WARM TRIAL target=325\n','(192) WARM LAUNCHED target=325\n'],assess=assess,MEAN_WINDOW_S=MEAN_WINDOW_S)
  exec(compile(funcs,'capture_warm.py','exec'),ns)
  accepted=ns['read_capture']('fixture',325)
  a=json.loads((Path(td)/'fixture/audit.json').read_text())
  assert not a['voltage_limits_enforced'] and not a['voltage_accepted']
  assert accepted== (not clipped)
  assert ns['port'].sent==([b'X'] if clipped else [])
print('PASS: historical overvoltage proceeds in report-only mode; clipped acquisition still stops')
