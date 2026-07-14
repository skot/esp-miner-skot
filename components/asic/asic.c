#include <string.h>
#include <stdio.h>

#include <esp_log.h>

#include "bm1397.h"
#include "bm1366.h"
#include "bm1368.h"
#include "bm1370.h"
#include "mc3.h"

#include "asic.h"
#include "device_config.h"
#include "frequency_transition_bmXX.h"

static const char *TAG = "asic";

uint8_t ASIC_init(GlobalState * GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Initializing %dx %s", GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            return BM1397_init(GLOBAL_STATE);
        case BM1366:
            return BM1366_init(GLOBAL_STATE);
        case BM1368:
            return BM1368_init(GLOBAL_STATE);
        case BM1370:
            return BM1370_init(GLOBAL_STATE);
        case MC3:
            return MC3_init(GLOBAL_STATE);
        default:
            ESP_LOGE(TAG, "Unknown ASIC id %d", GLOBAL_STATE->DEVICE_CONFIG.family.asic.id);
            return 0;
    }
}

task_result * ASIC_process_work(GlobalState * GLOBAL_STATE)
{
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            return BM1397_process_work(GLOBAL_STATE);
        case BM1366:
            return BM1366_process_work(GLOBAL_STATE);
        case BM1368:
            return BM1368_process_work(GLOBAL_STATE);
        case BM1370:
            return BM1370_process_work(GLOBAL_STATE);
        case MC3:
            return MC3_process_work(GLOBAL_STATE);
        default:
            ESP_LOGE(TAG, "Unknown ASIC id %d — cannot process work", GLOBAL_STATE->DEVICE_CONFIG.family.asic.id);
            return NULL;
    }
}

int ASIC_set_max_baud(GlobalState * GLOBAL_STATE)
{
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            return BM1397_set_max_baud();
        case BM1366:
            return BM1366_set_max_baud();
        case BM1368:
            return BM1368_set_max_baud();
        case BM1370:
            return BM1370_set_max_baud();
        case MC3:
            return MC3_set_max_baud();
        default:
            ESP_LOGE(TAG, "Unknown ASIC id %d — cannot set max baud", GLOBAL_STATE->DEVICE_CONFIG.family.asic.id);
            return 0;
    }
}

void ASIC_send_work(GlobalState * GLOBAL_STATE, void * next_job)
{
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            BM1397_send_work(GLOBAL_STATE, next_job);
            break;
        case BM1366:
            BM1366_send_work(GLOBAL_STATE, next_job);
            break;
        case BM1368:
            BM1368_send_work(GLOBAL_STATE, next_job);
            break;
        case BM1370:
            BM1370_send_work(GLOBAL_STATE, next_job);
            break;
        case MC3:
            MC3_send_work(GLOBAL_STATE, next_job);
            break;
        default:
            ESP_LOGE(TAG, "Unknown ASIC id %d — cannot send work", GLOBAL_STATE->DEVICE_CONFIG.family.asic.id);
            break;
    }
}

void ASIC_set_version_mask(GlobalState * GLOBAL_STATE, uint32_t mask)
{
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            BM1397_set_version_mask(mask);
            break;
        case BM1366:
            BM1366_set_version_mask(mask);
            break;
        case BM1368:
            BM1368_set_version_mask(mask);
            break;
        case BM1370:
            BM1370_set_version_mask(mask);
            break;
        case MC3:
            MC3_set_version_mask(mask);
            break;
        default:
            ESP_LOGE(TAG, "Unknown ASIC id %d — cannot set version mask", GLOBAL_STATE->DEVICE_CONFIG.family.asic.id);
            break;
    }
}

void ASIC_set_frequency(GlobalState * GLOBAL_STATE)
{
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            do_frequency_transition(GLOBAL_STATE, BM1397_send_hash_frequency);
            return;
        case BM1366:
            do_frequency_transition(GLOBAL_STATE, BM1366_send_hash_frequency);
            return;
        case BM1368:
            do_frequency_transition(GLOBAL_STATE, BM1368_send_hash_frequency);
            return;
        case BM1370:
            do_frequency_transition(GLOBAL_STATE, BM1370_send_hash_frequency);
            return;
        case MC3:
            MC3_ramp_hash_frequency(GLOBAL_STATE);
            return;
        default:
            ESP_LOGE(TAG, "Unknown ASIC id %d — cannot set frequency", GLOBAL_STATE->DEVICE_CONFIG.family.asic.id);
            return;
    }
}

