#include <string.h>
#include <stdlib.h>
#include "unity.h"
#define TEST_STUB_GLOBAL_STATE_H
#include "main/global_state.h"
#include "asic.h"

#define ASIC_get_asic_measurement measurement_test_total
#define ASIC_get_domain_measurement measurement_test_domain
#define ASIC_get_temperature measurement_test_temperature
#define BZM_get_hashrate_measurement measurement_test_bzm
#define BZM_read_temperature measurement_test_bzm_temperature
#define BZM_get_telemetry_snapshot measurement_test_telemetry
#define BZM_try_get_state measurement_test_clocks
#define esp_timer_get_time measurement_test_now
#include "components/asic/asic_measurement.c"

static bool software_available;
static bool telemetry_available, clocks_available;
static bzm_telemetry_store_t test_telemetry;
static bzm_bringup_state_t test_clocks;
int64_t measurement_test_now(void) { return 10000000; }
bool measurement_test_telemetry(bzm_telemetry_store_t *snapshot)
{
    *snapshot = test_telemetry;
    return telemetry_available;
}
bool measurement_test_clocks(bzm_bringup_state_t *state)
{
    *state = test_clocks;
    return clocks_available;
}
static uint8_t requested_asic;
static unsigned temperature_reads;
float measurement_test_bzm_temperature(GlobalState *state)
{
    (void)state;
    ++temperature_reads;
    return 72;
}
bool measurement_test_bzm(GlobalState *state, uint8_t asic_nr, uint64_t *time_us, float *hashrate)
{
    (void)state;
    requested_asic = asic_nr;
    *time_us = 3000000;
    *hashrate = 350;
    return software_available;
}

TEST_CASE("ASIC measurement interface returns Bitmain totals and domains without changing accounting", "[asic][hashrate]")
{
    GlobalState *state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    state->DEVICE_CONFIG.family = FAMILY_GAMMA;
    measurement_t total = {.value = 512, .time_us = 2000000, .hashrate = 1200};
    measurement_t domains[4] = {{.time_us = 2000000, .hashrate = 300}};
    measurement_t *domain_pointers[] = {domains};
    state->HASHRATE_MONITOR_MODULE.total_measurement = &total;
    state->HASHRATE_MONITOR_MODULE.domain_measurements = domain_pointers;
    pthread_mutex_init(&state->HASHRATE_MONITOR_MODULE.lock, NULL);
    atomic_init(&state->HASHRATE_MONITOR_MODULE.is_initialized, true);
    asic_domain_measurement_t sample;
    TEST_ASSERT_EQUAL(ESP_OK, ASIC_get_asic_measurement(state, 0, &sample));
    TEST_ASSERT_EQUAL_FLOAT(1200, sample.hashrate);
    TEST_ASSERT_TRUE((uint64_t)(2000000) == (uint64_t)(sample.time_us));
    TEST_ASSERT_EQUAL(ESP_OK, ASIC_get_domain_measurement(state, 0, 0, &sample));
    TEST_ASSERT_EQUAL_FLOAT(300, sample.hashrate);
    TEST_ASSERT_EQUAL_UINT32(512, total.value);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ASIC_get_asic_measurement(state, 1, &sample));
    TEST_ASSERT_EQUAL_FLOAT(0, sample.hashrate);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ASIC_get_domain_measurement(state, 0, 4, &sample));
    atomic_store(&state->HASHRATE_MONITOR_MODULE.is_initialized, false);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ASIC_get_asic_measurement(state, 0, &sample));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ASIC_get_asic_measurement(NULL, 0, &sample));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ASIC_get_asic_measurement(state, 0, NULL));
    state->DEVICE_CONFIG.family = FAMILY_BONANZA;
    software_available = true;
    TEST_ASSERT_EQUAL(ESP_OK, ASIC_get_asic_measurement(state, 3, &sample));
    TEST_ASSERT_EQUAL_UINT8(3, requested_asic);
    TEST_ASSERT_EQUAL_FLOAT(350, sample.hashrate);
    TEST_ASSERT_EQUAL(ESP_OK, ASIC_get_domain_measurement(state, 3, 0, &sample));
    TEST_ASSERT_EQUAL_FLOAT(350, sample.hashrate);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, ASIC_get_domain_measurement(state, 3, 1, &sample));
    software_available = false;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ASIC_get_asic_measurement(state, 0, &sample));
    TEST_ASSERT_EQUAL_FLOAT(0, sample.hashrate);
    pthread_mutex_destroy(&state->HASHRATE_MONITOR_MODULE.lock);
    free(state);
}

