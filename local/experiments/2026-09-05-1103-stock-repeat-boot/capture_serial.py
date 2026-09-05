"""User-authorized log-only bench. No scope API and no fake scope acknowledgements."""
import os,re,time,json,serial
from pathlib import Path
p=Path(__file__).resolve().parent
port_name=os.environ['BITAXE_BENCH_PORT']
ended=False;started=time.monotonic()
with serial.Serial(port_name,115200,timeout=.2) as port:
 try:
  with (p/'serial.log').open('w',buffering=1) as f:
   while time.monotonic()-started<1000:
    line=re.sub(r'\x1b\[[0-9;]*m','',port.readline().decode(errors='replace'))
    if not line:continue
    f.write(line)
    if any(x in line for x in ['WARM ', 'DEFER ', 'THERMAL invalid','THERMAL retry','SAFETY STOP','got ip:','App version:','COMPENSATION_CONFIG','VOUT_SCALE_LOOP','Switching frequency','ON_OFF_CONFIG']):print(line,end='',flush=True)
    if 'WARM END' in line:
     ended=True;break
   if not ended:raise RuntimeError('Log-only bench timeout')
  (p/'capture-status.json').write_text(json.dumps({'completed_log':ended,'scope_used':False,'port':port_name,'elapsed_host_seconds':time.monotonic()-started},indent=2))
 finally:
  if not ended:
   try:port.write(b'X');port.flush()
   except serial.SerialException:pass
