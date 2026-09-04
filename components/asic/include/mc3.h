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
#define MC3_INTERNAL_VOLTAGE_DOMAIN_COUNT 4
#define MC3_PVT_VOLTAGE_CHANNEL_COUNT 16
#define MC3_SLICE_COUNT 12
#define MC3_CORES_PER_SLICE 13

typedef struct {
    float frequency_mhz;
    uint8_t chip_count;
    float chip_frequency_mhz[MC3_QUALIFICATION_MAX_CHIPS];
    uint32_t passed[MC3_QUALIFICATION_MAX_CHIPS];
    uint32_t failed[MC3_QUALIFICATION_MAX_CHIPS];
    float hashrate_ghs[MC3_QUALIFICATION_MAX_CHIPS];
    float total_hashrate_ghs;
} mc3_qualification_result_t;

typedef struct {
    uint8_t chip_id;
    uint64_t core_pass_sum;
    uint32_t domain_pass[MC3_INTERNAL_VOLTAGE_DOMAIN_COUNT];
    uint8_t domain_zero_cores[MC3_INTERNAL_VOLTAGE_DOMAIN_COUNT];
    uint32_t slice_pass[MC3_SLICE_COUNT];
    uint16_t minimum_core_id;
    uint32_t minimum_core_pass;
} mc3_core_domain_summary_t;

typedef struct {
    uint16_t valid_channel_mask;
    float channel_mv[MC3_PVT_VOLTAGE_CHANNEL_COUNT];
} mc3_pvt_voltage_reading_t;

typedef void (*mc3_frequency_step_callback_t)(void *global_state, float actual_frequency);

uint8_t MC3_init(void * GLOBAL_STATE);
void MC3_send_work(void * GLOBAL_STATE, bm_job * next_bm_job);
void MC3_set_version_mask(uint32_t version_mask);
int MC3_set_max_baud(void);
int MC3_set_default_baud(void);
float MC3_send_hash_frequency(float frequency);
bool MC3_ramp_hash_frequency(void * GLOBAL_STATE);
bool MC3_start_qualification_work(void * GLOBAL_STATE);
bool MC3_refresh_qualification_work(void * GLOBAL_STATE);
bool MC3_reapply_active_qualification_work(void);
bool MC3_set_active_pll_running_preserve_config(void * GLOBAL_STATE,
    bool running);
bool MC3_qualify_frequency(void * GLOBAL_STATE, float frequency,
    mc3_qualification_result_t *result);
bool MC3_qualify_chip_frequencies(void * GLOBAL_STATE,
    const float *frequencies_mhz, uint8_t frequency_count,
    mc3_qualification_result_t *result);
bool MC3_set_chip_hash_frequency_live(void * GLOBAL_STATE, uint8_t chip_id,
    float frequency_mhz, float *actual_frequency_mhz);
bool MC3_measure_active_frequency_profile(void * GLOBAL_STATE,
    mc3_qualification_result_t *result);
bool MC3_start_active_spdlog_observation(uint32_t duration_ms);
bool MC3_read_core_domain_summary(uint8_t chip_id,
    mc3_core_domain_summary_t *summary);
void MC3_set_frequency_step_callback(mc3_frequency_step_callback_t callback);
void MC3_set_ping_pong_pll_experiment_enabled(bool enabled);
task_result * MC3_process_work(void * GLOBAL_STATE);
void MC3_read_registers(void * GLOBAL_STATE);
void MC3_set_nonce_space(double nonce_percent, float frequency, uint16_t asic_count, uint16_t cores);
uint8_t MC3_read_temperatures(float *temps, size_t max_temps);
uint8_t MC3_read_vdd_voltages(float *voltages_mv, size_t max_voltages);
uint8_t MC3_read_pvt_voltages(mc3_pvt_voltage_reading_t *readings,
    size_t max_readings);
bool MC3_start_core_scan(void);
void MC3_get_core_scan_status(asic_tuning_status_t *status);
bool MC3_get_core_scan_chip_result(uint8_t chip_id, asic_tuning_chip_result_t *result);

#endif /* MC3_H_ */