TEST_CASE("ASIC native temperature requires a capability and a supported driver", "[asic][temperature]")
{
    GlobalState *state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    temperature_reads = 0;
    TEST_ASSERT_EQUAL_FLOAT(-1, ASIC_get_temperature(NULL));
    state->DEVICE_CONFIG.family = FAMILY_BONANZA;
    TEST_ASSERT_EQUAL_FLOAT(72, ASIC_get_temperature(state));
    TEST_ASSERT_EQUAL_UINT32(1, temperature_reads);
    state->DEVICE_CONFIG.family.asic.native_temperature = false;
    TEST_ASSERT_EQUAL_FLOAT(-1, ASIC_get_temperature(state));
    state->DEVICE_CONFIG.family = FAMILY_GAMMA;
    TEST_ASSERT_EQUAL_FLOAT(-1, ASIC_get_temperature(state));
    state->DEVICE_CONFIG.family.asic.native_temperature = true;
    TEST_ASSERT_EQUAL_FLOAT(-1, ASIC_get_temperature(state));
    TEST_ASSERT_EQUAL_UINT32(1, temperature_reads);
    free(state);
}

TEST_CASE("ASIC chip details report fresh BZM telemetry and per-PLL clocks", "[asic][bap]")
{
    GlobalState *state = calloc(1, sizeof(*state));
    TEST_ASSERT_NOT_NULL(state);
    asic_chip_detail_t details[4];

    state->DEVICE_CONFIG.family = FAMILY_GAMMA;
    TEST_ASSERT_EQUAL(0, ASIC_get_chip_details(state, details, 4));
    TEST_ASSERT_EQUAL(0, ASIC_get_chip_details(NULL, details, 4));

    state->DEVICE_CONFIG.family = FAMILY_BONANZA;
    telemetry_available = clocks_available = false;
    TEST_ASSERT_EQUAL(0, ASIC_get_chip_details(state, details, 4));

    memset(&test_telemetry, 0, sizeof(test_telemetry));
    for (int chip = 0; chip < 4; ++chip) {
        bzm_telemetry_sample_t *sample = &test_telemetry.samples[chip];
        sample->received = sample->thermal_valid = sample->voltage_valid = true;
        sample->asic_id = bzm_asic_wire_ids[chip];
        sample->timestamp_us = 9500000;
        sample->temperature_c = 55.0f + chip;
        sample->ch0_mv = 350.0f + chip;
        sample->ch1_mv = 349.0f;
        test_clocks.domain_clock_mhz[chip][0] = 800;
        test_clocks.domain_clock_mhz[chip][1] = 775;
    }
    test_telemetry.samples[2].timestamp_us = 7000000;   // Older than two seconds
    test_telemetry.samples[3].voltage_valid = false;
    test_clocks.running = true;
    telemetry_available = clocks_available = true;

    TEST_ASSERT_EQUAL(4, ASIC_get_chip_details(state, details, 4));
    TEST_ASSERT_EQUAL_UINT8(2, details[0].domain_count);
    TEST_ASSERT_TRUE(details[1].has_temperature);
    TEST_ASSERT_EQUAL_FLOAT(56, details[1].temperature_c);
    TEST_ASSERT_TRUE(details[1].has_voltage);
    TEST_ASSERT_EQUAL_FLOAT(351, details[1].domain_voltage_mv[0]);
    TEST_ASSERT_EQUAL_FLOAT(349, details[1].domain_voltage_mv[1]);
    TEST_ASSERT_FALSE(details[2].has_temperature);
    TEST_ASSERT_FALSE(details[2].has_voltage);
    TEST_ASSERT_TRUE(details[3].has_temperature);
    TEST_ASSERT_FALSE(details[3].has_voltage);
    TEST_ASSERT_TRUE(details[3].has_frequency);
    TEST_ASSERT_EQUAL_FLOAT(775, details[3].domain_frequency_mhz[1]);

    test_clocks.running = false;    // Clocks mean nothing until the chips run
    TEST_ASSERT_EQUAL(4, ASIC_get_chip_details(state, details, 4));
    TEST_ASSERT_FALSE(details[0].has_frequency);
    TEST_ASSERT_EQUAL(2, ASIC_get_chip_details(state, details, 2));
    free(state);
}
