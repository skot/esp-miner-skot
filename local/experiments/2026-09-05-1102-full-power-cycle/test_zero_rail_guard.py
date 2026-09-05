from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent;s=(p.parents[2]/'main/power/asic_init.c').read_text();start=s.index('if (!valid && total == 0');end=s.index(';',start)+1;condition=s[start:end]
code='''#include <stdbool.h>\n#include <stdint.h>\n#include <stdatomic.h>\n#include <assert.h>\ntypedef struct {atomic_bool zero_rail_expected;int global;} G;\nstatic bool allowed;\nstatic bool proto_warm_report_only(int g){(void)g;return allowed;}\nstatic bool check(bool valid,int16_t total,G *guard){'''+condition+'''return valid;}\nint main(void){G g={0};allowed=true;assert(!check(false,0,&g));atomic_store(&g.zero_rail_expected,true);assert(check(false,0,&g));assert(!check(false,-1,&g));allowed=false;assert(!check(false,0,&g));assert(check(true,1600,&g));}\n'''
with tempfile.TemporaryDirectory() as d:
 f=Path(d)/'test.c';f.write_text(code);out=Path(d)/'test';subprocess.run(['cc','-std=c11',str(f),'-o',str(out)],check=True);subprocess.run([str(out)],check=True)
print('PASS: exact zero only allowed during explicit supported cycle; negative read failures and other-board zero still rejected.')
