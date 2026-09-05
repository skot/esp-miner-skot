from pathlib import Path
import tempfile,subprocess
p=Path(__file__).resolve().parent;s=(p.parents[2]/'main/power/TPS546.c').read_text()
def fn(name):
 a=s.rfind('\n',0,s.index(name+'('))+1;b=s.index('{',a);depth=1;i=b+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
init=fn('TPS546_init');write=fn('TPS546_write_entire_config')
assert init.index('smb_write_byte(PMBUS_OPERATION')<init.index('TPS546_prepare_voltage_scale()')<init.index('TPS546_write_entire_config()')
assert 'PMBUS_VOUT_SCALE_LOOP' not in write
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_RESPONSE -3
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_RETURN_ON_ERROR(x,...) do {int e=(x);if(e)return e;} while(0)
#define ESP_RETURN_ON_FALSE(x,e,...) do {if(!(x))return (e);} while(0)
#define PMBUS_OPERATION 1
#define PMBUS_VOUT_SCALE_LOOP 41
#define OPERATION_OFF 0
static bool seed=false;
#define TPS546_BENCH_SEED_SCALE_0125 seed
static struct {float TPS546_INIT_SCALE_LOOP;} tps546_config;
static int byte_error,word_error,fail_write,writes;
static uint8_t operation;static uint16_t scale,values[4];static bool mismatch;
static uint16_t float_2_slinear11(float f){return f*1000;}
static float slinear11_2_float(uint16_t v){return v/1000.f;}
static int smb_read_byte(int reg,uint8_t *v){assert(reg==PMBUS_OPERATION);*v=operation;return byte_error;}
static int smb_write_word(int reg,uint16_t v){assert(reg==PMBUS_VOUT_SCALE_LOOP);values[writes++]=v;if(writes==fail_write)return -7;scale=v;return 0;}
static int smb_read_word(int reg,uint16_t *v){assert(reg==PMBUS_VOUT_SCALE_LOOP);*v=mismatch?500:scale;return word_error;}
'''+fn('TPS546_prepare_voltage_scale')+r'''
static void reset(void){byte_error=word_error=fail_write=writes=0;operation=0;scale=125;mismatch=seed=false;tps546_config.TPS546_INIT_SCALE_LOOP=.25f;}
int main(void){
 reset();assert(TPS546_prepare_voltage_scale()==0 && writes==1 && scale==250);
 reset();seed=true;assert(TPS546_prepare_voltage_scale()==0 && writes==2 && values[0]==125 && values[1]==250);
 reset();seed=true;tps546_config.TPS546_INIT_SCALE_LOOP=.125f;assert(TPS546_prepare_voltage_scale()==0 && writes==1 && scale==125);
 reset();operation=0x80;assert(TPS546_prepare_voltage_scale()==-2 && writes==0);
 reset();byte_error=-4;assert(TPS546_prepare_voltage_scale()==-4 && writes==0);
 reset();fail_write=1;assert(TPS546_prepare_voltage_scale()==-7 && writes==1);
 reset();seed=true;fail_write=1;assert(TPS546_prepare_voltage_scale()==-7 && writes==1);
 reset();seed=true;fail_write=2;assert(TPS546_prepare_voltage_scale()==-7 && writes==2);
 reset();word_error=-5;assert(TPS546_prepare_voltage_scale()==-5);
 reset();mismatch=true;assert(TPS546_prepare_voltage_scale()==-3);
 puts("PASS: converter-off check, scale before entire voltage configuration, readback/write failure handling, explicit 0.125 starting-state probe and unchanged1102 scale.");
}
'''
with tempfile.TemporaryDirectory() as d:
 f=Path(d)/'test.c';f.write_text(code);exe=Path(d)/'test'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(f),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
