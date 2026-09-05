"""Run the actual job producer loop through pause/resume with a held pool job."""
from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[3];s=(root/'main/tasks/create_jobs_task.c').read_text();a=s.index('void create_jobs_task(');b=s.index('\nstatic void generate_work(',a)
code=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <setjmp.h>
#define MC3 3
#define STRATUM_PROTOCOL_V1 1
#define STRATUM_PROTOCOL_V2 2
#define MALLOC_CAP_SPIRAM 1
#define portTICK_PERIOD_MS 1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef int stratum_protocol_t;
typedef struct {} bm_job;
typedef struct {char *job_id;bool clean_jobs;} mining_notify;
typedef struct {unsigned job_id;bool clean_jobs;} sv2_job_t;
typedef sv2_job_t sv2_ext_job_t;
typedef struct {struct {bm_job **active_jobs;} ASIC_TASK_MODULE;uint8_t *valid_jobs;double pool_difficulty;stratum_protocol_t stratum_protocol;int stratum_queue;bool ASIC_initalized,new_set_mining_difficulty_msg,new_stratum_version_rolling_msg;uint32_t version_mask;struct {struct {struct {int id;} asic;} family;} DEVICE_CONFIG;} GlobalState;
static GlobalState g;static jmp_buf done;static unsigned step,sends;
static uint64_t now;
static void *heap_caps_malloc(size_t n,int cap){(void)cap;return malloc(n);}
static int ASIC_get_asic_job_frequency_ms(GlobalState *v){(void)v;return 1000;}
static bool stratum_v2_is_extended_channel(GlobalState *v){(void)v;return false;}
static void free_work_item(GlobalState *v,void *work,stratum_protocol_t proto){(void)v;(void)work;(void)proto;}
static uint64_t esp_timer_get_time(void){return now++;}
static void vTaskDelay(int ms){now+=ms*1000;}
static void ASIC_set_version_mask(GlobalState *v,uint32_t mask){(void)v;(void)mask;}
static void *queue_dequeue_timeout(int *q,int ms){
 (void)q;(void)ms;static mining_notify n={"job",true};static sv2_job_t j={42,true};
 if(step==10)longjmp(done,1);
 const bool ready[]={false,false,true,true,false,true,true,false,false,true};
 g.ASIC_initalized=ready[step];bool new_job=step==0 || step==4;step++;
 return new_job?(g.stratum_protocol==STRATUM_PROTOCOL_V1?(void*)&n:(void*)&j):NULL;
}
static void record_send(void){assert(g.ASIC_initalized);assert(step==3 || step==6 || step==10);sends++;}
static void generate_work(GlobalState *v,mining_notify *n,uint64_t e,double d){(void)v;(void)n;(void)e;(void)d;record_send();}
static void generate_work_sv2(GlobalState *v,sv2_job_t *n,double d){(void)v;(void)n;(void)d;record_send();}
static void generate_work_sv2_ext(GlobalState *v,sv2_ext_job_t *n,double d,uint64_t e){(void)v;(void)n;(void)d;(void)e;record_send();}
'''+s[a:b]+r'''
int main(void){
 for(int protocol=1;protocol<=2;protocol++){
  g=(GlobalState){.stratum_protocol=protocol,.pool_difficulty=1000,.DEVICE_CONFIG={.family={.asic={MC3}}}};step=sends=0;now=0;
  if(!setjmp(done))create_jobs_task(&g);
  assert(sends==3);
  free(g.ASIC_TASK_MODULE.active_jobs);free(g.valid_jobs);
 }
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(code);subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('PASS: actual producer loop sends nothing during pause, sends the latest job on resume, resends held work after reset, and avoids duplicate resends while running (SV1 MC3 and SV2 standard).')
