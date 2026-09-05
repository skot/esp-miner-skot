"""Compile actual production helpers against injected rail/IO failures."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]
asrc=(root/'main/power/asic_init.c').read_text()
vsrc=(root/'main/power/vcore.c').read_text()
h=(root/'main/power/TPS546.h').read_text()
def function(src,name):
 start=src.index('static ',src.rfind('\n',0,src.index(name))+1)
 brace=src.index('{',src.index(name,start));depth=1;i=brace+1
 while depth:
  depth+=(src[i]=='{')-(src[i]=='}');i+=1
 return src[start:i]
struct=h[h.rfind('typedef struct',0,h.index('} TPS546_CONFIG;')):h.index('} TPS546_CONFIG;')+len('} TPS546_CONFIG;')]
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define PROTO_CLEAN_ALL_RECOVERY_MAX_DOMAIN_MV 1200
static bool run_warm=false,report_only=false,ceramic=false,pmbus_only=false;
#define PROTO_1103_PMBUS_ONLY pmbus_only
#define PROTO_1103_CERAMIC_OUTPUT ceramic
static const char *warm_board="1102";
#define PROTO_RUN_WARM_RAMP_PROBE run_warm
#define PROTO_WARM_REPORT_ONLY report_only
#define PROTO_WARM_BOARD_VERSION warm_board
#define TPS546_INIT_PHASE_SINGLE 0
#define TPS546_INIT_PHASE_MULTI 255
enum { GAMMA_TURBO, PROTO, HEX, SUPRA_HEX, GAMMA };
typedef struct {int id;} FamilyConfig;
typedef struct {FamilyConfig family; const char *board_version; bool asic_enable,asic_enable_active_high,domain_voltage_sense;} DeviceConfig;
typedef struct {DeviceConfig DEVICE_CONFIG;} GlobalState;
static int16_t rail=2280;
static uint16_t lo=1140,hi=1140;
static int adc_error=0,gpio_error=0,reg_error=0,gpio_calls=0,reg_calls=0,adc_calls=0;
static esp_err_t VCORE_prepare_asic_enable(GlobalState *g) {(void)g;gpio_calls++;return gpio_error;}
static esp_err_t VCORE_set_voltage(GlobalState *g,float v) {(void)g;assert(v==0);assert(gpio_calls>0);reg_calls++;return reg_error;}
static int16_t VCORE_get_regulator_voltage_mv_fresh(GlobalState *g) {(void)g;return rail;}
static esp_err_t VCORE_get_domain_voltages_mv(GlobalState *g,uint16_t total,uint16_t *l,uint16_t *u) {(void)g;(void)total;adc_calls++;*l=lo;*u=hi;return adc_error;}
static esp_err_t ADC_get_domain_midpoint_mv(uint16_t *v) {adc_calls++;*v=lo;return adc_error;}
'''+struct+'\n'+function(vsrc,'get_tps546_config(')+'\n'+function(asrc,'proto_probe_power_off(')+'\n'+function(asrc,'proto_warm_report_only(')+'\n'+function(asrc,'proto_probe_read_rails(')+r'''
int main(void) {
 GlobalState g={.DEVICE_CONFIG={.family={PROTO},.board_version="1102",.asic_enable=true,.asic_enable_active_high=true}};
 const uint8_t c1102[]={0x13,0x20,0xC6,0x19,0xC6},c1103[]={0x13,0x11,0x8C,0x11,0x86};
 TPS546_CONFIG c=get_tps546_config(&g.DEVICE_CONFIG);
 assert(c.TPS546_INIT_FREQUENCY==1300 && c.TPS546_INIT_SCALE_LOOP==.125f);
 assert(!memcmp(c.TPS546_INIT_COMPENSATION_CONFIG,c1102,5));
 int16_t total;uint16_t lower,upper;
 assert(proto_probe_read_rails(&g,&total,&lower,&upper,false));
 assert(total==2280 && lower==0 && upper==0 && adc_calls==0);
 rail=2399;assert(proto_probe_read_rails(&g,&total,&lower,&upper,false));
 rail=2400;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));
 rail=0;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));
 rail=-1;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));
 rail=2280;g.DEVICE_CONFIG.board_version="unknown";assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));
 g.DEVICE_CONFIG.board_version=NULL;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));
 g.DEVICE_CONFIG.board_version="1102";
 assert(proto_probe_power_off(&g)==ESP_OK && gpio_calls==1 && reg_calls==1);
 reg_error=-5;assert(proto_probe_power_off(&g)==-5);reg_error=0;
 gpio_error=-6;assert(proto_probe_power_off(&g)==-6);gpio_error=0;
 g.DEVICE_CONFIG.board_version="1103";g.DEVICE_CONFIG.domain_voltage_sense=true;
 c=get_tps546_config(&g.DEVICE_CONFIG);
 assert(c.TPS546_INIT_FREQUENCY==550 && c.TPS546_INIT_SCALE_LOOP==.25f);
 assert(!memcmp(c.TPS546_INIT_COMPENSATION_CONFIG,c1103,5));
 assert(proto_probe_read_rails(&g,&total,&lower,&upper,false));
 lo=1200;hi=1080;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));
 lo=1080;hi=1200;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));
 lo=hi=1140;adc_error=-1;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));adc_error=0;
 g.DEVICE_CONFIG.domain_voltage_sense=false;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));
 int old_reg=reg_calls;assert(proto_probe_power_off(&g)==ESP_OK && reg_calls==old_reg);
 g.DEVICE_CONFIG.family.id=GAMMA;c=get_tps546_config(&g.DEVICE_CONFIG);assert(c.TPS546_INIT_FREQUENCY==650);
 g.DEVICE_CONFIG.family.id=PROTO;g.DEVICE_CONFIG.board_version="1102";rail=2800;
 run_warm=true;report_only=true;assert(proto_probe_read_rails(&g,&total,&lower,&upper,false));
 rail=0;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));rail=2800;
 run_warm=false;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));run_warm=true;
 report_only=false;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));report_only=true;
 warm_board="1103";assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));warm_board="1102";
 g.DEVICE_CONFIG.board_version="1103";assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));

 warm_board="1103";g.DEVICE_CONFIG.domain_voltage_sense=true;
 lo=1400;hi=1400;assert(proto_probe_read_rails(&g,&total,&lower,&upper,false));
 adc_error=-1;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));
 rail=13;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,true));
 rail=682;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,true));
 adc_error=0;assert(proto_probe_read_rails(&g,&total,&lower,&upper,true));
 rail=13;assert(proto_probe_read_rails(&g,&total,&lower,&upper,true));
 assert(lower==0 && upper==0);
 rail=100;assert(proto_probe_read_rails(&g,&total,&lower,&upper,true));
 rail=101;assert(proto_probe_read_rails(&g,&total,&lower,&upper,true));
 rail=-1;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,true));
 rail=0;assert(proto_probe_read_rails(&g,&total,&lower,&upper,true));
 assert(!proto_probe_read_rails(&g,&total,&lower,&upper,false));
 report_only=false;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,true));report_only=true;
 warm_board="1102";assert(!proto_probe_read_rails(&g,&total,&lower,&upper,true));warm_board="1103";
 g.DEVICE_CONFIG.domain_voltage_sense=false;assert(!proto_probe_read_rails(&g,&total,&lower,&upper,true));
 g.DEVICE_CONFIG.board_version="unknown";warm_board="unknown";assert(!proto_warm_report_only(&g));
 g.DEVICE_CONFIG.board_version="1103";ceramic=true;c=get_tps546_config(&g.DEVICE_CONFIG);
 assert(c.TPS546_INIT_FREQUENCY==550 && c.TPS546_INIT_SCALE_LOOP==.25f && c.TPS546_INIT_COMPENSATION_CONFIG[3]==0x19);
 g.DEVICE_CONFIG.board_version="1102";c=get_tps546_config(&g.DEVICE_CONFIG);
 assert(c.TPS546_INIT_FREQUENCY==1300 && c.TPS546_INIT_SCALE_LOOP==.125f && !memcmp(c.TPS546_INIT_COMPENSATION_CONFIG,c1102,5));

 pmbus_only=true;g.DEVICE_CONFIG.board_version="1103";g.DEVICE_CONFIG.asic_enable=false;
 c=get_tps546_config(&g.DEVICE_CONFIG);assert(c.TPS546_INIT_IGNORE_CONTROL_PIN && c.TPS546_INIT_SCALE_LOOP==.25f);
 int before=reg_calls;assert(proto_probe_power_off(&g)==ESP_OK && reg_calls==before+1);
 g.DEVICE_CONFIG.board_version="1102";c=get_tps546_config(&g.DEVICE_CONFIG);assert(!c.TPS546_INIT_IGNORE_CONTROL_PIN);
 puts("PASS: matching 1102/1103 bench selection, mandatory 1103 ADC, normal voltage limits and bounded discharge exception;");
 puts("PASS: 1102/1103 regulator selection, voltage boundaries, missing/failed sensing, GPIO/PMBus shutdown and error propagation");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
