from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent
src=(p.parents[2]/'main/power/asic_init.c').read_text()
def fn(name):
 start=src.rfind('static ',0,src.index(name+'('));brace=src.index('{',start);depth=1;i=brace+1
 while depth:
  depth+=(src[i]=='{')-(src[i]=='}');i+=1
 return src[start:i]
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
typedef struct {uint32_t domain_pass[4];unsigned domain_zero_cores[4];} summary_t;
typedef struct {summary_t previous[4];int64_t previous_us[4];} proto_bcast_observation_t;
typedef struct {struct {float actual_frequency;} POWER_MANAGEMENT_MODULE;} GlobalState;
typedef struct {int unused;} proto_delay_guard_t;
static int64_t now=1000000;
static bool abort_read=false, recover=false;
static unsigned snapshots=0,rates_calls=0;
static int64_t esp_timer_get_time(void) {return now;}
static int proto_bcast_command(void) {return 0;}
static void vTaskDelay(int ms) {now+=(int64_t)ms*1000;}
static bool proto_warm_snapshot(GlobalState *g,proto_delay_guard_t *d,const char *p,proto_bcast_observation_t *o,int64_t w) {
 (void)g;(void)d;(void)p;(void)w;snapshots++;now+=500000;
 if(abort_read)return false;
 if(recover && now>65000000)for(int a=0;a<4;a++)for(int b=0;b<4;b++)o->previous[a].domain_pass[b]++;
 return true;
}
static bool proto_warm_rates(GlobalState *g,const proto_bcast_observation_t *f,const proto_bcast_observation_t *l) {
 (void)g;(void)f;(void)l;rates_calls++;return recover;
}
'''+fn('proto_warm_zero_pass_mask')+'\n'+fn('proto_warm_latch_zero')+'\n'+fn('proto_warm_hold_zero')+r'''
int main(void) {
 proto_bcast_observation_t o={0};
 o.previous[0].domain_zero_cores[1]=39;
 assert(proto_warm_zero_pass_mask(&o)==0); /* no sample */
 o.previous_us[0]=1;
 assert(proto_warm_zero_pass_mask(&o)==2);
 o.previous[0].domain_zero_cores[1]=38;assert(proto_warm_zero_pass_mask(&o)==0);
 o.previous[0].domain_zero_cores[1]=39;o.previous[0].domain_pass[1]=1;assert(proto_warm_zero_pass_mask(&o)==0);
 o.previous[0].domain_pass[1]=0;o.previous_us[1]=1;o.previous[1].domain_zero_cores[2]=39;
 assert(proto_warm_zero_pass_mask(&o)==0x42);
 int64_t first=0;uint16_t mask=0;proto_warm_latch_zero(&o,475,&first,&mask);
 assert(first==1000000 && mask==0x42);
 now+=1000000;o.previous[0].domain_zero_cores[2]=39;proto_warm_latch_zero(&o,475,&first,&mask);
 assert(first==1000000 && mask==0x42); /* original time/mask preserved */
 GlobalState g={0};proto_delay_guard_t guard={0};
 assert(proto_warm_hold_zero(&g,&guard,"warm475",&o,0,first,mask));
 assert(now>=first+120000000 && now<first+121000000 && snapshots>100 && rates_calls==2);
 now=first;rates_calls=0;recover=true;
 assert(proto_warm_hold_zero(&g,&guard,"warm475",&o,0,first,mask));
 assert(now>=first+120000000 && rates_calls==2); /* full hold even after recovery */
 now=first;rates_calls=0;abort_read=true;
 assert(!proto_warm_hold_zero(&g,&guard,"warm475",&o,0,first,mask));
 assert(rates_calls==0 && now<first+1000000);
 puts("PASS: zero-domain detection, first-event latch, full 120-second hold with/without recovery, sensor failure exits; no PLL/work/rearm dependency in hold");
}
'''
with tempfile.TemporaryDirectory() as d:
 f=Path(d)/'test.c';f.write_text(code);out=Path(d)/'test'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-unused-parameter','-Wno-unused-variable',str(f),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
