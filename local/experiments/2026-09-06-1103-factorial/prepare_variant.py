"""Reconstruct archived bench in an isolated worktree; vary two compile-time factors."""
from pathlib import Path
import argparse,re,json,hashlib
p=Path(__file__).resolve().parent;root=p.parents[2]
a=argparse.ArgumentParser();a.add_argument('--warmup',choices=['hot','40c'],required=True);a.add_argument('--launch',choices=['original','passive'],required=True);a.add_argument('--cool-after-hot',action='store_true');args=a.parse_args()
assert not args.cool_after_hot or args.warmup=='hot'
w=Path('/tmp/bitaxe-1103-regression');old=p.parent/'2026-09-05-1103-scale-before-limits'
def fn(s,name):
 m=re.search(r'^(?:static )?bool '+name+r'\([^;]*?\)\n\{',s,re.M);assert m,name
 depth=1;i=m.end()
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i]
s=(old/'asic-init-source.c').read_text()
s=s.replace('#include "asic_init.h"','#include "asic_init.h"\n#include "proto_compare.h"',1)
needle='    if (!MC3_start_qualification_work(global) || atomic_load(&guard->tripped)) return false;\n    return MC3_start_active_spdlog_observation(180000);'
assert s.count(needle)==1
s=s.replace(needle,'''    if (PROTO_COMPARE_PASSIVE_LAUNCH) return MC3_start_qualification_observation();
'''+needle)
needle='if (hottest >= 54.5f && hottest <= 60 && coldest >= 52)'
assert s.count(needle)==1
s=s.replace(needle,'if (hottest <= 60 && (PROTO_COMPARE_LOW_WARMUP ? coldest >= 40.0f : (hottest >= 54.5f && coldest >= 52)))')
needle='    ESP_LOGW(TAG, "WARM BEGIN baseline=300'
i=s.index(needle)
s=s[:i]+'''    ESP_LOGW(TAG, "COMPARE CONFIG warmup=%s launch=%s observation=120s; archived bench base",
        PROTO_COMPARE_LOW_WARMUP ? "all40C" : "hottest54.5C_coldest52C",
        PROTO_COMPARE_PASSIVE_LAUNCH ? "passive_prejob_arm" : "original_postjob_strobe");
'''+s[i:]
# Keep the old warmup banner accurate for the cooler selector.
s=s.replace('target_temp=55 max_frequency=600; fan lease only, NVS unchanged", PROTO_WARM_VOLTAGE_MV);','target_temp=%u max_frequency=600; fan lease only, NVS unchanged", PROTO_WARM_VOLTAGE_MV, PROTO_COMPARE_LOW_WARMUP ? 40 : 55);')
if args.cool_after_hot:
 helper=(p/'cool_after_hot.inc').read_text()
 s=s.replace('static void run_proto_warm_ramp_probe(GlobalState *global)',helper+'\nstatic void run_proto_warm_ramp_probe(GlobalState *global)',1)
 needle='    if (PROTO_WARM_DEFER_WORK) {\n        ok = proto_warm_deferred_work'
 assert s.count(needle)==1
 s=s.replace(needle,'    if (!proto_compare_cool_after_hot(global, &guard)) goto finish;\n'+needle,1)
(w/'main/power/asic_init.c').write_text(s)
(w/'main/power/proto_compare.h').write_text(f'#define PROTO_COMPARE_LOW_WARMUP {int(args.warmup=="40c")}\n#define PROTO_COMPARE_PASSIVE_LAUNCH {int(args.launch=="passive")}\n#define PROTO_COMPARE_COOL_AFTER_HOT {int(args.cool_after_hot)}\n')
# Preserve the archived MC3 implementation; add only the production launch option.
m=(old/'mc3-source.c').read_text();current=(root/'components/asic/mc3.c').read_text()
orig=fn(m,'mc3_write_mining_config')
replacement=fn(current,'mc3_write_mining_config_impl')+'\n\n'+fn(current,'mc3_write_mining_config')
# Verify wrapper introduces only conditional PLL enable into the archived function.
normalized=fn(current,'mc3_write_mining_config_impl').replace('mc3_write_mining_config_impl(uint32_t rolltime, uint32_t version_mask, bool enable_pll)','mc3_write_mining_config(uint32_t rolltime, uint32_t version_mask)').replace('(enable_pll && !mc3_write_register','!mc3_write_register').replace('MC3_DEFAULT_PLL0_ENABLE, MC3_CHIP_NUM_ALL)))','MC3_DEFAULT_PLL0_ENABLE, MC3_CHIP_NUM_ALL))')
assert normalized==orig
m=m.replace(orig,replacement,1)
passive=fn(current,'MC3_start_qualification_observation').replace('        mc3_diagnostic_start_window(1431, true);\n','')
passive=passive.replace('mc3_spdlog_timer_value(1431)', 'mc3_spdlog_timer_value(2146)')  # 180.02s, matching archived observation
# The archived helpers returned void. Keep them unchanged for original launch;
# use the current checked helpers only on the passive path. Successful bus
# transactions/payloads are identical; read/write failures now propagate.
checked_names=['mc3_write_work_target','mc3_write_version_bases','mc3_write_v_work']
checked='\n\n'.join(fn(current,name) for name in checked_names)
for name in checked_names:
 checked=checked.replace(name,name.replace('mc3_','mc3_compare_',1))
 passive=passive.replace(name,name.replace('mc3_','mc3_compare_',1))
m+='\n\n'+checked+'\n\n'+passive+'\n'
(w/'components/asic/mc3.c').write_text(m)
h=w/'components/asic/include/mc3.h';header=h.read_text()
if 'bool MC3_start_qualification_observation(void);' not in header:
 header+='\nbool MC3_start_qualification_observation(void);\n';h.write_text(header)
print(json.dumps({'worktree':str(w),'warmup':args.warmup,'launch':args.launch,'asic_init_sha256':hashlib.sha256(s.encode()).hexdigest(),'mc3_sha256':hashlib.sha256(m.encode()).hexdigest()},indent=2))
