from pathlib import Path
import tempfile,subprocess
p=Path(__file__).resolve().parent;root=p.parents[2]
def fn(s,name):
 start=s.rfind('\n',0,s.index(name+'('))+1;brace=s.index('{',start);depth=1;i=brace+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
tps=(root/'main/power/TPS546.c').read_text();vc=(root/'main/power/vcore.c').read_text()
common='''#include <assert.h>\n#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n#include <math.h>\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG -2\n#define ESP_LOGE(...) ((void)0)\n#define PMBUS_READ_VOUT 0x8B\nstatic float last_vout=2.3f, decoded;\nstatic int read_error;\nstatic int smb_read_word(int reg,uint16_t *v){assert(reg==PMBUS_READ_VOUT);*v=7;return read_error;}\nstatic float ulinear16_2_float(uint16_t v){assert(v==7);return decoded;}\ntypedef struct {struct {bool TPS546;int regulator_voltage_offset_mv;} DEVICE_CONFIG;} GlobalState;\n'''
code=common+fn(tps,'TPS546_read_vout')+'\n'+fn(tps,'TPS546_get_vout')+'\n'+fn(vc,'VCORE_get_regulator_voltage_mv_fresh')+'''
int main(void){
 float out=99;GlobalState g={.DEVICE_CONFIG={.TPS546=true,.regulator_voltage_offset_mv=13}};
 read_error=-5;assert(TPS546_read_vout(&out)==-5 && out==99);assert(TPS546_get_vout()==2.3f);
 assert(VCORE_get_regulator_voltage_mv_fresh(&g)==-1);
 read_error=0;decoded=0;assert(VCORE_get_regulator_voltage_mv_fresh(&g)==13);
 decoded=2.0f;assert(VCORE_get_regulator_voltage_mv_fresh(&g)==2013);
 g.DEVICE_CONFIG.regulator_voltage_offset_mv=0;assert(VCORE_get_regulator_voltage_mv_fresh(&g)==2000);
 read_error=-5;assert(VCORE_get_regulator_voltage_mv_fresh(&g)==-1 && TPS546_get_vout()==2.0f);
 read_error=0;decoded=NAN;assert(VCORE_get_regulator_voltage_mv_fresh(&g)==-1);
 decoded=INFINITY;assert(VCORE_get_regulator_voltage_mv_fresh(&g)==-1);
 decoded=-1;assert(VCORE_get_regulator_voltage_mv_fresh(&g)==-1);
 decoded=31;assert(VCORE_get_regulator_voltage_mv_fresh(&g)==-1);
 assert(TPS546_read_vout(NULL)==ESP_ERR_INVALID_ARG);
 assert(VCORE_get_regulator_voltage_mv_fresh(NULL)==-1);
 g.DEVICE_CONFIG.TS546=false;
 return 0;
}
'''.replace('g.DEVICE_CONFIG.TS546=false;','g.DEVICE_CONFIG.TPS546=false;assert(VCORE_get_regulator_voltage_mv_fresh(&g)==-1);')
with tempfile.TemporaryDirectory() as d:
 f=Path(d)/'test.c';f.write_text(code);out=Path(d)/'test'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(f),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('PASS: fresh PMBus errors never become cached discharge/rail measurements; ordinary cached telemetry retained; zero offsets, invalid numeric data and unsupported hardware checked.')
