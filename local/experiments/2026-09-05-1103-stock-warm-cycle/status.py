from pathlib import Path
import re
p=Path(__file__).resolve().parent
if (p/'serial.log').exists():
 s=(p/'serial.log').read_text()
 for c in range(1,5):
  a=re.findall(r'\((\d+)\).*BCAST SAMPLE phase=(\w+) A'+str(c)+r'.*rate=([\d.,-]+) zero=([\d,]+) D2_mV=([\d.-]+).*temp_C=([\d.-]+) freq=([\d.]+)',s)
  if a:
   t,phase,r,z,mv,temp,f=a[-1];print(f'A{c} t={t} phase={phase} MHz={f} temp={temp} D2_rate={r.split(",")[2]} D2_mv={mv} zeros={z}')
 for line in [l for l in s.splitlines() if any(k in l for k in ['WARM POLICY','WARM STEP','WARM FINAL','WARM END','SAFETY STOP','DEFER '])][-5:]:print(line)
if (p/'capture.log').exists():
 for line in (p/'capture.log').read_text().splitlines()[-2:]:print(line)
