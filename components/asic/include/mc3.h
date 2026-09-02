#ifndef MC3_H_
#define MC3_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "asic_common.h"
#include "asic_tuning.h"
#include "mining.h"

#define MC3_SERIALTX_DEBUG false
#define MC3_SERIALRX_DEBUG false
#define MC3_DEBUG_WORK false
#define MC3_STARTUP_FREQUENCY_MHZ 100
#define MC3_QUALIFICATION_MAX_CHIPS ASIC_TUNING_MAX_CHIPS

typedef struct {
    float frequency_mhz;
    uint8_t chip_count;
    uint32_t passed[MC3_QUALIFICATION_MAX_CHIPS];
    uint32_t failed[MC3_QUALIFICATION_MAX_CHIPS];
    float hashrate_ghs[MC3_QUALIFICATION_MAX_CHIPS];
    float total_hashrate_ghs;
} mc3_qualification_result_t;

typedef void (*mc3_frequency_step_callback_t)(void *global_state, float actual_frequency);

uint8_t MC3_init(void * GLOBAL_STATE);
void MC3_send_work(void * GLOBAL_STATE, bm_job * next_bm_job);
void MC3_set_version_mask(uint32_t version_mask);
int MC3_set_max_baud(void);
int MC3_set_default_baud(void);
float MC3_send_hash_frequency(float frequency);
bool MC3_ramp_hash_frequency(void * GLOBAL_STATE);
bool MC3_start_qualification_work(void * GLOBAL_STATE);
bool MC3_qualify_frequency(void * GLOBAL_STATE, float frequency,
    mc3_qualification_result_t *result);
void MC3_set_frequency_step_callback(mc3_frequency_step_callback_t callback);
task_result * MC3_process_work(void * GLOBAL_STATE);
void MC3_read_registers(void * GLOBAL_STATE);
void MC3_set_nonce_space(double nonce_percent, float frequency, uint16_t asic_count, uint16_t cores);
uint8_t MC3_read_temperatures(float *temps, size_t max_temps);
uint8_t MC3_read_vdd_voltages(float *voltages_mv, size_t max_voltages);
bool MC3_start_core_scan(void);
void MC3_get_core_scan_status(asic_tuning_status_t *status);
bool MC3_get_core_scan_chip_result(uint8_t chip_id, asic_tuning_chip_result_t *result);

#endif /* MC3_H_ */
