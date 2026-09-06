"""Derive per-domain throughput from passive counter growth, never API rolling rate."""
from pathlib import Path
import argparse,json,re,statistics
ap=argparse.ArgumentParser();ap.add_argument('--run',default='run-01');args=ap.parse_args()
p=Path(__file__).resolve().parent/args.run;s=(p/'serial.log').read_text(errors='replace')
pattern=r'PASSIVE COUNT phase=(\w+) A(\d) D(\d) begin_us=(\d+) end_us=(\d+) pass=(\d+) fail=(\d+) zero=(\d+)'
groups={}
for phase,a,d,t0,t1,pas,fail,zero in re.findall(pattern,s):
 row={'begin_us':int(t0),'end_us':int(t1),'time':(int(t0)+int(t1))/2e6,'pass':int(pas),'fail':int(fail),'zero':int(zero)}
 groups.setdefault((phase,int(a),int(d)),[]).append(row)
def slope(rows,key):
 x=[r['time'] for r in rows];y=[r[key] for r in rows];xm=statistics.mean(x);ym=statistics.mean(y)
 return sum((a-xm)*(b-ym) for a,b in zip(x,y))/sum((a-xm)**2 for a in x)
results=[]
for (phase,a,d),rows in sorted(groups.items()):
 if len(rows)<2:continue
 monotonic=all(n['pass']>=o['pass'] and n['fail']>=o['fail'] for o,n in zip(rows,rows[1:]))
 rate=slope(rows,'pass')*2**24/1e9 if monotonic else None
 failrate=slope(rows,'fail')*2**24/1e9 if monotonic else None
 r={'phase':phase,'asic':a,'domain':d,'samples':len(rows),'duration_seconds':rows[-1]['time']-rows[0]['time'],'monotonic':monotonic,'pass_ghs':rate,'fail_ghs':failrate,'percent_ideal':100*rate/187.2 if rate is not None else None,'zero_first':rows[0]['zero'],'zero_last':rows[-1]['zero'],'first_pass':rows[0]['pass'],'last_pass':rows[-1]['pass']};results.append(r)
 print(f"{phase:12} A{a} D{d}: {rate:.2f} GH/s ({r['percent_ideal']:.2f}% ideal), zero {r['zero_first']}->{r['zero_last']}, {r['duration_seconds']:.2f}s" if monotonic else f'{phase} A{a} D{d}: COUNTER DECREASE')
audit=[{'phase':p,'us':int(t),'writes':int(w),'pll':int(pl),'logger':int(l),'failures':int(f)} for p,t,w,pl,l,f in re.findall(r'PASSIVE AUDIT phase=(\w+) us=(\d+) writes=(\d+) pll=(\d+) logger=(\d+) failures=(\d+)',s)]
plls=[{'phase':p,'asic':int(a),'cfg':c,'en':e} for p,a,c,e in re.findall(r'PASSIVE PLL phase=(\w+) A(\d) cfg=([\da-f]+) en=([\da-f]+)',s)]
output={'completed':'PASSIVE END completed=1' in s,'domains':results,'audit':audit,'pll_states':plls,'totals':{phase:sum(r['pass_ghs'] for r in results if r['phase']==phase and r['pass_ghs'] is not None) for phase in ['before_reset','after_reset']}}
by_phase={phase:[r for r in audit if r['phase']==phase] for phase in ['before','first','before_reset','reset','after_reset']}
verified=all(by_phase.values()) and len(results)==32 and all(r['monotonic'] for r in results)
if verified:
 before,first,reset=(by_phase[k][0] for k in ['before','first','reset'])
 verified=(first['logger']==before['logger']+2 and first['pll']==before['pll'] and
           first['failures']==before['failures'] and reset['logger']==first['logger'] and
           reset['pll']==first['pll'] and reset['failures']==first['failures'] and reset['writes']>first['writes'])
 for phase,reference in [('before_reset',first),('after_reset',reset)]:
  verified=verified and all(all(r[k]==reference[k] for k in ['writes','pll','logger','failures']) for r in by_phase[phase])
 for chip in range(1,5):
  verified=verified and len({(r['cfg'],r['en']) for r in plls if r['asic']==chip})==1
output['passive_audit_verified']=bool(verified)
(p/'analysis.json').write_text(json.dumps(output,indent=2));print(json.dumps({'completed':output['completed'],'passive_audit_verified':verified,'totals':output['totals']},indent=2))
