from pathlib import Path
import json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
p=Path(__file__).resolve().parent
rates=json.loads((p/'rate-windows.json').read_text());fs=list(range(300,601,25));matrix=np.full((16,len(fs)),np.nan)
for r in rates:
 if r['window']=='whole_phase':matrix[(r['asic']-1)*4+r['domain'],fs.index(int(r['frequency']))]=r['pass_percent']
fig,ax=plt.subplots(figsize=(12,7),layout='constrained')
cmap=plt.get_cmap('viridis').copy();cmap.set_bad('#dedede')
im=ax.imshow(matrix,vmin=0,vmax=100,cmap=cmap,aspect='auto')
ax.set_xticks(range(len(fs)),fs);ax.set_yticks(range(16),[f'A{c} D{d}' for c in range(1,5) for d in range(4)])
for r in range(16):
 for c in range(len(fs)):
  if np.isfinite(matrix[r,c]):ax.text(c,r,f'{matrix[r,c]:.0f}',ha='center',va='center',fontsize=8,color='white' if matrix[r,c]<65 else 'black')
for y in [3.5,7.5,11.5]:ax.axhline(y,color='white',lw=2)
ax.set_xlabel('PLL frequency (MHz) • 600 MHz locked, then invalid temperature read before first counter snapshot')
fig.colorbar(im,ax=ax,label='Passing throughput (% ideal)')
ax.set_title('1102 report-only warm ramp: progressive loss of passing activity on A1/A2\nEach cell averages its frequency observation window; gray = no counter data')
for ext in ['png','pdf']:fig.savefig(p/f'domain-progression.{ext}',dpi=150)
