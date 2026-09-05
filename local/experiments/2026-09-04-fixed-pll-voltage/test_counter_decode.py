"""Compile the production counter reader with a synthetic MC3 register transport."""
from pathlib import Path
import re, subprocess, tempfile
repo=Path(__file__).resolve().parents[3]
s=(repo/'components/asic/mc3.c').read_text()
h=(repo/'components/asic/include/mc3.h').read_text()
reader=s[s.index('static bool mc3_read_core_spdlog_bank('):s.index('static uint64_t mc3_sum_core_pass_counts(')]
struct=next(x for x in re.findall(r'typedef struct \{.*?\} mc3_\w+;',h,re.S) if '} mc3_core_domain_summary_t;' in x)
constants='\n'.join(re.findall(r'^#define MC3_CORE_(?:SPDLOG_\w+|PASS_READ_CHUNK) .*$',s,re.M))
code='''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#define ASIC_TUNING_MAX_CORES 156
#define MC3_INTERNAL_VOLTAGE_DOMAIN_COUNT 4
#define MC3_SLICE_COUNT 12
#define MC3_CORES_PER_SLICE 13
#define ESP_LOGW(...) ((void)0)
static uint8_t mc3_chip_count=4;
static pthread_mutex_t mc3_work_lock=PTHREAD_MUTEX_INITIALIZER;
static bool fail_once=false, fail_always=false;
static bool mc3_read_register_block(uint8_t chip, uint32_t reg, uint32_t *out, uint8_t n) {
    if (fail_always) return false;
    if (fail_once) {fail_once=false; return false;}
    unsigned core=reg>=0x12400 ? 78+(reg-0x12400)/4 : (reg-0x12000)/4;
    assert(chip<4 && core+n<=156);
    for (unsigned i=0;i<n;i++) out[i]=((65535u-core-i)<<16)|(core+i+1);
    return true;
}
'''+constants+'\n'+struct+'\n'+reader+'''
int main(void) {
    mc3_core_domain_summary_t result;
    fail_once=true;
    assert(MC3_read_core_domain_summary(0,&result));
    assert(result.core_pass_sum==156u*157u/2u);
    for (unsigned d=0;d<4;d++) {
        unsigned first=d*39;
        assert(result.domain_pass[d]==39*(2*first+40)/2);
        assert(result.domain_fail[d]==39*65535-39*(2*first+38)/2);
        assert(result.domain_zero_cores[d]==0);
    }
    uint32_t tuning[156];
    assert(mc3_read_core_pass_counts(0,tuning));
    for (unsigned i=0;i<156;i++) assert(tuning[i]==i+1);
    fail_always=true;
    assert(!MC3_read_core_domain_summary(0,&result));
    assert(!MC3_read_core_domain_summary(4,&result));
    assert(!MC3_read_core_domain_summary(0,NULL));
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);(p/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test'),'-pthread'],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: separate low/high counters, bank/domain boundaries, retry, invalid input, read failure, existing tuning path')
