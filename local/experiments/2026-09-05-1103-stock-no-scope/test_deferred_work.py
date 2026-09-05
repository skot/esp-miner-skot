from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent;root=p.parents[2]
src=(root/'main/power/asic_init.c').read_text();mc3=(root/'components/asic/mc3.c').read_text()
def fn(s,name):
 start=s.rfind('\n',0,s.index(name+'('))+1;brace=s.index('{',start);depth=1;i=brace+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
init=fn(mc3,'MC3_init')
assert 'MC3_start_qualification_work' not in init and 'mc3_write_mining_config' not in init and 'mc3_start_spdlog' not in init
assert 'MC3_send_hash_frequency(MC3_STARTUP_FREQUENCY_MHZ)' in init
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define PROTO_WARM_VOLTAGE_MV 1160
#define PROTO_WARM_DEFER_THERMAL_READS 1
#define PROTO_WARM_CYCLE_POWER 1
#define UART_FREQ 115200
#define pdMS_TO_TICKS(x) (x)
typedef struct {bool ASIC_initalized;struct {bool hardware_fault;} SYSTEM_MODULE;struct {float actual_frequency;} POWER_MANAGEMENT_MODULE;} GlobalState;
typedef struct {GlobalState *global;atomic_bool tripped;atomic_bool zero_rail_expected;} proto_delay_guard_t;
typedef struct {float temperature_c[4];} proto_bcast_observation_t;
static GlobalState g;
static proto_delay_guard_t guard;
static int64_t now,last_lock,first_work_time,stop_at;
static unsigned steps,jobs,arms,hold_reset_calls,reset_calls,init_calls,temp_reads;
static int fail_frequency,invalid_temperature_frequency,chip_count=4;
static bool job_success=true;
static int supply_mv=2320,voltage_steps,enable_calls;
static bool fail_discharge,fail_enable;
static int asic_prepare_power(GlobalState *v){(void)v;assert(hold_reset_calls==1);supply_mv=0;return 0;}
static int VCORE_set_voltage(GlobalState *v,float volts){(void)v;assert(volts==0);supply_mv=0;return 0;}
static int16_t VCORE_get_regulator_voltage_mv_fresh(GlobalState *v){(void)v;return fail_discharge?2320:supply_mv;}
static int enable_proto_power(GlobalState *v,unsigned mv){(void)v;assert(mv==800 && supply_mv==0 && jobs==0);enable_calls++;if(fail_enable)return -1;supply_mv=1600;return 0;}
static int set_proto_core_voltage(GlobalState *v,unsigned mv){assert(v->POWER_MANAGEMENT_MODULE.actual_frequency==100 && jobs==0 && arms==0);assert(mv==810U+10U*(unsigned)voltage_steps);voltage_steps++;supply_mv=2*mv;return 0;}

static int64_t esp_timer_get_time(void){return now;}
static void vTaskDelay(int ms){now+=(int64_t)ms*1000;}
static int proto_bcast_command(void){return stop_at && now>=stop_at ? 'X' : 0;}
static bool proto_warm_report_only(GlobalState *v){(void)v;return true;}
static int asic_hold_reset_low(void){hold_reset_calls++;return 0;}
static void SERIAL_set_baud(int x){assert(x==115200 && hold_reset_calls==1);}
static void SERIAL_clear_buffer(void){}
static int asic_reset(void){assert(hold_reset_calls==1);reset_calls++;now+=200000;return 0;}
static void clear_asic_chain_error(void){}
static int MC3_init(GlobalState *v){assert(reset_calls==1 && jobs==0 && arms==0 && supply_mv==1600 && enable_calls==1);init_calls++;v->POWER_MANAGEMENT_MODULE.actual_frequency=100;return chip_count;}
static float MC3_send_hash_frequency(float f){
 assert(init_calls==1 && jobs==0 && arms==0);
 assert(f==125+25*steps);steps++;
 if(steps>1){int previous=(int)f-25;assert(now-last_lock>=(previous>500?1000000:500000));}
 last_lock=now;
 return f==fail_frequency?0:f;
}
static int MC3_read_temperatures(float *out,int n){assert(n==4);assert(steps==20 && jobs==0 && arms==0);temp_reads++;for(int i=0;i<n;i++)out[i]=55; if(g.POWER_MANAGEMENT_MODULE.actual_frequency==invalid_temperature_frequency)out[0]=-288;return 4;}
static void proto_warm_fan(const proto_bcast_observation_t *o,int64_t t){(void)o;(void)t;}
static bool MC3_start_qualification_work(GlobalState *v){assert(v->POWER_MANAGEMENT_MODULE.actual_frequency==600 && steps==20);assert(now-last_lock>=1500000);first_work_time=now;jobs++;return job_success;}
static bool MC3_start_active_spdlog_observation(unsigned ms){assert(ms==180000 && jobs==1 && job_success);arms++;return true;}
'''+fn(src,'proto_warm_deferred_wait')+'\n'+fn(src,'proto_warm_deferred_temperature')+'\n'+fn(src,'proto_warm_cycle_asic_power')+'\n'+fn(src,'proto_warm_reset_ramp_first_work')+r'''
static void setup(void){g=(GlobalState){0};guard=(proto_delay_guard_t){.global=&g};now=last_lock=first_work_time=stop_at=0;steps=jobs=arms=hold_reset_calls=reset_calls=init_calls=0;supply_mv=2320;voltage_steps=enable_calls=0;fail_discharge=fail_enable=false;temp_reads=0;fail_frequency=invalid_temperature_frequency=0;chip_count=4;job_success=true;}
int main(void){
 setup();assert(proto_warm_reset_ramp_first_work(&g,&guard));assert(steps==20 && jobs==1 && arms==1 && first_work_time>=13000000 && temp_reads==1 && voltage_steps==36 && supply_mv==2320);
 setup();fail_frequency=475;assert(!proto_warm_reset_ramp_first_work(&g,&guard));assert(jobs==0 && arms==0 && steps==15);
 setup();invalid_temperature_frequency=600;assert(!proto_warm_reset_ramp_first_work(&g,&guard));assert(jobs==0 && arms==0);
 setup();stop_at=12500000;assert(!proto_warm_reset_ramp_first_work(&g,&guard));assert(jobs==0 && arms==0); /* stop during final settling */
 setup();chip_count=3;assert(!proto_warm_reset_ramp_first_work(&g,&guard));assert(steps==0 && jobs==0);
 setup();g.ASIC_initalized=true;assert(!proto_warm_reset_ramp_first_work(&g,&guard));assert(reset_calls==0);
 setup();job_success=false;assert(!proto_warm_reset_ramp_first_work(&g,&guard));assert(jobs==1 && arms==0);
 setup();fail_discharge=true;assert(!proto_warm_reset_ramp_first_work(&g,&guard));assert(jobs==0 && arms==0 && enable_calls==0 && now>=2250000 && !atomic_load(&guard.zero_rail_expected));
 setup();fail_enable=true;assert(!proto_warm_reset_ramp_first_work(&g,&guard));assert(jobs==0 && arms==0 && steps==0 && !atomic_load(&guard.zero_rail_expected));
 puts("PASS: full power-cycle/discharge and 800..1160 ramp with no work; first thermal read only after final 600 MHz settling and before first work; explicit 125..600 MHz steps; no work/arm before final settling; one job and arm; PLL/sensor/manual-stop/chain/producer failures prevent first work.");
}
'''
with tempfile.TemporaryDirectory() as d:
 f=Path(d)/'test.c';f.write_text(code);out=Path(d)/'test'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-unused-parameter',str(f),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
