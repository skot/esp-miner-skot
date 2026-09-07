from pathlib import Path
import re,sys
p=Path(__file__).resolve().parent/sys.argv[1];s=(p/'serial.log').read_text()
pat=r'\((\d+)\).*BCAST SAMPLE phase=(\w+) A(\d) counter_us=(\d+) dt_us=(\d+) pass=([\d,]+) rate=([\d.,-]+) zero=([\d,]+) D2_mV=([\d.-]+) fail=([\d,]+) temp_C=([\d.-]+) freq=([\d.]+)'
rows=[m.groups() for m in re.finditer(pat,s)];total=0
for c in range(1,5):
 a=[r for r in rows if int(r[2])==c]
 if not a:continue
 last=a[-1];a=[r for r in a if r[1]==last[1]];first=a[0]
 rate=None
 if last[1]=='warm600' and int(last[3])>int(first[3]):
  rate=(sum(map(int,last[5].split(',')))-sum(map(int,first[5].split(','))))*16777.216/(int(last[3])-int(first[3]));total+=rate
 print(f'A{c} {last[1]} {last[11]}MHz {last[10]}C zeros={last[7]} GH/s={round(rate,2) if rate else "-"}')
if total:print('TOTAL',round(total,2),'GH/s')
for line in s.splitlines():
 if any(m in line for m in ['DEFER TEMP freq=600','DEFER END','WARM END']):print(line)
