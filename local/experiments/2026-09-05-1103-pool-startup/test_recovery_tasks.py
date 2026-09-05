"""Compile the real app_main tail and verify tasks survive a paused Proto boot."""
from pathlib import Path
import subprocess,tempfile

root=Path(__file__).resolve().parents[3]
source=(root/'main/main.c').read_text()
tail=source.split('    queue_init(&GLOBAL_STATE.stratum_queue);',1)[1]
harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define PROTO 1
#define ASIC_INIT_COLD_BOOT 0
#define pdPASS 1
#define MALLOC_CAP_SPIRAM 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
enum {create_jobs_task, ASIC_result_task, hashrate_monitor_task,
      statistics_task, protocol_coordinator_task, self_test_task};
static struct {
 struct { struct { int id; } family; } DEVICE_CONFIG;
 struct { bool is_active; int system_init_ret; } SELF_TEST_MODULE;
 struct { const char *asic_status; } SYSTEM_MODULE;
} GLOBAL_STATE;
static int seen[6],init_result,shown,coordinators;
static int asic_initialize(void *g,int mode,int delay) {return init_result;}
static void self_test_show_message(void *g,const char *msg) {shown++;}
static void protocol_coordinator_init(void *g) {coordinators++;}
static int xTaskCreate(int fn,const char *name,int size,void *g,int priority,void *handle) {
 seen[fn]++;return pdPASS;
}
static int xTaskCreateWithCaps(int fn,const char *name,int size,void *g,int priority,void *handle,int caps) {
 return xTaskCreate(fn,name,size,g,priority,handle);
}
static void boot_tail(int system_init_ret) {
'''+tail+r'''
static void reset(int family,bool self_test,int result) {
 memset(&GLOBAL_STATE,0,sizeof(GLOBAL_STATE));memset(seen,0,sizeof(seen));
 GLOBAL_STATE.DEVICE_CONFIG.family.id=family;
 GLOBAL_STATE.SELF_TEST_MODULE.is_active=self_test;
 init_result=result;shown=0;coordinators=0;
}
int main(void) {
 // Both successful and interrupted Proto boots must create the workers and
 // protocol coordinator exactly once; recovery reuses their initialized state.
 for(int ready=0;ready<=1;ready++) {
  reset(PROTO,false,ready?4:0);boot_tail(ESP_OK);
  for(int task=0;task<5;task++)assert(seen[task]==1);
  assert(!seen[self_test_task] && coordinators==1 && !shown);
 }
 reset(2,false,0);boot_tail(ESP_OK);
 for(int task=0;task<6;task++)assert(!seen[task]);
 assert(!coordinators);
 reset(PROTO,true,0);boot_tail(ESP_OK);
 for(int task=0;task<4;task++)assert(!seen[task]);
 assert(seen[protocol_coordinator_task]==1 && seen[self_test_task]==1);
 assert(shown==1 && GLOBAL_STATE.SELF_TEST_MODULE.system_init_ret==ESP_FAIL);
 reset(PROTO,true,4);boot_tail(ESP_OK);
 for(int task=0;task<6;task++)assert(seen[task]==1);
 reset(PROTO,false,4);boot_tail(ESP_FAIL);
 for(int task=0;task<4;task++)assert(!seen[task]);
 assert(seen[protocol_coordinator_task]==1);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
 c=Path(tmp)/'recovery.c';binary=Path(tmp)/'recovery'
 c.write_text(harness)
 subprocess.run(['cc','-std=c11',str(c),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('PASS: real app_main tail keeps Proto workers alive after interrupted boot; other boot/self-test paths retained')
