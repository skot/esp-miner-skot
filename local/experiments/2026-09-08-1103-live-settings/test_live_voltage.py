from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]
s=(root/'main/tasks/power_management_task.c').read_text()
body=s[s.index('typedef struct {\n    uint16_t requested_mv;'):s.index('static void update_asic_telemetry')]
t=(root/'main/power/asic_init.c').read_text();startup=t[t.index('static uint16_t get_proto_target_core_voltage_mv'):t.index('static esp_err_t set_proto_core_voltage')]
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
typedef struct {float actual_frequency,frequency_value;uint16_t applied_core_voltage_mv;int64_t initialized_us;} PowerManagementModule;
typedef struct {bool ASIC_initalized;struct {bool hardware_fault,mining_paused;} SYSTEM_MODULE;struct {bool is_active;} SELF_TEST_MODULE;struct {const char *board_version;struct {struct {int id;unsigned default_voltage_mv;} asic;} family;} DEVICE_CONFIG;PowerManagementModule POWER_MANAGEMENT_MODULE;} GlobalState;
static int64_t now;static unsigned writes,saved_mv=1180;static bool fail;
static int64_t esp_timer_get_time(void){return now;}
static uint16_t nvs_config_get_u16(int k){(void)k;return saved_mv;}
static int VCORE_set_voltage(GlobalState *g,float v){writes++;if(fail)return -1;g->POWER_MANAGEMENT_MODULE.applied_core_voltage_mv=(unsigned)(v*1000+.5);return 0;}
'''+startup+body+r'''
int main(void){
 GlobalState g={.ASIC_initalized=true,.DEVICE_CONFIG={.board_version="1103",.family={.asic={.id=MC3,.default_voltage_mv=1000}}},.POWER_MANAGEMENT_MODULE={.actual_frequency=600,.frequency_value=600,.applied_core_voltage_mv=1200,.initialized_us=1}};
 proto_live_settings_t s={0};PowerManagementModule *p=&g.POWER_MANAGEMENT_MODULE;
 assert(get_proto_target_core_voltage_mv(&g)==1200 && saved_mv==1180);
 p->frequency_value=575;assert(get_proto_target_core_voltage_mv(&g)==1200);p->frequency_value=300;assert(get_proto_target_core_voltage_mv(&g)==1180);p->frequency_value=600;
 g.DEVICE_CONFIG.board_version="1102";assert(get_proto_target_core_voltage_mv(&g)==1180);g.DEVICE_CONFIG.board_version="1103";
 g.SELF_TEST_MODULE.is_active=true;assert(get_proto_target_core_voltage_mv(&g)==1000);g.SELF_TEST_MODULE.is_active=false;
 saved_mv=1300;assert(get_proto_target_core_voltage_mv(&g)==1300);saved_mv=1180;
 now=1000000;assert(proto_apply_live_voltage(&g,&s,1187,600));now=29000000;assert(proto_apply_live_voltage(&g,&s,1187,600));assert(writes==0);
 now=31000000;assert(proto_apply_live_voltage(&g,&s,1187,600));assert(p->applied_core_voltage_mv==1190);
 assert(proto_apply_live_voltage(&g,&s,1187,600));assert(p->applied_core_voltage_mv==1187);
 assert(proto_apply_live_voltage(&g,&s,1200,600));now+=1100000;assert(proto_apply_live_voltage(&g,&s,1200,600));assert(p->applied_core_voltage_mv==1197);assert(proto_apply_live_voltage(&g,&s,1200,600));assert(p->applied_core_voltage_mv==1200);
 unsigned before=writes;g.ASIC_initalized=false;assert(proto_apply_live_voltage(&g,&s,1160,600));assert(writes==before);g.ASIC_initalized=true;
 g.SYSTEM_MODULE.hardware_fault=true;assert(proto_apply_live_voltage(&g,&s,1160,600));assert(writes==before);g.SYSTEM_MODULE.hardware_fault=false;
 assert(!proto_apply_live_voltage(&g,&s,1300,600));assert(!proto_apply_live_voltage(&g,&s,1180,NAN));
 assert(proto_apply_live_voltage(&g,&s,1180,600));now+=500000;assert(proto_apply_live_voltage(&g,&s,1170,600));now+=500000;assert(proto_apply_live_voltage(&g,&s,1170,600));assert(writes==before);
 now+=600000;fail=true;assert(!proto_apply_live_voltage(&g,&s,1170,600));assert(p->applied_core_voltage_mv==1200);
 puts("PASS: live voltage only, incremental targets, startup voltage separation, 30-second hold, coalescing, pause/fault and write failure");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
