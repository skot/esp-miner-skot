"""Compile the real launch/config/audit code against a recording register transport."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[3]
s=(root/'components/asic/mc3.c').read_text()
def function(name):
 m=re.search(r'^(?:static )?(?:bool|void) '+name+r'\([^;]*?\)\n\{',s,re.M);assert m,name
 level=1;i=m.end()
 while level:
  level+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i]
macros='\n'.join(l for l in s.splitlines() if l.startswith('#define MC3_') and not l.endswith('\\'))
code=r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
'''+macros+r'''
typedef struct {uint32_t version,version_mask,ntime,target;double pool_diff;} bm_job;
typedef struct {uint32_t pll_cfg[4],pll_en[4];unsigned writes,pll_writes,logger_writes,write_failures;} mc3_passive_launch_state_t;
static pthread_mutex_t mc3_work_lock=PTHREAD_MUTEX_INITIALIZER;

static unsigned mc3_chip_count=4,mc3_active_frequency_count=4,mc3_active_uniform_pll_id;
static bool mc3_active_frequency_profile_mixed;
static float mc3_active_chip_frequency_mhz[]={600,600,600,600};
static struct {uint32_t reg,value;} recorded[100];
static unsigned n,fail_at=9999;
'''+r'''
static bool mc3_write_register(uint8_t c,uint32_t r,uint32_t v,uint8_t count){(void)c;(void)count;recorded[n].reg=r;recorded[n++].value=v;if(n==fail_at){return false;}return true;}

static bool mc3_version_start_bit(uint32_t mask,uint8_t *start){*start=13;return mask!=0;}
static uint32_t mc3_v_ctrl(uint8_t bit){return bit;}
static uint32_t mc3_global_spd_for_pll(unsigned p){return MC3_DEFAULT_GLOBAL_SPD_VALUE+p;}
static bool mc3_write_active_rolltimes(void){return true;}
static uint32_t mc3_pll_en_register(unsigned p){return MC3_PLL0_EN+p*8;}
static uint32_t mc3_rolltime_for_frequency(float f){return (uint32_t)f;}
static uint32_t mc3_spdlog_timer_value(unsigned c){return c|0x10000;}
static bool mc3_write_work_target(double d,unsigned c){(void)c;return mc3_write_register(0,MC3_WORK_TARGET_L,(unsigned)d,8);}
static bool mc3_write_version_bases(bm_job *j,unsigned c){(void)c;return mc3_write_register(0,MC3_V_BASE,j->version,8);}
static bm_job jobs[2];static unsigned job_count;
static bool mc3_write_v_work(bm_job *j,unsigned id){assert(id==0);jobs[job_count++]=*j;return mc3_write_register(0,MC3_V_WORK_NOTE+12,id,8);}
'''+function('mc3_write_mining_config_impl')+function('mc3_write_mining_config')+function('MC3_start_qualification_observation')+r'''
int main(void){
 assert(MC3_start_qualification_observation());unsigned first_n=n;
 assert(recorded[first_n-1].reg==MC3_V_WORK_NOTE+12);
 unsigned arms=0;
 for(unsigned i=0;i<first_n;i++){
  assert(recorded[i].reg<MC3_PLL0_CFG || recorded[i].reg>=MC3_PLL0_CFG+32);
  if(recorded[i].reg==MC3_SPDLOG_RST || recorded[i].reg==MC3_SPDLOG_TIMER)arms++;
 }
 assert(arms==2 && recorded[6].reg==MC3_SPDLOG_RST && recorded[7].reg==MC3_SPDLOG_TIMER);
 assert(recorded[5].reg==MC3_WORK_CFG);
 assert(!(recorded[5].value & (MC3_WORK_CFG_RST_FIFO|MC3_WORK_CFG_SPD_GO)));
 // Every register-write failure in launch prevents success, including payload.
 for(unsigned k=1;k<=first_n;k++){n=0;job_count=0;fail_at=k;assert(!MC3_start_qualification_observation());assert(n==k);}
 fail_at=9999;mc3_active_chip_frequency_mhz[0]=625;n=0;assert(!MC3_start_qualification_observation());assert(n==0);
 mc3_active_chip_frequency_mhz[0]=600;mc3_chip_count=3;assert(!MC3_start_qualification_observation());assert(n==0);
 mc3_chip_count=4;mc3_active_frequency_profile_mixed=true;assert(!MC3_start_qualification_observation());assert(n==0);
 assert(mc3_write_mining_config(600,MC3_QUALIFICATION_VERSION_MASK));assert(recorded[n-1].reg==MC3_PLL0_EN);
 puts("PASS: one pre-job counter arm, no PLL writes, no post-job strobe, all write failures propagate, unsupported profiles rejected, normal mining config retained");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
