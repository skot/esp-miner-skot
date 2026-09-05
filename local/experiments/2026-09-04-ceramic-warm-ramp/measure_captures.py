"""Full-record rail extrema and pre-event sampled spread, not a switching-ripple measurement."""
from pathlib import Path
import json
import numpy as np
p=Path(__file__).resolve().parent
out={}
for run in [p.parent/'2026-09-04-tps-local-50ohm',p]:
 for folder in sorted(run.iterdir()):
  if not (folder/'raw.npz').is_file():continue
  r=np.load(folder/'raw.npz');pr={k:np.array(v.split(','),float) for k,v in json.loads((folder/'preambles.json').read_text()).items()}
  dt=pr['CHAN1'][4];n=round(20e-6/dt)
  t=(np.arange(len(r['CHAN1']))-pr['CHAN1'][6])*dt+pr['CHAN1'][5]
  v={k:(r[k].astype(float)-a[8]-a[9])*a[7] for k,a in pr.items()}
  v['upper_estimate']=v['CHAN1']-v['CHAN2'];v['midpoint_error']=v['CHAN2']-v['CHAN1']/2
  result={}
  for k,a in v.items():
   b=a[(t>0)&(t<.008)];m=np.convolve(a,np.ones(n)/n,'valid');base=float(b.mean())
   result[k]={'baseline_v':base,'raw_min_v':float(a.min()),'raw_max_v':float(a.max()),'mean20us_min_v':float(m.min()),'mean20us_max_v':float(m.max()),'mean_overshoot_mv':float((m.max()-base)*1000),'mean_undershoot_mv':float((m.min()-base)*1000),'baseline_raw_pp_mv':float(np.ptp(b)*1000),'baseline_p99_minus_p1_mv':float(np.diff(np.percentile(b,[1,99]))[0]*1000),'raw_sample_lsb_v':float(pr['CHAN1' if k=='CHAN1' else 'CHAN2'][7]) if k in pr else None}
  out[run.name+'/'+folder.name]=result
(p/'capture-metrics.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps(out,indent=2))
