#include <string.h>
#include "esp_timer.h"
#include "asic.h"
#include "bzm/driver.h"
#include "bzm/topology.h"
#include "global_state.h"

float ASIC_get_temperature(GlobalState *state)
{
    if (state == NULL || !state->DEVICE_CONFIG.family.asic.native_temperature) return -1;
    switch (state->DEVICE_CONFIG.family.asic.id) {
        case BZM:
            return BZM_read_temperature(state);
        default:
            return -1;
    }
}

static esp_err_t get_measurement(GlobalState *state, uint8_t asic_nr, uint8_t domain_nr,
                                 bool total, asic_domain_measurement_t *measurement)
{
    if (state == NULL || measurement == NULL) return ESP_ERR_INVALID_ARG;
    *measurement = (asic_domain_measurement_t){0};
    if (asic_nr >= state->DEVICE_CONFIG.family.asic_count ||
        (!total && domain_nr >= state->DEVICE_CONFIG.family.asic.hash_domains)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state->DEVICE_CONFIG.family.asic.id == BZM) {
        asic_domain_measurement_t sample = {0};
        if (!BZM_get_hashrate_measurement(state, asic_nr, &sample.time_us, &sample.hashrate)) {
            return ESP_ERR_INVALID_STATE;
        }
        *measurement = sample;
        return ESP_OK;
    }
    HashrateMonitorModule *monitor = &state->HASHRATE_MONITOR_MODULE;
    if (!monitor->is_initialized) return ESP_ERR_INVALID_STATE;
    pthread_mutex_lock(&monitor->lock);
    measurement_t sample = total ? monitor->total_measurement[asic_nr]
                                : monitor->domain_measurements[asic_nr][domain_nr];
    pthread_mutex_unlock(&monitor->lock);
    measurement->time_us = sample.time_us;
    measurement->hashrate = sample.hashrate;
    return ESP_OK;
}

esp_err_t ASIC_get_asic_measurement(GlobalState *state, uint8_t asic_nr,
                                    asic_domain_measurement_t *measurement)
{
    return get_measurement(state, asic_nr, 0, true, measurement);
}

esp_err_t ASIC_get_domain_measurement(GlobalState *state, uint8_t asic_nr, uint8_t domain_nr,
                                      asic_domain_measurement_t *measurement)
{
    return get_measurement(state, asic_nr, domain_nr, false, measurement);
}

static size_t get_bzm_chip_details(asic_chip_detail_t *details, size_t count)
{
    _Static_assert(BZM_BRINGUP_PLL_COUNT <= ASIC_DETAIL_MAX_DOMAINS, "BZM domains exceed the detail table");
    bzm_telemetry_store_t telemetry;
    bzm_bringup_state_t clocks;
    bool has_telemetry = BZM_get_telemetry_snapshot(&telemetry);
    /* Never wait on the driver: a busy reactor means no clocks this time. */
    bool has_clocks = BZM_try_get_state(&clocks) && clocks.running;
    if (!has_telemetry && !has_clocks) return 0;
    if (count > BZM_MAX_ASIC_COUNT) count = BZM_MAX_ASIC_COUNT;

    uint64_t now_us = esp_timer_get_time();
    for (size_t chip = 0; chip < count; ++chip) {
        asic_chip_detail_t *detail = &details[chip];
        detail->domain_count = BZM_BRINGUP_PLL_COUNT;
        if (has_telemetry) {
            /* Samples are kept in chain order, the same order as the clocks and hashrate. */
            const bzm_telemetry_sample_t *sample = &telemetry.samples[chip];
            bool fresh = sample->received && sample->asic_id == bzm_asic_wire_ids[chip] &&
                         sample->timestamp_us != 0 && sample->timestamp_us <= now_us &&
                         now_us - sample->timestamp_us <= BZM_TELEMETRY_MAX_AGE_US;
            if (fresh && sample->thermal_valid) {
                detail->has_temperature = true;
                detail->temperature_c = sample->temperature_c;
            }
            if (fresh && sample->voltage_valid) {
                /* CH0 is the bottom engine stack and CH1 the top one. */
                detail->has_voltage = true;
                detail->domain_voltage_mv[0] = sample->ch0_mv;
                detail->domain_voltage_mv[1] = sample->ch1_mv;
            }
        }
        if (has_clocks) {
            detail->has_frequency = true;
            for (size_t pll = 0; pll < BZM_BRINGUP_PLL_COUNT; ++pll) {
                detail->domain_frequency_mhz[pll] = clocks.domain_clock_mhz[chip][pll];
            }
        }
    }
    return count;
}

size_t ASIC_get_chip_details(GlobalState *state, asic_chip_detail_t *details, size_t max_chips)
{
    if (state == NULL || details == NULL) return 0;
    size_t count = state->DEVICE_CONFIG.family.asic_count;
    if (count > max_chips) count = max_chips;
    memset(details, 0, count * sizeof(*details));
    switch (state->DEVICE_CONFIG.family.asic.id) {
        case BZM:
            return get_bzm_chip_details(details, count);
        default:
            return 0;
    }
}
