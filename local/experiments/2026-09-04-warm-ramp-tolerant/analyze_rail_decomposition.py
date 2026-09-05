"""Compare simultaneous total-rail and midpoint movement, without causal attribution."""
from pathlib import Path
import json
import numpy as np
p=Path(__file__).resolve().parent/'step-325'
r=np.load(p/'raw.npz')
pre={k:np.array(v.split(','),float) for k,v in json.loads((p/'preambles.json').read_text()).items()}
v={k:(r[k].astype(float)-a[8]-a[9])*a[7] for k,a in pre.items()}
t=np.arange(len(v['CHAN1']))*pre['CHAN1'][4]+pre['CHAN1'][5]
n=round(20e-6/pre['CHAN1'][4])
x={k:np.convolve(a,np.ones(n)/n,'valid') for k,a in v.items()}
tt=(t[:len(x['CHAN1'])]+t[n-1:])/2
x['lower']=x['CHAN2'];x['upper']=x['CHAN1']-x['CHAN2']
x['midpoint_error']=x['lower']-x['CHAN1']/2
base={k:float(np.mean(a[(tt>0)&(tt<.008)])) for k,a in x.items()}
out={'baseline':base}
for label,i in [('lower_peak',int(np.argmax(x['lower']))),('upper_peak',int(np.argmax(x['upper'])))]:
    z={k:float(a[i]) for k,a in x.items()};delta={k:z[k]-base[k] for k in z}
    out[label]={'time_ms':float(tt[i]*1e3),'volts':z,'delta_volts':delta,'half_total_change_v':delta['CHAN1']/2}
(p/'total-midpoint-decomposition.json').write_text(json.dumps(out,indent=2)+'\n')
