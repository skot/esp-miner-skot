from pathlib import Path
import tempfile,subprocess
p=Path(__file__).resolve().parent;s=(p.parents[2]/'main/power/asic_init.c').read_text()
a=s.index('static bool proto_warm_deferred_work(');b=s.index('\nstatic void run_proto_warm_ramp_probe',a)
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <inttypes.h>
#define ESP_LOGW(tag,...) do { if (0) printf(__VA_ARGS__); } while (0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
static bool no_scope=true,reset_ok=true,snapshot_ok=true,ack;
#define PROTO_WARM_NO_SCOPE no_scope
typedef struct {int unused;} GlobalState;
typedef struct {atomic_bool tripped;} proto_delay_guard_t;
typedef struct {int unused;} proto_bcast_observation_t;
static int64_t now,stop_at;static int commands,resets,samples,rates;
static int64_t esp_timer_get_time(void){return now;}
static void vTaskDelay(int ms){now+=(int64_t)ms*1000;}
static int proto_bcast_command(void){commands++;if(stop_at && now>=stop_at)return 'X';if(!no_scope){if(commands==2)return 'G';if(commands==3)return 'T';if(ack && commands==4)return 'R';}return 0;}
static bool proto_warm_snapshot(GlobalState *g,proto_delay_guard_t *d,const char *p,proto_bcast_observation_t *o,int64_t start){(void)g;(void)d;(void)p;(void)o;(void)start;samples++;now+=1000000;return snapshot_ok;}
static bool proto_warm_reset_ramp_first_work(GlobalState *g,proto_delay_guard_t *d){(void)g;resets++;now+=20000000;return reset_ok && !atomic_load(&d->tripped);}
static bool proto_warm_rates(GlobalState *g,proto_bcast_observation_t *f,proto_bcast_observation_t *l){(void)g;(void)f;(void)l;rates++;return true;}
'''+s[a:b]+r'''
static void setup(void){now=stop_at=0;commands=resets=samples=rates=0;no_scope=reset_ok=snapshot_ok=true;ack=false;}
int main(void){GlobalState g={0};proto_delay_guard_t d={0};proto_bcast_observation_t o={0};
setup();assert(proto_warm_deferred_work(&g,&d,&o,0));assert(now>=140000000 && resets==1 && samples>=90 && rates==2);
setup();stop_at=30000000;assert(!proto_warm_deferred_work(&g,&d,&o,0));assert(now<40000000 && rates==0);
setup();snapshot_ok=false;assert(!proto_warm_deferred_work(&g,&d,&o,0));assert(rates==0);
setup();reset_ok=false;assert(!proto_warm_deferred_work(&g,&d,&o,0));assert(samples==0);
setup();no_scope=false;assert(!proto_warm_deferred_work(&g,&d,&o,0));assert(now>=80000000 && now<90000000 && rates==0);
setup();no_scope=false;ack=true;assert(proto_warm_deferred_work(&g,&d,&o,0));assert(now>=140000000 && rates==2);
puts("PASS: no-scope completes 120s without handshake, retains stop/failure handling; scope mode still requires real acknowledgement.");}
'''
with tempfile.TemporaryDirectory() as d:
 f=Path(d)/'test.c';f.write_text(code);exe=Path(d)/'test'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(f),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
