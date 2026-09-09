from pathlib import Path
import tempfile,subprocess
s=(Path(__file__).resolve().parents[3]/'components/asic/mc3.c').read_text()
body=s[s.index('static bool mc3_ramp_hash_frequency'):s.index('void MC3_prepare_pool_work')]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define MC3_FREQUENCY_HIGH_THRESHOLD_MHZ 500
#define MC3_FREQUENCY_HIGH_RAMP_DELAY_MS 1000
#define MC3_FREQUENCY_RAMP_DELAY_MS 500
#define pdMS_TO_TICKS(x) (x)
#include <stdint.h>
typedef struct {float frequency_mhz;} mc3_pll_config_t;
static const mc3_pll_config_t PLL_CONFIGS[]={{550},{575},{600}};
typedef struct {float actual_frequency,frequency_value;} PowerManagementModule;
typedef struct {PowerManagementModule POWER_MANAGEMENT_MODULE;} GlobalState;
static int mc3_work_lock,locked,writes,waits,callbacks,quiesces,refreshes,pvts;static bool fail,pvt_fail,work_fail;
static const mc3_pll_config_t *mc3_get_pll_config(float f){for(unsigned i=0;i<3;i++)if(PLL_CONFIGS[i].frequency_mhz==f)return &PLL_CONFIGS[i];assert(0);return NULL;}
static void pthread_mutex_lock(int *p){(void)p;assert(!locked);locked=1;}
static void pthread_mutex_unlock(int *p){(void)p;assert(locked);locked=0;}
static float MC3_send_hash_frequency(float f){assert(locked);writes++;return fail?0:f;}
static void vTaskDelay(int ms){assert(locked && (ms==1000 || ms==10));if(ms==1000)waits++;}
static void callback(void *g,float f){(void)g;(void)f;assert(locked);callbacks++;}
static void (*mc3_frequency_step_callback)(void*,float)=callback;
#define MC3_WORK_CFG 0x400
#define MC3_DEFAULT_WORK_CFG_APPLY_AND_RESET 0x8800D2FF
#define MC3_CHIP_NUM_ALL 255
static bool mc3_write_register(int chip,unsigned reg,unsigned value,unsigned all){assert(locked && chip==0 && reg==MC3_WORK_CFG && value==MC3_DEFAULT_WORK_CFG_APPLY_AND_RESET && all==255);quiesces++;return true;}
static bool MC3_reinitialize_pvt_sensors(void){assert(locked);pvts++;return !pvt_fail;}
static bool mc3_refresh_pool_work_locked(GlobalState *g){(void)g;assert(locked);refreshes++;return !work_fail;}
'''+body+r'''
int main(void){
 GlobalState g={.POWER_MANAGEMENT_MODULE={.actual_frequency=600,.frequency_value=550}};
 assert(MC3_step_hash_frequency(&g));assert(g.POWER_MANAGEMENT_MODULE.actual_frequency==575 && writes==1 && waits==1 && callbacks==1 && !locked);
 assert(MC3_step_hash_frequency(&g));assert(g.POWER_MANAGEMENT_MODULE.actual_frequency==550 && writes==2 && !locked);
 assert(MC3_step_hash_frequency(&g));assert(writes==2 && quiesces==2 && pvts==2 && refreshes==2);
 g.POWER_MANAGEMENT_MODULE.frequency_value=600;assert(MC3_ramp_hash_frequency(&g));assert(g.POWER_MANAGEMENT_MODULE.actual_frequency==600 && writes==4 && !locked);
 g.POWER_MANAGEMENT_MODULE.frequency_value=550;fail=true;assert(!MC3_step_hash_frequency(&g));assert(g.POWER_MANAGEMENT_MODULE.actual_frequency==600 && !locked && waits==4);
 fail=false;pvt_fail=true;assert(!MC3_step_hash_frequency(&g));assert(!locked && refreshes==2);
 g.POWER_MANAGEMENT_MODULE.frequency_value=600;pvt_fail=false;work_fail=true;assert(!MC3_step_hash_frequency(&g));assert(!locked);
 puts("PASS: one PLL table entry per live step, full startup ramp preserved, work mutex held, settle/callback and lock failure handling");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-sign-compare',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
