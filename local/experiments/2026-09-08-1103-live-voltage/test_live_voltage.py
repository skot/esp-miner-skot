from pathlib import Path
import subprocess, tempfile

root = Path(__file__).resolve().parents[3]
source = Path(__file__).with_name('power_management_task-tested.c').read_text()
body = source[source.index('static portMUX_TYPE live_voltage_lock'):source.index('static void log_proto_frequency_step_telemetry')]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define PROTO 1
#define MC3 2
#define NVS_CONFIG_ASIC_FREQUENCY 1
#define NVS_CONFIG_ASIC_VOLTAGE 2
#define ESP_OK 0
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
typedef struct {
 struct { struct {int id; struct {int id;} asic;} family; const char *board_version;} DEVICE_CONFIG;
 bool ASIC_initalized;
 struct {bool mining_paused,overheat_mode;} SYSTEM_MODULE;
 struct {bool is_active;} SELF_TEST_MODULE;
 struct {float actual_frequency;} POWER_MANAGEMENT_MODULE;
} GlobalState;
static int64_t now;
static int saved_mv=1200, saved_mhz=600, writes, fail_write;
static float output;
static int64_t esp_timer_get_time(void){return now;}
static float nvs_config_get_float(int k){(void)k;return saved_mhz;}
static uint16_t nvs_config_get_u16(int k){(void)k;return saved_mv;}
static int VCORE_set_voltage(GlobalState *g,float v){(void)g;writes++;output=v;return fail_write;}
''' + body + r'''
int main(void){
 GlobalState g={.DEVICE_CONFIG={.family={.id=PROTO,.asic={.id=MC3}},.board_version="1103"},.ASIC_initalized=true,.POWER_MANAGEMENT_MODULE={.actual_frequency=600}};
 live_voltage_reset();live_voltage_poll(&g);assert(writes==0);
 assert(!POWER_MANAGEMENT_request_live_voltage(&g,1180));
 assert(!POWER_MANAGEMENT_request_live_voltage(&g,1195));
 assert(!POWER_MANAGEMENT_request_live_voltage(&g,1210));
 assert(POWER_MANAGEMENT_request_live_voltage(&g,1190));assert(writes==0);
 live_voltage_poll(&g);assert(writes==1 && output>1.189f && output<1.191f);
 assert(saved_mv==1200 && POWER_MANAGEMENT_live_voltage()==1190);
 now=89000000;live_voltage_poll(&g);assert(writes==1);
 assert(POWER_MANAGEMENT_request_live_voltage(&g,1190));
 now=91000000;live_voltage_poll(&g);assert(writes==1);
 now=180000000;live_voltage_poll(&g);assert(POWER_MANAGEMENT_live_voltage()==1200);
 for(int mv=1190;mv>=1160;mv-=10){assert(POWER_MANAGEMENT_request_live_voltage(&g,mv));live_voltage_poll(&g);}
 assert(!POWER_MANAGEMENT_request_live_voltage(&g,1150));
 assert(POWER_MANAGEMENT_request_live_voltage(&g,1200));
 live_voltage_poll(&g);assert(POWER_MANAGEMENT_live_voltage()==1170);
 live_voltage_poll(&g);live_voltage_poll(&g);live_voltage_poll(&g);assert(POWER_MANAGEMENT_live_voltage()==1200);
 assert(POWER_MANAGEMENT_request_live_voltage(&g,1190));fail_write=1;live_voltage_poll(&g);
 fail_write=0;live_voltage_poll(&g);assert(POWER_MANAGEMENT_live_voltage()==1200);
 assert(POWER_MANAGEMENT_request_live_voltage(&g,1190));g.ASIC_initalized=false;
 int before=writes;live_voltage_poll(&g);assert(writes==before && POWER_MANAGEMENT_live_voltage()==1200);
 assert(!POWER_MANAGEMENT_request_live_voltage(&g,1190));g.ASIC_initalized=true;
 g.SYSTEM_MODULE.mining_paused=true;assert(!POWER_MANAGEMENT_request_live_voltage(&g,1190));g.SYSTEM_MODULE.mining_paused=false;
 g.DEVICE_CONFIG.board_version="1102";assert(!POWER_MANAGEMENT_request_live_voltage(&g,1190));g.DEVICE_CONFIG.board_version="1103";
 saved_mv=1160;assert(!POWER_MANAGEMENT_request_live_voltage(&g,1190));saved_mv=1200;
 saved_mhz=500;assert(!POWER_MANAGEMENT_request_live_voltage(&g,1190));saved_mhz=600;
 puts("PASS: queued voltage-only writes, step bounds, lease renewal/expiry, incremental restore, write failure, stop cancellation and startup-setting/board gating");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
