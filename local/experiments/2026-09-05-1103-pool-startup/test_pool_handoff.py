from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]
s=(root/'components/asic/mc3.c').read_text();a=s.index('void MC3_prepare_pool_work(');b=s.index('\nbool MC3_start_qualification_work',a)
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int pthread_mutex_t;
static int locks;
static void pthread_mutex_lock(pthread_mutex_t *m){(void)m;locks++;}
static void pthread_mutex_unlock(pthread_mutex_t *m){(void)m;locks--;}
typedef struct {uint8_t *valid_jobs;pthread_mutex_t valid_jobs_lock;} GlobalState;
static pthread_mutex_t mc3_work_lock,mc3_pending_lock;
static bool mc3_spdlog_started=true,mc3_diagnostic_mining=false;
static unsigned mc3_pending_head=4,mc3_pending_count=10;
static uint32_t mc3_seen_nonce_words[4]={1,2,3,4};
'''+s[a:b]+r'''
int main(void){
 uint8_t valid[128];memset(valid,1,sizeof(valid));GlobalState g={.valid_jobs=valid};
 MC3_prepare_pool_work(&g);
 assert(mc3_diagnostic_mining);
 assert(!mc3_spdlog_started && !mc3_pending_head && !mc3_pending_count && !locks);
 for(int i=0;i<128;i++)assert(!valid[i]);for(int i=0;i<4;i++)assert(!mc3_seen_nonce_words[i]);
 g.valid_jobs=NULL;MC3_prepare_pool_work(&g);assert(!locks);
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(code);subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
# Explicit producer gate precedes every protocol's generation; reset clears
# the sent marker even when the held pool job has not changed.
s=(root/'main/tasks/create_jobs_task.c').read_text();a=s.index('    while (1) {');b=s.index('\nstatic void generate_work(',a);loop=s[a:b]
assert loop.index('if (!GLOBAL_STATE->ASIC_initalized) current_work_sent = false;') < loop.index('queue_dequeue_timeout')
assert 'if (current_work_sent &&' in loop
assert loop.index('if (!GLOBAL_STATE->ASIC_initalized) {') < loop.index('// Generate and send job') < loop.index('current_work_sent = true;')
s=(root/'main/power/asic_init.c').read_text();assert s.index('MC3_prepare_pool_work(GLOBAL_STATE);') < s.index('GLOBAL_STATE->ASIC_initalized = true;')
print('PASS: qualification state is cleared without ASIC writes; producer pause/resume and final handoff gates are present.')
