from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent
code=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef struct {struct {float actual_frequency;} POWER_MANAGEMENT_MODULE;struct {bool hardware_fault;} SYSTEM_MODULE;} GlobalState;
typedef struct {atomic_bool tripped;} proto_delay_guard_t;
typedef struct {float temperature_c[4];} proto_bcast_observation_t;
static int64_t now;static int reads,fan_restored,stop_command;static float temp;static bool frozen,invalid;
static int64_t esp_timer_get_time(void){return now;}
static void FAN_CONTROLLER_set_bench_percent(int p){assert(p==100);}
static int proto_bcast_command(void){return stop_command;}
static int MC3_read_temperatures(float *t,int n){assert(n==4);reads++;t[0]=invalid?70:temp;t[1]=temp-.8;t[2]=temp-.4;t[3]=temp-.3;if(!frozen)temp-=.25;return 4;}
static void proto_warm_fan(proto_bcast_observation_t *t,int64_t elapsed){assert(elapsed==0 && t->temperature_c[0]<=42.5 && t->temperature_c[1]>=39.5);fan_restored++;}
static bool proto_warm_deferred_wait(proto_delay_guard_t *g,int ms){assert(ms==1000);now+=1000000;return !atomic_load(&g->tripped);}
'''+(p/'cool_after_hot.inc').read_text()+r'''
int main(void){
 GlobalState g={.POWER_MANAGEMENT_MODULE={.actual_frequency=300}};proto_delay_guard_t guard={0};temp=55;
 assert(proto_compare_cool_after_hot(&g,&guard));assert(fan_restored==1 && reads==51 && now==50000000 && g.POWER_MANAGEMENT_MODULE.actual_frequency==300);
 now=0;reads=0;temp=55;frozen=true;assert(!proto_compare_cool_after_hot(&g,&guard));assert(now==180000000 && reads==180);
 now=0;invalid=true;assert(!proto_compare_cool_after_hot(&g,&guard));assert(now==0);
 invalid=false;stop_command='X';assert(!proto_compare_cool_after_hot(&g,&guard));
 stop_command=0;g.POWER_MANAGEMENT_MODULE.actual_frequency=600;assert(!proto_compare_cool_after_hot(&g,&guard));
 puts("PASS: cool at unchanged 300 MHz; bounded timeout; invalid temperature/manual stop rejected; fan policy restored only in matched temperature band");
}
'''
with tempfile.TemporaryDirectory() as d:
 f=Path(d)/'test.c';f.write_text(code);exe=Path(d)/'test'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(f),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