void ASIC_set_nonce_space(GlobalState * GLOBAL_STATE)
{
    float nonce_percent = 1.0;
    int cores = GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count;
    int asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    float frequency = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency;

    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            return;
        case BM1366:
            BM1366_set_nonce_space(nonce_percent, frequency, asic_count, cores);
            return;
        case BM1368:
            BM1368_set_nonce_space(nonce_percent, frequency, asic_count, cores);
            return;
        case BM1370:
            BM1370_set_nonce_space(nonce_percent, frequency, asic_count, cores);
            return;
        case MC3:
            MC3_set_nonce_space(nonce_percent, frequency, asic_count, cores);
            return;
        default:
            ESP_LOGE(TAG, "Unknown ASIC id %d — cannot set nonce space", GLOBAL_STATE->DEVICE_CONFIG.family.asic.id);
            return;
    }
}

double ASIC_get_asic_job_frequency_ms(GlobalState * GLOBAL_STATE)
{
    float freq = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value;
    int cores = GLOBAL_STATE->DEVICE_CONFIG.family.asic.core_count;
    int small_cores = GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count;
    int asic_count = GLOBAL_STATE->DEVICE_CONFIG.family.asic_count;
    int asic_default_timeout_divided = GLOBAL_STATE->DEVICE_CONFIG.family.asic.default_asic_timeout / _next_power_of_two(asic_count);

    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            // no version-rolling so same Nonce Space is splitted between Big Cores
            return calculate_bm_timeout_ms(freq, asic_count, small_cores, cores, 4, 1.0, asic_default_timeout_divided);
        case BM1366:
        case BM1368:
        case BM1370:
            return asic_default_timeout_divided;
        case MC3:
            return 100;
        default:
            ESP_LOGE(TAG, "Unknown ASIC id %d — cannot compute job frequency", GLOBAL_STATE->DEVICE_CONFIG.family.asic.id);
            return 500;
    }
}

void ASIC_read_registers(GlobalState * GLOBAL_STATE)
{
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case BM1397:
            BM1397_read_registers();
            break;
        case BM1366:
            BM1366_read_registers();
            break;
        case BM1368:
            BM1368_read_registers();
            break;
        case BM1370:
            BM1370_read_registers();
            break;
        case MC3:
            MC3_read_registers(GLOBAL_STATE);
            break;
        default:
            ESP_LOGE(TAG, "Unknown ASIC id %d — cannot read registers", GLOBAL_STATE->DEVICE_CONFIG.family.asic.id);
            break;
    }
}

bool ASIC_start_tuning(GlobalState * GLOBAL_STATE)
{
    if (!GLOBAL_STATE->ASIC_initalized) {
        return false;
    }

    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case MC3:
            return MC3_start_core_scan();
        default:
            return false;
    }
}

void ASIC_get_tuning_status(GlobalState * GLOBAL_STATE, asic_tuning_status_t *status)
{
    memset(status, 0, sizeof(*status));

    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case MC3:
            MC3_get_core_scan_status(status);
            return;
        default:
            status->state = ASIC_TUNING_UNSUPPORTED;
            snprintf(status->message, sizeof(status->message), "Tuning is not supported for %s",
                GLOBAL_STATE->DEVICE_CONFIG.family.asic.name);
            return;
    }
}

bool ASIC_get_tuning_chip_result(GlobalState * GLOBAL_STATE, uint8_t chip_id, asic_tuning_chip_result_t *result)
{
    switch (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id) {
        case MC3:
            return MC3_get_core_scan_chip_result(chip_id, result);
        default:
            return false;
    }
}

const char *ASIC_tuning_state_name(asic_tuning_state_t state)
{
    switch (state) {
        case ASIC_TUNING_UNSUPPORTED:
            return "unsupported";
        case ASIC_TUNING_IDLE:
            return "idle";
        case ASIC_TUNING_QUEUED:
            return "queued";
        case ASIC_TUNING_MEASURING:
            return "measuring";
        case ASIC_TUNING_READING:
            return "reading";
        case ASIC_TUNING_COMPLETE:
            return "complete";
        case ASIC_TUNING_ERROR:
            return "error";
        default:
            return "unknown";
    }
}
