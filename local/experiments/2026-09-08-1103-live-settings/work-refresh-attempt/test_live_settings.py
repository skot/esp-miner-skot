from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]
s=(root/'main/tasks/power_management_task.c').read_text()
body=s[s.index('typedef struct {\n    uint16_t requested_mv;'):s.index('static void update_asic_telemetry')]
startup=(root/'main/power/asic_init.c').read_text()
startup=startup[startup.index('static uint16_t get_proto_target_core_voltage_mv'):startup.index('static esp_err_t set_proto_core_voltage')]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define MC3 3
#define ESP_OK 0
#define NVS_CONFIG_ASIC_VOLTAGE 1
#define ESP_LOGI(...) ((void)0)
typedef struct {float actual_frequency,frequency_value,expected_hashrate; uint16_t applied_core_voltage_mv; int64_t initialized_us;} PowerManagementModule;
typedef struct {bool ASIC_initalized; struct {bool hardware_fault,mining_paused;} SYSTEM_MODULE;struct {bool is_active;} SELF_TEST_MODULE;struct {const char *board_version;struct {unsigned asic_count;struct {int id;unsigned small_core_count,default_voltage_mv;} asic;} family;} DEVICE_CONFIG;PowerManagementModule POWER_MANAGEMENT_MODULE;} GlobalState;
static int64_t now;static unsigned writes,plls;static bool write_fail,pll_fail,pvt_fail;static unsigned pvts;static unsigned saved_mv=1180;
static int64_t esp_timer_get_time(void){return now;}
static uint16_t nvs_config_get_u16(int k){(void)k;return saved_mv;}
static float MC3_supported_frequency(float f){return roundf(f/25)*25;}
static int VCORE_set_voltage(GlobalState *g,float v){writes++;if(write_fail)return -1;g->POWER_MANAGEMENT_MODULE.applied_core_voltage_mv=(unsigned)(v*1000+.5);return 0;}
static bool MC3_step_hash_frequency(GlobalState *g){plls++;if(pll_fail)return false;g->POWER_MANAGEMENT_MODULE.actual_frequency += g->POWER_MANAGEMENT_MODULE.actual_frequency<g->POWER_MANAGEMENT_MODULE.frequency_value?25:-25;pvts++;return !pvt_fail;}
'''+startup+body+r'''
int main(void){
 GlobalState g={.ASIC_initalized=true,.DEVICE_CONFIG={.board_version="1103",.family={.asic_count=4,.asic={.id=MC3,.small_core_count=1248,.default_voltage_mv=1000}}},.POWER_MANAGEMENT_MODULE={.actual_frequency=600,.frequency_value=600,.applied_core_voltage_mv=1200,.initialized_us=1}};
 proto_live_settings_t s={0};PowerManagementModule *p=&g.POWER_MANAGEMENT_MODULE;
 assert(get_proto_target_core_voltage_mv(&g)==1200);assert(saved_mv==1180);
 g.DEVICE_CONFIG.board_version="1102";assert(get_proto_target_core_voltage_mv(&g)==1180);g.DEVICE_CONFIG.board_version="1103";
 g.SELF_TEST_MODULE.is_active=true;assert(get_proto_target_core_voltage_mv(&g)==1000);g.SELF_TEST_MODULE.is_active=false;
 saved_mv=1300;assert(get_proto_target_core_voltage_mv(&g)==1300);saved_mv=1180;
 now=1000000;assert(proto_apply_live_settings(&g,&s,1180,600));
 now=29000000;assert(proto_apply_live_settings(&g,&s,1180,600));assert(!writes && !plls);
 now=31000000;assert(proto_apply_live_settings(&g,&s,1180,600));assert(p->applied_core_voltage_mv==1190 && writes==1 && !plls);
 assert(proto_apply_live_settings(&g,&s,1180,600));assert(p->applied_core_voltage_mv==1180 && !plls);
 // Combined reduction: lower clock before lowering voltage.
 assert(proto_apply_live_settings(&g,&s,1160,550));now+=1100000;
 assert(proto_apply_live_settings(&g,&s,1160,550));assert(p->actual_frequency==575 && p->applied_core_voltage_mv==1180);
 assert(proto_apply_live_settings(&g,&s,1160,550));assert(p->actual_frequency==550 && p->applied_core_voltage_mv==1180);
 assert(proto_apply_live_settings(&g,&s,1160,550));assert(p->applied_core_voltage_mv==1170);
 assert(proto_apply_live_settings(&g,&s,1160,550));assert(p->applied_core_voltage_mv==1160);
 // Combined increase: voltage first, then PLL. No NVS writes or resets.
 assert(proto_apply_live_settings(&g,&s,1200,600));now+=1100000;
 for(unsigned i=0;i<4;i++){assert(proto_apply_live_settings(&g,&s,1200,600));assert(p->actual_frequency==550);}
 assert(p->applied_core_voltage_mv==1200);assert(proto_apply_live_settings(&g,&s,1200,600));assert(p->actual_frequency==575);
 assert(proto_apply_live_settings(&g,&s,1200,600));assert(p->actual_frequency==600 && saved_mv==1180 && pvts==4);
 // Invalid input, stop/pause, coalescing and failures.
 assert(!proto_apply_live_settings(&g,&s,1300,600));assert(!proto_apply_live_settings(&g,&s,1200,NAN));assert(!proto_apply_live_settings(&g,&s,1200,625));
 unsigned before=writes;g.ASIC_initalized=false;assert(proto_apply_live_settings(&g,&s,1160,500));assert(writes==before);g.ASIC_initalized=true;
 g.SYSTEM_MODULE.mining_paused=true;assert(proto_apply_live_settings(&g,&s,1160,500));assert(writes==before);g.SYSTEM_MODULE.mining_paused=false;
 assert(proto_apply_live_settings(&g,&s,1190,600));now+=500000;
 assert(proto_apply_live_settings(&g,&s,1180,600));now+=500000;assert(proto_apply_live_settings(&g,&s,1180,600));assert(writes==before);
 now+=600000;write_fail=true;assert(!proto_apply_live_settings(&g,&s,1180,600));assert(p->applied_core_voltage_mv==1200);write_fail=false;
 assert(proto_apply_live_settings(&g,&s,1200,575));now+=1100000;pll_fail=true;assert(!proto_apply_live_settings(&g,&s,1200,575));assert(p->actual_frequency==600);pll_fail=false;pvt_fail=true;assert(!proto_apply_live_settings(&g,&s,1200,575));assert(p->actual_frequency==575);
 puts("PASS: live settings order, gradual voltage, bounded PLL steps, no reset, startup boost/hold, coalescing, range checks and failures");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
