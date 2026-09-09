from pathlib import Path
import subprocess,tempfile
s=(Path(__file__).resolve().parents[3]/'components/asic/mc3.c').read_text()
body=s[s.index('static bool mc3_refresh_pool_work_locked'):s.index('static bool mc3_ramp_hash_frequency')]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ESP_LOGI(...) ((void)0)
#define MC3_CHIP_NUM_ALL 255
#define MC3_SPDLOG_TIMER_COUNT 48
typedef struct {unsigned version_mask;double pool_diff;} bm_job;
typedef struct {int valid_jobs_lock;unsigned char *valid_jobs;struct {bm_job *active_jobs[128];} ASIC_TASK_MODULE;} GlobalState;
static unsigned mc3_job_id=7,mc3_chip_count=4;static bool mc3_spdlog_started=true;static unsigned stage,fail_stage;
static void pthread_mutex_lock(int *p){(void)p;}
static void pthread_mutex_unlock(int *p){(void)p;}
static uint32_t mc3_current_rolltime(GlobalState *g){(void)g;return 123;}
static bool next(unsigned s){assert(stage==s);stage++;return fail_stage!=stage;}
static bool mc3_write_mining_config_impl(unsigned r,unsigned mask,bool pll){assert(r==123 && mask==0x1fffe000 && !pll);return next(0);}
static bool mc3_write_work_target(double d,unsigned count){assert(d==1000 && count==4);return next(1);}
static bool mc3_write_version_bases(bm_job *j,unsigned c){assert(j->version_mask==0x1fffe000 && c==4);return next(2);}
static bool mc3_write_v_work(bm_job *j,unsigned id){assert(j->pool_diff==1000 && id==7);return next(3);}
static bool mc3_start_spdlog_window(unsigned first,unsigned all,unsigned timer){assert(first==0 && all==255 && timer==48);return next(4);}
'''+body+r'''
int main(void){
 unsigned char valid[128]={0};bm_job job={.version_mask=0x1fffe000,.pool_diff=1000};GlobalState g={.valid_jobs=valid};
 g.ASIC_TASK_MODULE.active_jobs[7]=&job;valid[7]=1;
 assert(mc3_refresh_pool_work_locked(&g));assert(stage==5 && g.ASIC_TASK_MODULE.active_jobs[7]==&job && valid[7]==1);
 for(unsigned i=1;i<=5;i++){stage=0;fail_stage=i;assert(!mc3_refresh_pool_work_locked(&g));assert(stage==i);}
 stage=0;valid[7]=0;assert(mc3_refresh_pool_work_locked(&g));assert(!stage && !mc3_spdlog_started);
 g.valid_jobs=NULL;assert(mc3_refresh_pool_work_locked(&g));
 puts("PASS: active job retained, rolltime refreshed, no PLL writes, all work-write failures propagated, no-job case deferred");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
