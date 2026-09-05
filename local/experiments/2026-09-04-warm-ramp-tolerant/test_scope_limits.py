"""Check new limits against recorded baselines, unsafe launch and boundary cases."""
from pathlib import Path
import json
import numpy as np
from scope_limits import assess
p=Path(__file__).resolve().parent
out={}
for name,relative,stem,expected in [
    ('baseline1160','2026-09-04-warm-ramp/warm-baseline300','',True),
    ('baseline1140','2026-09-04-warm-ramp-1140/warm-baseline300','',True),
    ('prior_broadcast','2026-09-04-broadcast-1140','transition-',False)]:
    folder=p.parent/relative
    raw=np.load(folder/(stem+'raw.npz'))
    pre={k:np.array(v.split(','),float) for k,v in json.loads((folder/(stem+'preambles.json')).read_text()).items()}
    v={k:(raw[k].astype(float)-a[8]-a[9])*a[7] for k,a in pre.items()}
    out[name]=assess(v['CHAN2'],v['CHAN1']-v['CHAN2'],pre['CHAN1'][4])
    assert out[name]['voltage_accepted']==expected
# Isolated small spike allowed, large spike rejected, sustained rise rejected.
b=np.full(200,1.15);a=b.copy();a[99]=1.24
assert assess(a,b,.5e-6)['voltage_accepted']
a[99]=1.25
assert not assess(a,b,.5e-6)['voltage_accepted']
a=b.copy();a[85:125]=1.23
assert not assess(a,b,.5e-6)['voltage_accepted']
# A rise spanning a download chunk boundary must still be rejected.
assert assess(a[61:150],b[61:150],.5e-6)['lower_max_moving_mean_v']>1.22
assert not assess(np.full(100,np.nan),b[:100],.5e-6)['voltage_accepted']
(p/'criteria-validation.json').write_text(json.dumps(out,indent=2)+'\n')
print('PASS: historical captures and raw/mean/boundary/invalid cases')
