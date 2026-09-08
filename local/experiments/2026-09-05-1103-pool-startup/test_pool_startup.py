"""Compile production startup helpers with injected hardware/state failures; no hardware IO."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]
s=(root/'main/power/asic_init.c').read_text()
start=s.index('// Production startup for the two supported stacked MC3 boards.')
end=s.index('static uint8_t asic_initialize_impl',start)
helpers=s[start:end]
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define ESP_LOGI(tag, ...) ignored_log(__VA_ARGS__)
static void ignored_log(const char *fmt,...){(void)fmt;}
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY 999999
#define pdPASS 1
#define UART_FREQ 115200
#define PROTO_MIN_CHIP_THROUGHPUT_PERCENT 97.5f
#define PROTO_MIN_PASS_RATE_PERCENT 97.0f
#define PROTO_INTERNAL_DOMAIN_COLLAPSE_PERCENT 90.0f
typedef void *SemaphoreHandle_t;
typedef struct {bool ASIC_initalized;struct {bool hardware_fault,mining_paused;const char *asic_status;} SYSTEM_MODULE;struct {const char *board_version;struct {unsigned asic_count;struct {unsigned small_core_count;} asic;} family;bool domain_voltage_sense;} DEVICE_CONFIG;struct {float actual_frequency,frequency_value,expected_hashrate,asic_temps[4],chip_temp_avg;unsigned asic_temp_count;} POWER_MANAGEMENT_MODULE;} GlobalState;
typedef struct {float temperature_c[4];} proto_bcast_observation_t;
typedef struct {float frequency_mhz;unsigned chip_count;float chip_frequency_mhz[4];uint32_t passed[4],failed[4];float hashrate_ghs[4],total_hashrate_ghs;} mc3_qualification_result_t;
typedef struct {uint64_t core_pass_sum;uint32_t domain_pass[4],domain_fail[4];unsigned domain_zero_cores[4];} mc3_core_domain_summary_t;
static GlobalState g;
static int64_t now,last_pll,stop_at,first_job;
static float temperature=55,cold_chip_temperature=55;
static unsigned pll_steps,jobs,voltage_steps,init_calls,temp_after_reset,voltage_drops,pvt_reinits,voltage_diagnostics;
static int supply=2320,fail_pll,chain=4,last_fan,off_calls,adc_error;
static bool diag_hot_after,diag_rail_bad,recovery_failure;
static bool post_start_collapse,counter_read_failure,late_hot,late_pause,cooled_before_job,drop_error,drop_pause,drop_rail_error,reinit_failure;
static bool failed_read,discharge_failure,enable_failure,thermal_failure,warm_timeout,collapsed,weak_core,hot,guard_only;
static void *guard_arg;
static void (*delay_hook)(void);
static int64_t esp_timer_get_time(void){return now;}
static void vTaskDelay(int ms){now+=(int64_t)ms*1000;if(stop_at && now>=stop_at)g.SYSTEM_MODULE.mining_paused=true;if(delay_hook)delay_hook();}
static void vTaskDelete(void *p){(void)p;}
static SemaphoreHandle_t xSemaphoreCreateBinary(void){return &g;}
static int xSemaphoreTake(SemaphoreHandle_t s,int t){(void)s;(void)t;return 1;}
static void xSemaphoreGive(SemaphoreHandle_t s){(void)s;}
static void vSemaphoreDelete(SemaphoreHandle_t s){(void)s;}
static int xTaskCreate(void (*fn)(void*),const char *name,unsigned stack,void *arg,int priority,void *out){(void)fn;(void)name;(void)stack;(void)priority;(void)out;guard_arg=arg;return 1;}
static int16_t VCORE_get_regulator_voltage_mv_fresh(GlobalState *v){(void)v;return (diag_rail_bad && pll_steps==20)?2300:failed_read?-1:discharge_failure?2320:(drop_rail_error && voltage_drops)?2400:supply;}
static int ADC_get_domain_midpoint_mv(uint16_t *raw){*raw=800;return adc_error;}
static int VCORE_get_domain_voltages_mv(GlobalState *v,int total,uint16_t *l,uint16_t *u){(void)v;*l=*u=total/2;return adc_error;}
static int asic_prepare_power(GlobalState *v){(void)v;supply=0;off_calls++;return 0;}
static int VCORE_set_voltage(GlobalState *v,float volts){(void)v;assert(volts==0);supply=0;return 0;}
static int enable_proto_power(GlobalState *v,unsigned mv){(void)v;assert(supply==0 && mv==800);if(enable_failure)return -1;supply=1600;return 0;}
static void SERIAL_set_baud(int baud){assert(baud==UART_FREQ);}
static void SERIAL_clear_buffer(void){}
static int asic_reset(void){return 0;}
static void clear_asic_chain_error(void){}
static void mc3_diagnostics_finish_startup(bool passed){(void)passed;}
static int MC3_init(GlobalState *v){assert(supply==1600);init_calls++;v->POWER_MANAGEMENT_MODULE.actual_frequency=100;return chain;}
static int set_proto_core_voltage(GlobalState *v,unsigned mv){if(v->POWER_MANAGEMENT_MODULE.actual_frequency==600){assert(mv==1160 && supply==2400 && jobs==0 && temp_after_reset==0 && pll_steps==20);voltage_drops++;if(drop_error)return -1;supply=2320;if(drop_pause)g.SYSTEM_MODULE.mining_paused=true;return 0;}assert(v->POWER_MANAGEMENT_MODULE.actual_frequency==100 && jobs==0 && mv==810+voltage_steps*10);voltage_steps++;supply=2*mv;return 0;}
static void FAN_CONTROLLER_set_startup_percent(int p){last_fan=p;}
static void proto_warm_fan(float hottest,int64_t elapsed){(void)hottest;(void)elapsed;last_fan=20;}
static int MC3_read_temperatures(float *out,unsigned n){assert(n==4);if(init_calls){assert(g.POWER_MANAGEMENT_MODULE.actual_frequency==600 && now-last_pll>=1500000);temp_after_reset++;}for(unsigned i=0;i<4;i++)out[i]=(cooled_before_job && init_calls)?39.9f:warm_timeout?39:(hot || (late_hot && jobs) || (diag_hot_after && pvt_reinits))?71:(i==3?cold_chip_temperature:temperature);if(init_calls && ((thermal_failure && !pvt_reinits) || (recovery_failure && pvt_reinits)))out[2]=-1;return 4;}
static void fill_result(GlobalState *v,mc3_qualification_result_t *r){memset(r,0,sizeof(*r));r->chip_count=4;r->frequency_mhz=v->POWER_MANAGEMENT_MODULE.actual_frequency;for(unsigned c=0;c<4;c++){r->chip_frequency_mhz[c]=r->frequency_mhz;r->passed[c]=9900;r->failed[c]=100;r->hashrate_ghs[c]=r->frequency_mhz*1.248f*.99f;r->total_hashrate_ghs+=r->hashrate_ghs[c];}}
static bool MC3_qualify_frequency(GlobalState *v,unsigned f,mc3_qualification_result_t *r){assert(!init_calls);v->POWER_MANAGEMENT_MODULE.actual_frequency=f;now+=4200000;fill_result(v,r);return true;}
static bool MC3_measure_active_frequency_profile(GlobalState *v,mc3_qualification_result_t *r){if(init_calls)assert(jobs==1 && v->POWER_MANAGEMENT_MODULE.actual_frequency==600);now+=4200000;fill_result(v,r);return true;}
static bool MC3_read_core_domain_summary(unsigned c,mc3_core_domain_summary_t *d){(void)c;*d=(mc3_core_domain_summary_t){.core_pass_sum=40000,.domain_pass={10000,10000,10000,10000}};if(init_calls && jobs){if(counter_read_failure)return false;now+=100000;uint32_t count=(uint32_t)((now-first_job-50000)*185.0/16777.216);d->core_pass_sum=(uint64_t)count*4;for(unsigned k=0;k<4;k++)d->domain_pass[k]=count;}if(weak_core && c==0){d->core_pass_sum-=d->domain_pass[2]*3/100;d->domain_pass[2]-=d->domain_pass[2]*3/100;d->domain_zero_cores[2]=1;}if(collapsed || (post_start_collapse && jobs)){d->core_pass_sum-=d->domain_pass[2];d->domain_pass[2]=0;}return true;}
static float MC3_send_hash_frequency(float f){assert(init_calls==1 && jobs==0 && temp_after_reset==0 && voltage_steps==(unsigned)(supply-1600)/20 && (supply==2320 || supply==2360 || supply==2400));assert(f==125+25*pll_steps);if(pll_steps)assert(now-last_pll==((f-25)>=300?2000000:500000));pll_steps++;last_pll=now;return f==fail_pll?0:f;}
static bool MC3_start_qualification_observation(void){GlobalState *v=&g;assert(init_calls==1 && temp_after_reset==(pvt_reinits?3u:1u) && now-last_pll>=1500000 && v->POWER_MANAGEMENT_MODULE.actual_frequency==600);jobs++;first_job=now;if(late_pause)stop_at=now+5000000;return true;}
static void MC3_log_voltage_diagnostic(void){assert(init_calls==1 && jobs==0 && supply>0);assert(temp_after_reset==1 || (supply==2400 && temp_after_reset==2 && pvt_reinits==1));voltage_diagnostics++;now+=1500000;}
static bool MC3_reinitialize_pvt_sensors(void){assert(init_calls==1 && jobs==0 && temp_after_reset==1 && voltage_drops==0 && supply==2400 && pll_steps==20 && now-last_pll>=2500000 && voltage_diagnostics==1);pvt_reinits++;return !reinit_failure;}
'''+helpers+r'''
static void stop_guard(void){atomic_store(&((proto_startup_guard_t*)guard_arg)->stop,true);}
static void setup(void){diag_hot_after=diag_rail_bad=recovery_failure=false;g=(GlobalState){.DEVICE_CONFIG={.board_version="1103",.family={.asic_count=4,.asic={1248}},.domain_voltage_sense=true},.POWER_MANAGEMENT_MODULE={.frequency_value=600,.actual_frequency=100}};now=last_pll=stop_at=0;pll_steps=jobs=voltage_steps=init_calls=temp_after_reset=voltage_drops=pvt_reinits=voltage_diagnostics=0;supply=2320;fail_pll=0;chain=4;last_fan=off_calls=adc_error=0;failed_read=discharge_failure=enable_failure=thermal_failure=warm_timeout=collapsed=weak_core=hot=guard_only=false;delay_hook=NULL;temperature=cold_chip_temperature=55;post_start_collapse=counter_read_failure=late_hot=late_pause=cooled_before_job=drop_error=drop_pause=drop_rail_error=reinit_failure=false;}
int main(void){
 setup();assert(proto_warmup_voltage_mv(&g,1200)==1160);assert(proto_warmup_voltage_mv(&g,1160)==1160);assert(proto_warmup_voltage_mv(&g,1180)==1180);
 setup();g.DEVICE_CONFIG.board_version="1102";assert(proto_warmup_voltage_mv(&g,1200)==1200);
 setup();g.POWER_MANAGEMENT_MODULE.frequency_value=300;assert(proto_warmup_voltage_mv(&g,1200)==1200);
 setup();weak_core=true;assert(run_proto_pool_startup(&g,1200));assert(pll_steps==20 && jobs==1 && voltage_steps==40 && voltage_drops==0 && pvt_reinits==1 && voltage_diagnostics==2 && temp_after_reset==14 && last_fan==-1 && off_calls==1 && supply==2400);
 setup();thermal_failure=true;assert(run_proto_pool_startup(&g,1200));assert(pvt_reinits==1 && voltage_diagnostics==2 && jobs==1 && supply==2400);
 setup();recovery_failure=true;assert(!run_proto_pool_startup(&g,1200));assert(pvt_reinits==1 && voltage_diagnostics==1 && jobs==0 && supply==0);
 setup();temperature=cold_chip_temperature=40;assert(run_proto_pool_startup(&g,1200));assert(jobs==1 && supply==2400);
 setup();cold_chip_temperature=39.9;assert(!run_proto_pool_startup(&g,1200));assert(init_calls==0 && jobs==0 && supply==0);
 setup();cooled_before_job=true;assert(!run_proto_pool_startup(&g,1200));assert(pvt_reinits==1 && jobs==0 && supply==0);
 setup();diag_rail_bad=true;assert(!run_proto_pool_startup(&g,1200));assert(pll_steps==20 && pvt_reinits==0 && temp_after_reset==0 && jobs==0 && supply==0);
 setup();diag_hot_after=true;assert(!run_proto_pool_startup(&g,1200));assert(pvt_reinits==1 && voltage_diagnostics==1 && temp_after_reset==2 && jobs==0 && supply==0);
 setup();reinit_failure=true;assert(!run_proto_pool_startup(&g,1200));assert(pvt_reinits==1 && voltage_diagnostics==1 && temp_after_reset==1 && jobs==0 && supply==0);
 setup();post_start_collapse=true;assert(!run_proto_pool_startup(&g,1200));assert(jobs==1 && supply==0);
 setup();counter_read_failure=true;assert(!run_proto_pool_startup(&g,1200));assert(jobs==1 && supply==0);
 setup();late_hot=true;assert(!run_proto_pool_startup(&g,1200));assert(jobs==1 && supply==0);
 setup();late_pause=true;assert(!run_proto_pool_startup(&g,1200));assert(jobs==1 && supply==0 && now-first_job<6000000);
 setup();fail_pll=475;assert(!run_proto_pool_startup(&g,1200));assert(pvt_reinits==0 && jobs==0 && supply==0);
 setup();chain=3;assert(!run_proto_pool_startup(&g,1200));assert(jobs==0 && pll_steps==0 && supply==0);
 setup();discharge_failure=true;assert(!run_proto_pool_startup(&g,1200));assert(init_calls==0 && supply==0);
 setup();enable_failure=true;assert(!run_proto_pool_startup(&g,1200));assert(init_calls==0 && supply==0);
 setup();hot=true;assert(!run_proto_pool_startup(&g,1200));assert(init_calls==0 && supply==0);
 setup();stop_at=55000000;assert(!run_proto_pool_startup(&g,1200));assert(jobs==0 && supply==0);
 setup();g.DEVICE_CONFIG.board_version="1102";weak_core=true;assert(run_proto_pool_startup(&g,1160));assert(jobs==1 && voltage_steps==36 && supply==2320 && pvt_reinits==0);
 setup();assert(run_proto_pool_startup(&g,1160));assert(jobs==1 && supply==2320 && pvt_reinits==0);
 setup();assert(run_proto_pool_startup(&g,1180));assert(jobs==1 && supply==2360 && pvt_reinits==0);
 setup();assert(!run_proto_pool_startup(&g,1201));assert(!jobs);
 setup();g.POWER_MANAGEMENT_MODULE.frequency_value=300;assert(run_proto_pool_startup(&g,1160));assert(!init_calls && !pvt_reinits);
 setup();g.POWER_MANAGEMENT_MODULE.frequency_value=NAN;assert(!run_proto_pool_startup(&g,1200));
 setup();g.ASIC_initalized=true;assert(!run_proto_pool_startup(&g,1200));

 for(int mode=0;mode<4;mode++){
  setup();proto_startup_guard_t guard={.global=&g};guard_arg=&guard;delay_hook=stop_guard;
  if(mode==0){supply=0;atomic_store(&guard.cycling,true);}
  if(mode==1)failed_read=true;
  if(mode==2){supply=682;atomic_store(&guard.cycling,true);adc_error=-1;}
  if(mode==3)g.SYSTEM_MODULE.mining_paused=true;
  proto_startup_guard_task(&guard);
  assert(atomic_load(&guard.tripped)==(mode!=0));
 }
 puts("PASS: 1160 mV warmup selection, 1200 mV PVT recovery before first work, no voltage drop, pool handoff, failed/hot sensors and domain collapse shutdown, other operating points unchanged.");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
# Handoff is after the startup gate, and all error exits go through shutdown.
impl=s[s.index('static uint8_t asic_initialize_impl'):s.index('\nuint8_t asic_initialize(')]
assert impl.index('run_proto_pool_startup(GLOBAL_STATE') < impl.index('GLOBAL_STATE->ASIC_initalized = true')
assert 'asic_prepare_power(global);' in s[s.index('\nuint8_t asic_initialize('):]

assert impl.index('!proto_pool_startup_settings_valid(GLOBAL_STATE') < impl.index('enable_proto_power(GLOBAL_STATE')

assert "? proto_warmup_voltage_mv(GLOBAL_STATE, target_core_voltage_mv)" in impl
