#ifndef AUD1123_H_
#define AUD1123_H_

#include "common.h"
#include "driver/gpio.h"
#include "mining.h"

#define ASIC_AUD1123_JOB_FREQUENCY_MS 500

#define AUD1123_ASIC_DIFFICULTY 256

#define AUD1123_SERIALTX_DEBUG true
#define AUD1123_SERIALRX_DEBUG false
#define AUD1123_DEBUG_WORK false //causes insane amount of debug output
#define AUD1123_DEBUG_JOBS false //causes insane amount of debug output

static const uint64_t AUD1123_CORE_COUNT = 128;
static const uint64_t AUD1123_SMALL_CORE_COUNT = 2040;

typedef struct
{
    float frequency;
} AUD1123Module;

typedef struct __attribute__((__packed__))
{
    uint8_t job_id;
    uint8_t num_midstates;
    uint8_t starting_nonce[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
    uint8_t merkle_root[32];
    uint8_t prev_block_hash[32];
    uint8_t version[4];
} AUD1123_job;

uint8_t AUD1123_init(uint64_t frequency, uint16_t asic_count);
void AUD1123_send_work(void * GLOBAL_STATE, bm_job * next_bm_job);
void AUD1123_set_job_difficulty_mask(int);
void AUD1123_set_version_mask(uint32_t version_mask);
int AUD1123_set_max_baud(void);
int AUD1123_set_default_baud(void);
void AUD1123_send_hash_frequency(float frequency);
bool AUD1123_set_frequency(float target_freq);
task_result * AUD1123_process_work(void * GLOBAL_STATE);

#endif /* AUD1123_H_ */
