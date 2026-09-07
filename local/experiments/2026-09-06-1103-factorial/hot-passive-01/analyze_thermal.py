from pathlib import Path
import re,json,collections
p=Path(__file__).resolve().parent;s=(p/'serial.log').read_text()
pat=re.compile(r'\((\d+)\).*THERMAL attempt=(\d+) A(\d+) fresh=(\d) trigger=(\d) done=(0x[\dA-F]+) status=(0x[\dA-F]+) status_read=(\d) raw=(0x[\dA-F]+) data_read=(\d) temp=([\d.-]+) valid=(\d)')
rows=[]
for m in pat.finditer(s):
 t,attempt,chip,fresh,trigger,done,status,sr,raw,dr,temp,valid=m.groups()
 rows.append(dict(boot_ms=int(t),attempt=int(attempt),asic=int(chip),fresh=int(fresh),trigger=int(trigger),done=int(done,16),status=int(status,16),status_read=int(sr),raw=int(raw,16),data_read=int(dr),temperature=float(temp),valid=int(valid)))
(p/'thermal-samples.json').write_text(json.dumps(rows,indent=2))
summary=dict(samples=len(rows),invalid=[r for r in rows if not r['valid']],retry_messages=[l for l in s.splitlines() if any(x in l for x in ['THERMAL retry','THERMAL recovered','THERMAL invalid','THERMAL stale'])])
summary['per_asic']={str(c):dict(samples=len(a),invalid=sum(not r['valid'] for r in a),raw_zero=sum(r['data_read'] and r['raw']==0 for r in a),max_valid_c=max((r['temperature'] for r in a if r['valid']),default=None)) for c in range(1,5) for a in [[r for r in rows if r['asic']==c]]}
(p/'thermal-summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
