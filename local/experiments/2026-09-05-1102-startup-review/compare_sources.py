from pathlib import Path
import subprocess,re,json,hashlib,struct
p=Path(__file__).resolve().parent;root=p.parents[2]
old=subprocess.check_output(['git','show','cd82a9e:components/asic/mc3.c'],cwd=root,text=True)
new=(root/'components/asic/mc3.c').read_text()
for ref in ['cd82a9e','9df1d31','0ac9d30']:
 for src in ['components/asic/mc3.c','main/power/asic_init.c']:
  (p/(ref+'-'+Path(src).name)).write_bytes(subprocess.check_output(['git','show',ref+':'+src],cwd=root))
for src in ['components/asic/mc3.c','main/power/asic_init.c']:(p/('current-'+Path(src).name)).write_bytes((root/src).read_bytes())
def table(s):
 t=s.split('PLL_CONFIGS[] = {',1)[1].split('};',1)[0]
 return {float(a.rstrip('f')):list(map(int,[b,c,d,e])) for a,b,c,d,e in re.findall(r'\{([\d.f]+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}',t)}
a,b=table(old),table(new);common=[dict(mhz=f,old=a[f],current=b.get(f),same=a[f]==b.get(f)) for f in a]
print('Changed common entries:',json.dumps([r for r in common if not r['same']]))
print(sum(r['same'] for r in common),'of',len(common),'old PLL table entries match current divider tuples.')
extra=sorted(set(b)-set(a));print('Added fractional entries:',len(extra));print('400→600 table iterations old/new:',sum(400<f<=600 for f in a),sum(400<f<=600 for f in b))
for r in common:
 def cfg(v):
  ref,fb,p1,p2=v;return f'0x{8|(fb<<4)|(ref<<16)|(p1<<22)|(p2<<25):08X}'
 r['old_cfg']=cfg(r['old']);r['current_cfg']=cfg(r['current'])
(p/'pll-table-comparison.json').write_text(json.dumps(dict(common=common,added_frequencies_mhz=extra),indent=2)+'\n')
backup=root/'local/experiments/2026-09-04-1102-warm-ramp/previous-factory.bin';raw=backup.read_bytes()
pattern=b''.join(struct.pack('<HBxHBB',int(f),*v) for f,v in a.items())
pos=raw.find(pattern)
assert pos>=0, 'Historical table not found in exact backup'
(p/'old-binary-pll-table.json').write_text(json.dumps(dict(entry_count=len(a),struct_layout='<HBxHBB',bytes=len(pattern),offset=pos,offset_hex=hex(pos),full_table_exact_match=True,table_hex=raw[pos:pos+len(pattern)].hex()),indent=2)+'\n')
print('Exact old binary table match:',hex(pos),len(pattern),'bytes')
strings=[x.decode('ascii') for x in re.findall(rb'[\x20-\x7e]{8,}',raw)]
terms=['Ramping Proto Vcore','Started broadcast qualification work','Qualifying','Failed to quiesce MC3','Starting Proto Vcore','Setting TON_RISE','Successfully transitioned']
checks={t:[x for x in strings if t in x] for t in terms}
(p/'old-image-strings.json').write_text(json.dumps(checks,indent=2)+'\n')
print('Old factory SHA256',hashlib.sha256(raw).hexdigest());print(json.dumps(checks,indent=2))
logold=(root/'local/experiments/2026-09-04-1102-warm-log/original.log').read_text()
lognew=(root/'local/experiments/2026-09-04-1102-zero-hold/serial.log').read_text()
for name,s in [('old-boot-evidence',logold),('current-boot-evidence',lognew)]:
 lines=[l for l in s.splitlines() if any(t in l for t in ['PLL0 locked','Writing MC3 mining config','Writing MC3 work job','Vcore ramp reached','Started broadcast qualification','ON_OFF_CONFIG:','TON_RISE:','WARM ZERO','WARM END'])]
 (p/(name+'.txt')).write_text('\n'.join(lines)+'\n')
