from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent
s=(p.parents[2]/'components/asic/mc3.c').read_text()
def fn(name):
 start=s.index('\n',s.index(name+'(')) if False else s.rfind('\n',0,s.index(name+'('))+1
 b=s.index('{',start);i=b+1;depth=1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <inttypes.h>
#define MC3_THERMAL_RESOLUTION_BITS 12
#define MC3_MAX_TRACKED_CHIPS 256
#define MC3_THERMAL_DOUT 1
#define MC3_THERMAL_DONE 2
#define MC3_THERMAL_STATUS 3
#define MC3_THERMAL_COMMAND 4
#define MC3_CHIP_NUM_ALL 8
#define TAG "test"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
static int mc3_thermal_sensor_lock,locked;
static void pthread_mutex_lock(int *p){(void)p;assert(!locked);locked=1;}
static void pthread_mutex_unlock(int *p){(void)p;assert(locked);locked=0;}
static int64_t now;
static int64_t esp_timer_get_time(void){return now;}
static void vTaskDelay(int n){now+=n*1000;}
static uint8_t mc3_chip_count=4;
static unsigned attempt,done_polls[4];
static bool pending[4];
static int scenario;
static bool mc3_write_register(uint8_t chip,uint32_t reg,uint32_t value,uint8_t all){
 assert(locked && chip==0 && reg==4 && value==0x88000105 && all==8);
 attempt++;for(int i=0;i<4;i++){pending[i]=true;done_polls[i]=0;}
 return scenario!=7 || attempt>1;
}
static bool mc3_read_register(uint8_t chip,uint32_t reg,uint32_t *value){
 assert(locked && chip<4);now+=2000;
 if(reg==MC3_THERMAL_DOUT){
  bool result=pending[chip];pending[chip]=false;
  *value=(result && chip==2 && (scenario==1 || (scenario==2 && attempt==1) || scenario==5))?0:2100;
  if(result && chip==0 && scenario==5)*value=2300; /* ~90 C plus invalid A3 */
  if(result && chip==2 && scenario==6 && attempt==1)return false;
  return true;
 }
 if(reg==MC3_THERMAL_DONE){
  *value=pending[chip]?1:0;
  if(pending[chip] && chip==2 && scenario==3 && done_polls[chip]++<3)*value=0;
  if(pending[chip] && chip==2 && scenario==8)*value=0;
  if(!pending[chip] && chip==2 && scenario==4 && attempt==0)*value=1; /* stale DONE */
  return true;
 }
 assert(reg==MC3_THERMAL_STATUS);*value=0;return true;
}
'''+fn('mc3_temperature_from_dout')+'\n'+fn('MC3_read_temperatures')+r'''
static void setup(int n){scenario=n;attempt=0;now=0;for(int i=0;i<4;i++)pending[i]=false;}
int main(void){float t[4];
 setup(0);assert(MC3_read_temperatures(t,4)==4 && attempt==1);for(int i=0;i<4;i++)assert(t[i]>50 && t[i]<60);assert(!locked);
 setup(2);MC3_read_temperatures(t,4);assert(attempt==2 && t[2]>50);
 setup(1);MC3_read_temperatures(t,4);assert(attempt==3 && t[2]==-1 && now<2000000);
 setup(3);MC3_read_temperatures(t,4);assert(attempt==1 && t[2]>50 && done_polls[2]==4);
 setup(4);MC3_read_temperatures(t,4);assert(attempt==2 && t[2]>50);
 setup(5);MC3_read_temperatures(t,4);assert(attempt==1 && t[0]>=70 && t[2]==-1);
 setup(6);MC3_read_temperatures(t,4);assert(attempt==2 && t[2]>50);
 setup(7);MC3_read_temperatures(t,4);assert(attempt==2 && t[2]>50);
 setup(8);MC3_read_temperatures(t,4);assert(attempt<=3 && t[2]==-1 && now<2100000);
 setup(0);assert(MC3_read_temperatures(NULL,4)==0 && !locked && attempt==0);
 puts("PASS: zero/read/trigger/stale-DONE retries; A3 completion waited independently; persistent failure bounded; hot result never retried away; no cached fallback.");
}
'''
with tempfile.TemporaryDirectory() as d:
 src=Path(d)/'test.c';src.write_text(code);out=Path(d)/'test'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-unused-variable',str(src),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
