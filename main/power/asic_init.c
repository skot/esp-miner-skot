#include "asic_init.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "asic.h"
#include "asic_common.h"
#include "mc3.h"
#include "serial.h"
#include "asic_reset.h"
#include "device_config.h"
#include "nvs_config.h"
#include "vcore.h"
#include "TPS546.h"
#include "driver/gpio.h"

#define GPIO_PROTO_VDDIO_5V_EN GPIO_NUM_21
#define PROTO_VDDIO_STABILIZATION_MS 100
#define PROTO_VCORE_STABILIZATION_MS 100
#define PROTO_VCORE_STARTUP_MV 800
#define PROTO_VCORE_INIT_FALLBACK_MAX_MV 960
#define PROTO_VCORE_INIT_FALLBACK_STEP_MV 50
#define PROTO_VCORE_RAMP_STEP_MV 10
#define PROTO_VCORE_RAMP_SETTLE_MS 100
#define PROTO_MAX_VDD_READINGS 8
#define PROTO_FREQUENCY_STEP_MHZ 25
#define PROTO_MAX_DOMAIN_DELTA_MV 50
#define PROTO_MIN_PASS_RATE_PERCENT 97.0f
#define PROTO_MIN_THROUGHPUT_PERCENT 97.0f
#define PROTO_MIN_CHIP_THROUGHPUT_PERCENT 97.5f
#define PROTO_BALANCE_SELECTION_DEADBAND_MV 5
#define PROTO_THROUGHPUT_SELECTION_DEADBAND_PERCENT 0.25f
#define PROTO_POST_BALANCE_MAX_CONSECUTIVE_REJECTIONS 3
#define PROTO_PLL_PROFILE_REAPPLY_ATTEMPTS 3
#define PROTO_INTERNAL_DOMAIN_DIAGNOSTIC_MIN_REQUEST_MHZ 425.0f
#define PROTO_INTERNAL_DOMAIN_COLLAPSE_PERCENT 90.0f
#define PROTO_RUN_VOLTAGE_FREQUENCY_MATRIX 0
#define PROTO_VF_MATRIX_BASELINE_MHZ 400.0f
#define PROTO_VF_MATRIX_SETTLE_MS 250
// Scope experiment: keep every frequency transition uniform across the chain.
// This bypasses ping-pong, staggered per-chip updates, and mixed-frequency
// recovery so the 400 -> 425 MHz transition uses the normal broadcast PLL0
// stop/reprogram/start sequence on all four ASICs simultaneously.
#define PROTO_FORCE_BROADCAST_PLL_UPDATES 1
// Retained for diagnostics. Even with every per-core SPD_TOP pair explicitly
// programmed, active cores did not live-transfer to the alternate locked PLL.
#define PROTO_RUN_PING_PONG_PLL_EXPERIMENT 0
#define PROTO_RUN_STAGGERED_PLL_DIAGNOSTIC 1
#define PROTO_STAGGERED_PLL_MIN_REQUEST_MHZ 450.0f
#define PROTO_STAGGERED_PLL_BASELINE_MHZ 400.0f
#define PROTO_STAGGERED_PLL_SETTLE_MS 250
// Focused D2 diagnostic: from a qualified all-400 MHz baseline, raise only
// A1 to 425 MHz, restore it, then raise only A2 to 425 MHz. Capture fresh PVT
// taps and per-core domain counters at every stable profile.
#define PROTO_RUN_ADDRESSED_D2_DIAGNOSTIC 0
#define PROTO_ADDRESSED_D2_BASELINE_MHZ 400.0f
#define PROTO_ADDRESSED_D2_TEST_MHZ 425.0f
#define PROTO_ADDRESSED_D2_SETTLE_MS 500
// Decisive comparison: from fresh all-400 qualification work, update all four
// PLLs in alternating half-stack order, immediately reset/reapply identical
// work, and observe whether 425 MHz survives without the broadcast PLL-off
// interval.
#define PROTO_RUN_CLEAN_ALL_CHIPS_DIAGNOSTIC 0
#define PROTO_CLEAN_ALL_BASELINE_MHZ 400.0f
#define PROTO_CLEAN_ALL_TEST_MHZ 425.0f
#define PROTO_CLEAN_ALL_RECOVERY_OBSERVATION_MS 30000
#define PROTO_CLEAN_ALL_RECOVERY_MAX_DOMAIN_MV 1200
// Load-release experiment: after a measured 425 MHz rejection, keep feeding
// work until the failed domain split is stable, then stop PLL0 without changing
// its divider configuration. Observe the electrical recovery before restarting
// that same PLL configuration and rolling back.
#define PROTO_RUN_425_REJECT_DIAGNOSTIC_HOLD 0
#define PROTO_REJECT_DIAGNOSTIC_LOADED_SECONDS 10
#define PROTO_REJECT_DIAGNOSTIC_REFRESH_SECONDS 5
#define PROTO_REJECT_DIAGNOSTIC_MAX_DOMAIN_MV 1200
#define PROTO_REJECT_DIAGNOSTIC_RELEASE_HOLD_MS 15000
#define PROTO_REJECT_DIAGNOSTIC_FAST_SAMPLE_MS 250
#define PROTO_REJECT_DIAGNOSTIC_FAST_SAMPLE_DURATION_MS 5000
#define PROTO_REJECT_DIAGNOSTIC_SLOW_SAMPLE_MS 1000

typedef struct {
    bool accepted;
    bool domains_valid;
    int domain_delta_mv;
    int absolute_domain_delta_mv;
    float throughput_percent;
    float min_chip_throughput_percent;
    float min_pass_rate_percent;
    float average_frequency_mhz;
} proto_qualification_metrics_t;

static bool proto_load_release_diagnostic_ran_this_boot = false;

static const float PROTO_FINE_BALANCE_FREQUENCIES_MHZ[] = {
    402.7778f,
    405.5556f,
    408.3333f,
    411.1111f,
    413.8889f,
    416.6667f,
    419.4445f,
    422.2222f,
};

static const float PROTO_POST_BALANCE_FREQUENCIES_MHZ[] = {
    428.1250f, 431.2500f, 434.3750f, 437.5000f,
    440.6250f, 443.7500f, 446.8750f, 450.0000f,
    453.1250f, 456.2500f, 459.3750f, 462.5000f,
    465.6250f, 468.7500f, 471.8750f, 475.0000f,
    478.1250f, 481.2500f, 484.3750f, 487.5000f,
    490.6250f, 493.7500f, 496.8750f, 500.0000f,
    504.1667f, 508.3333f, 512.5000f, 516.6667f,
    520.8333f, 525.0000f, 529.1667f, 533.3333f,
    537.5000f, 541.6667f, 545.8333f, 550.0000f,
    554.1667f, 558.3333f, 562.5000f, 566.6667f,
    570.8333f, 575.0000f, 579.1667f, 583.3333f,
    587.5000f, 591.6667f, 595.8333f, 600.0000f,
};

static const float PROTO_A1_DOMAIN_DIAGNOSTIC_FREQUENCIES_MHZ[] = {
    425.0000f,
    428.1250f,
    431.2500f,
    434.3750f,
    437.5000f,
    440.6250f,
    443.7500f,
    446.8750f,
    450.0000f,
};

static const uint16_t PROTO_VF_MATRIX_VOLTAGES_MV[] = {
    1140,
    1160,
    1180,
};

static const float PROTO_VF_MATRIX_FREQUENCIES_MHZ[] = {
    419.4445f,
    422.2222f,
    425.0000f,
    428.1250f,
    431.2500f,
};

static const float PROTO_STAGGERED_PLL_FREQUENCIES_MHZ[] = {
    425.0000f,
    428.1250f,
    431.2500f,
    434.3750f,
    437.5000f,
    440.6250f,
    443.7500f,
    446.8750f,
    450.0000f,
};

static const uint8_t PROTO_STAGGERED_PLL_ORDERS[][4] = {
    {2, 3, 0, 1},
    {0, 1, 2, 3},
};

static const char *PROTO_STAGGERED_PLL_ORDER_NAMES[] = {
    "upper-first(A3,A4,A1,A2)",
    "lower-first(A1,A2,A3,A4)",
};

static const char *TAG = "asic_init";

static esp_err_t set_proto_vddio_5v(const GlobalState *GLOBAL_STATE, bool enabled)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.family.id != PROTO) {
        return ESP_OK;
    }

    gpio_config_t vddio_5v_conf = {
        .pin_bit_mask = (1ULL << GPIO_PROTO_VDDIO_5V_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&vddio_5v_conf), TAG, "Failed to configure Proto VDDIO 5V enable GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_PROTO_VDDIO_5V_EN, enabled ? 1 : 0), TAG,
        "Failed to set Proto VDDIO 5V enable GPIO");
    ESP_LOGI(TAG, "Proto VDDIO 5V %s on GPIO%d", enabled ? "enabled" : "disabled", GPIO_PROTO_VDDIO_5V_EN);

    return ESP_OK;
}

esp_err_t asic_prepare_power(const GlobalState *GLOBAL_STATE)
{
    ESP_RETURN_ON_ERROR(asic_hold_reset_low(), TAG, "Failed to hold ASIC reset low");
    ESP_RETURN_ON_ERROR(VCORE_prepare_asic_enable(GLOBAL_STATE), TAG,
        "Failed to disable Proto Vcore enable");
    ESP_RETURN_ON_ERROR(set_proto_vddio_5v(GLOBAL_STATE, false), TAG, "Failed to disable Proto VDDIO 5V");
    return ESP_OK;
}

static uint16_t get_proto_target_core_voltage_mv(const GlobalState *GLOBAL_STATE)
{
    return GLOBAL_STATE->SELF_TEST_MODULE.is_active
        ? GLOBAL_STATE->DEVICE_CONFIG.family.asic.default_voltage_mv
        : nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
}

static esp_err_t set_proto_core_voltage(GlobalState *GLOBAL_STATE, uint16_t core_voltage_mv)
{
    ESP_RETURN_ON_ERROR(VCORE_set_voltage(GLOBAL_STATE, (float)core_voltage_mv / 1000.0f), TAG,
        "Failed to set Proto Vcore");
    return ESP_OK;
}

static esp_err_t enable_proto_power(GlobalState *GLOBAL_STATE, uint16_t startup_core_voltage_mv)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.family.id != PROTO) {
        return ESP_OK;
    }

    // MC3's stacked VDD domains must be established before the 1.2 V/0.8 V
    // auxiliary rails (and the oscillators they power) are enabled.
    ESP_LOGI(TAG, "Starting Proto Vcore at %u mV/domain", startup_core_voltage_mv);
    ESP_RETURN_ON_ERROR(set_proto_core_voltage(GLOBAL_STATE, startup_core_voltage_mv), TAG,
        "Failed to enable Proto startup Vcore");
    ESP_LOGI(TAG, "Waiting %u ms for Proto Vcore to stabilize", PROTO_VCORE_STABILIZATION_MS);
    vTaskDelay(pdMS_TO_TICKS(PROTO_VCORE_STABILIZATION_MS));

    ESP_RETURN_ON_ERROR(set_proto_vddio_5v(GLOBAL_STATE, true), TAG, "Failed to enable Proto VDDIO 5V");
    ESP_LOGI(TAG, "Waiting %u ms for Proto VDDIO/clock rails to stabilize", PROTO_VDDIO_STABILIZATION_MS);
    vTaskDelay(pdMS_TO_TICKS(PROTO_VDDIO_STABILIZATION_MS));

    return ESP_OK;
}

static void log_proto_vdd_measurements(GlobalState *GLOBAL_STATE,
                                       uint16_t commanded_core_voltage_mv)
{
    float voltages_mv[PROTO_MAX_VDD_READINGS] = {0};
    uint8_t count = MC3_read_vdd_voltages(voltages_mv, PROTO_MAX_VDD_READINGS);

    if (count == 0) {
        ESP_LOGW(TAG, "Vcore ramp command=%u mV/domain; MC3 VDD measurement unavailable",
            commanded_core_voltage_mv);
    } else {
        char readings[128] = {0};
        size_t used = 0;
        for (uint8_t chip_id = 0; chip_id < count && used < sizeof(readings); chip_id++) {
            int written = snprintf(readings + used, sizeof(readings) - used,
                "%s%u:%.1f", chip_id == 0 ? "" : " ", chip_id, voltages_mv[chip_id]);
            if (written < 0 || (size_t)written >= sizeof(readings) - used) {
                break;
            }
            used += written;
        }

        ESP_LOGI(TAG, "Vcore ramp command=%u mV/domain; MC3 VDD mV [%s]",
            commanded_core_voltage_mv, readings);
    }

    int16_t regulator_voltage_mv = VCORE_get_regulator_voltage_mv(GLOBAL_STATE);
    uint16_t lower_domain_mv = 0;
    uint16_t upper_domain_mv = 0;
    if (regulator_voltage_mv > 0 &&
        VCORE_get_domain_voltages_mv(GLOBAL_STATE, (uint16_t)regulator_voltage_mv,
            &lower_domain_mv, &upper_domain_mv) == ESP_OK) {
        ESP_LOGI(TAG,
            "Vcore ramp command=%u mV/domain; PCB domains lower=%u upper=%u delta=%+d mV total=%d mV",
            commanded_core_voltage_mv,
            lower_domain_mv,
            upper_domain_mv,
            (int)upper_domain_mv - (int)lower_domain_mv,
            regulator_voltage_mv);
    }
}

static bool proto_is_400_or_425_uniform_profile(
    const mc3_qualification_result_t *result)
{
    if (result == NULL || result->chip_count == 0) {
        return false;
    }

    bool diagnostic_frequency =
        (result->frequency_mhz >= 399.9f && result->frequency_mhz <= 400.1f) ||
        (result->frequency_mhz >= 424.9f && result->frequency_mhz <= 425.1f);
    if (!diagnostic_frequency) {
        return false;
    }

    for (uint8_t chip_id = 0; chip_id < result->chip_count; chip_id++) {
        float delta_mhz = result->chip_frequency_mhz[chip_id] >
                result->frequency_mhz
            ? result->chip_frequency_mhz[chip_id] - result->frequency_mhz
            : result->frequency_mhz - result->chip_frequency_mhz[chip_id];
        if (delta_mhz > 0.01f) {
            return false;
        }
    }
    return true;
}

static float proto_pvt_average(const mc3_pvt_voltage_reading_t *reading,
                               const uint8_t *channels, size_t count)
{
    float sum_mv = 0.0f;
    for (size_t i = 0; i < count; i++) {
        sum_mv += reading->channel_mv[channels[i]];
    }
    return sum_mv / count;
}

static void log_proto_pvt_stack_voltages(const char *profile,
                                         uint8_t expected_chip_count)
{
    static const uint8_t required_channels[] = {
        4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    };
    static const uint8_t vss_channels[] = {4, 5, 6, 15};
    static const uint8_t vddi1_channels[] = {7, 8};
    static const uint8_t vddi2_channels[] = {9, 10};
    static const uint8_t vddi3_channels[] = {11, 12};
    static const uint8_t vdd_channels[] = {13, 14};
    const uint16_t required_mask = 0xFFF0U;
    mc3_pvt_voltage_reading_t readings[PROTO_MAX_VDD_READINGS] = {0};
    uint8_t chip_count = MC3_read_pvt_voltages(
        readings, PROTO_MAX_VDD_READINGS);

    if (chip_count == 0) {
        ESP_LOGE(TAG, "MC3 PVT %s: conversion unavailable", profile);
        return;
    }
    if (chip_count != expected_chip_count) {
        ESP_LOGW(TAG, "MC3 PVT %s: read %u ASICs, expected %u",
            profile, chip_count, expected_chip_count);
    }

    for (uint8_t chip_id = 0; chip_id < chip_count; chip_id++) {
        const mc3_pvt_voltage_reading_t *reading = &readings[chip_id];
        ESP_LOGW(TAG,
            "MC3 PVT %s A%u sensors mV: VSS4=%.1f VSS5=%.1f VSS6=%.1f VSS15=%.1f I1[7]=%.1f I1[8]=%.1f I2[9]=%.1f I2[10]=%.1f I3[11]=%.1f I3[12]=%.1f VDD[13]=%.1f VDD[14]=%.1f valid=0x%04X",
            profile, chip_id + 1,
            reading->channel_mv[4], reading->channel_mv[5],
            reading->channel_mv[6], reading->channel_mv[15],
            reading->channel_mv[7], reading->channel_mv[8],
            reading->channel_mv[9], reading->channel_mv[10],
            reading->channel_mv[11], reading->channel_mv[12],
            reading->channel_mv[13], reading->channel_mv[14],
            reading->valid_channel_mask);

        if ((reading->valid_channel_mask & required_mask) != required_mask) {
            char missing[48] = {0};
            size_t used = 0;
            for (size_t i = 0;
                 i < sizeof(required_channels) / sizeof(required_channels[0]);
                 i++) {
                uint8_t channel = required_channels[i];
                if ((reading->valid_channel_mask & (1U << channel)) != 0) {
                    continue;
                }
                int written = snprintf(missing + used, sizeof(missing) - used,
                    "%s%u", used == 0 ? "" : ",", channel);
                if (written < 0 || (size_t)written >= sizeof(missing) - used) {
                    break;
                }
                used += (size_t)written;
            }
            ESP_LOGE(TAG,
                "MC3 PVT %s A%u: missing sensor channel(s) %s; domain voltages not calculated",
                profile, chip_id + 1, missing);
            continue;
        }

        float vss_mv = proto_pvt_average(reading, vss_channels,
            sizeof(vss_channels) / sizeof(vss_channels[0]));
        float vddi1_mv = proto_pvt_average(reading, vddi1_channels,
            sizeof(vddi1_channels) / sizeof(vddi1_channels[0]));
        float vddi2_mv = proto_pvt_average(reading, vddi2_channels,
            sizeof(vddi2_channels) / sizeof(vddi2_channels[0]));
        float vddi3_mv = proto_pvt_average(reading, vddi3_channels,
            sizeof(vddi3_channels) / sizeof(vddi3_channels[0]));
        float vdd_mv = proto_pvt_average(reading, vdd_channels,
            sizeof(vdd_channels) / sizeof(vdd_channels[0]));
        ESP_LOGW(TAG,
            "MC3 PVT %s A%u averaged taps/domains mV: VSS=%.1f I1=%.1f I2=%.1f I3=%.1f VDD=%.1f | D0=%.1f D1=%.1f D2=%.1f D3=%.1f sum=%.1f",
            profile, chip_id + 1, vss_mv, vddi1_mv, vddi2_mv,
            vddi3_mv, vdd_mv, vddi1_mv - vss_mv,
            vddi2_mv - vddi1_mv, vddi3_mv - vddi2_mv,
            vdd_mv - vddi3_mv, vdd_mv - vss_mv);
    }
}

static bool log_proto_internal_domain_summary(
    const char *profile, uint8_t chip_id, uint32_t global_pass,
    mc3_core_domain_summary_t *summary_out);

static esp_err_t ramp_proto_core_voltage(GlobalState *GLOBAL_STATE,
                                         uint16_t initial_core_voltage_mv,
                                         uint16_t target_core_voltage_mv)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.family.id != PROTO ||
        initial_core_voltage_mv >= target_core_voltage_mv) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Ramping Proto Vcore from %u to %u mV/domain at 100 MHz",
        initial_core_voltage_mv, target_core_voltage_mv);

    uint16_t core_voltage_mv = initial_core_voltage_mv;
    while (core_voltage_mv < target_core_voltage_mv) {
        uint16_t remaining_mv = target_core_voltage_mv - core_voltage_mv;
        core_voltage_mv += remaining_mv < PROTO_VCORE_RAMP_STEP_MV
            ? remaining_mv
            : PROTO_VCORE_RAMP_STEP_MV;

        ESP_RETURN_ON_ERROR(set_proto_core_voltage(GLOBAL_STATE, core_voltage_mv), TAG,
            "Failed during Proto Vcore ramp");
        vTaskDelay(pdMS_TO_TICKS(PROTO_VCORE_RAMP_SETTLE_MS));
        log_proto_vdd_measurements(GLOBAL_STATE, core_voltage_mv);
    }

    ESP_LOGI(TAG, "Proto Vcore ramp reached %u mV/domain", target_core_voltage_mv);
    return ESP_OK;
}

static bool evaluate_proto_qualification(GlobalState *GLOBAL_STATE,
                                         const char *profile,
                                         mc3_qualification_result_t *result,
                                         proto_qualification_metrics_t *metrics_out)
{
    float min_pass_rate_percent = 100.0f;
    float min_chip_throughput_percent = 100.0f;
    bool counters_valid = result->chip_count > 0;
    float expected_hashrate_ghs = 0.0f;
    float total_frequency_mhz = 0.0f;
    for (uint8_t chip_id = 0; chip_id < result->chip_count; chip_id++) {
        uint64_t total = (uint64_t)result->passed[chip_id] + result->failed[chip_id];
        float pass_rate_percent = total > 0
            ? ((float)result->passed[chip_id] * 100.0f) / (float)total
            : 0.0f;
        if (pass_rate_percent < min_pass_rate_percent) {
            min_pass_rate_percent = pass_rate_percent;
        }
        counters_valid = counters_valid && total > 0;
        float chip_frequency_mhz = result->chip_frequency_mhz[chip_id] > 0.0f
            ? result->chip_frequency_mhz[chip_id]
            : result->frequency_mhz;
        float chip_expected_hashrate_ghs = chip_frequency_mhz *
            GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count / 1000.0f;
        float chip_throughput_percent = chip_expected_hashrate_ghs > 0.0f
            ? result->hashrate_ghs[chip_id] * 100.0f / chip_expected_hashrate_ghs
            : 0.0f;
        if (chip_throughput_percent < min_chip_throughput_percent) {
            min_chip_throughput_percent = chip_throughput_percent;
        }
        expected_hashrate_ghs += chip_expected_hashrate_ghs;
        total_frequency_mhz += chip_frequency_mhz;
        ESP_LOGI(TAG,
            "Proto qualification %s chip%u@%.3fMHz: %.2f/%.2f GH/s throughput=%.2f%% pass/fail=%lu/%lu pass_rate=%.2f%%",
            profile, chip_id, chip_frequency_mhz, result->hashrate_ghs[chip_id],
            chip_expected_hashrate_ghs, chip_throughput_percent,
            (unsigned long)result->passed[chip_id],
            (unsigned long)result->failed[chip_id], pass_rate_percent);
    }

    float throughput_percent = expected_hashrate_ghs > 0.0f
        ? result->total_hashrate_ghs * 100.0f / expected_hashrate_ghs
        : 0.0f;

    int16_t regulator_voltage_mv = VCORE_get_regulator_voltage_mv(GLOBAL_STATE);
    float regulator_current_a = TPS546_get_iout();
    uint16_t lower_domain_mv = 0;
    uint16_t upper_domain_mv = 0;
    bool domains_valid = !GLOBAL_STATE->DEVICE_CONFIG.domain_voltage_sense;
    int domain_delta_mv = 0;
    if (GLOBAL_STATE->DEVICE_CONFIG.domain_voltage_sense && regulator_voltage_mv > 0 &&
        VCORE_get_domain_voltages_mv(GLOBAL_STATE, (uint16_t)regulator_voltage_mv,
            &lower_domain_mv, &upper_domain_mv) == ESP_OK) {
        domains_valid = true;
        domain_delta_mv = (int)upper_domain_mv - (int)lower_domain_mv;
    }
    int absolute_domain_delta_mv = domain_delta_mv < 0 ? -domain_delta_mv : domain_delta_mv;

    if (domains_valid && GLOBAL_STATE->DEVICE_CONFIG.domain_voltage_sense) {
        ESP_LOGI(TAG,
            "Proto qualification %s domains: lower=%u upper=%u delta=%+d mV total=%d mV TPS current=%.2f A",
            profile, lower_domain_mv, upper_domain_mv,
            domain_delta_mv, regulator_voltage_mv, regulator_current_a);
    } else if (!domains_valid) {
        ESP_LOGE(TAG,
            "Proto qualification %s: domain measurement unavailable; TPS current=%.2f A",
            profile, regulator_current_a);
    }

    if (proto_is_400_or_425_uniform_profile(result)) {
        log_proto_pvt_stack_voltages(profile, result->chip_count);
        for (uint8_t chip_id = 0; chip_id < result->chip_count; chip_id++) {
            if (!log_proto_internal_domain_summary(
                    profile, chip_id, result->passed[chip_id], NULL)) {
                ESP_LOGE(TAG,
                    "MC3 internal-domain diagnostic %s A%u unavailable",
                    profile, chip_id + 1);
            }
        }
    }

    bool accepted = counters_valid && domains_valid &&
        min_pass_rate_percent >= PROTO_MIN_PASS_RATE_PERCENT &&
        throughput_percent >= PROTO_MIN_THROUGHPUT_PERCENT &&
        min_chip_throughput_percent >= PROTO_MIN_CHIP_THROUGHPUT_PERCENT &&
        absolute_domain_delta_mv <= PROTO_MAX_DOMAIN_DELTA_MV;
    if (metrics_out != NULL) {
        *metrics_out = (proto_qualification_metrics_t) {
            .accepted = accepted,
            .domains_valid = domains_valid,
            .domain_delta_mv = domain_delta_mv,
            .absolute_domain_delta_mv = absolute_domain_delta_mv,
            .throughput_percent = throughput_percent,
            .min_chip_throughput_percent = min_chip_throughput_percent,
            .min_pass_rate_percent = min_pass_rate_percent,
            .average_frequency_mhz = result->chip_count > 0
                ? total_frequency_mhz / result->chip_count
                : 0.0f,
        };
    }
    ESP_LOGI(TAG,
        "Proto qualification %s: total=%.2f/%.2f GH/s throughput=%.2f%% min_chip=%.2f%% min_pass=%.2f%% balance=%d mV => %s",
        profile, result->total_hashrate_ghs, expected_hashrate_ghs,
        throughput_percent, min_chip_throughput_percent,
        min_pass_rate_percent, absolute_domain_delta_mv,
        accepted ? "PASS" : "REJECT");
    return accepted;
}

static bool proto_balance_candidate_is_better(
    const proto_qualification_metrics_t *candidate,
    const proto_qualification_metrics_t *best)
{
    if (candidate->absolute_domain_delta_mv +
            PROTO_BALANCE_SELECTION_DEADBAND_MV <
        best->absolute_domain_delta_mv) {
        return true;
    }
    if (best->absolute_domain_delta_mv +
            PROTO_BALANCE_SELECTION_DEADBAND_MV <
        candidate->absolute_domain_delta_mv) {
        return false;
    }

    if (candidate->min_chip_throughput_percent >
        best->min_chip_throughput_percent +
            PROTO_THROUGHPUT_SELECTION_DEADBAND_PERCENT) {
        return true;
    }
    if (best->min_chip_throughput_percent >
        candidate->min_chip_throughput_percent +
            PROTO_THROUGHPUT_SELECTION_DEADBAND_PERCENT) {
        return false;
    }

    if (candidate->throughput_percent > best->throughput_percent +
            PROTO_THROUGHPUT_SELECTION_DEADBAND_PERCENT) {
        return true;
    }
    if (best->throughput_percent > candidate->throughput_percent +
            PROTO_THROUGHPUT_SELECTION_DEADBAND_PERCENT) {
        return false;
    }

    return candidate->average_frequency_mhz > best->average_frequency_mhz;
}

static bool qualify_proto_frequency_step(GlobalState *GLOBAL_STATE, float frequency_mhz,
                                         mc3_qualification_result_t *result)
{
    if (!MC3_qualify_frequency(GLOBAL_STATE, frequency_mhz, result)) {
        ESP_LOGE(TAG, "Proto qualification communication failed at %.0f MHz", frequency_mhz);
        return false;
    }

    char profile[24];
    snprintf(profile, sizeof(profile), "%.3fMHz", result->frequency_mhz);
    return evaluate_proto_qualification(GLOBAL_STATE, profile, result, NULL);
}

static void run_proto_reject_diagnostic_hold(GlobalState *GLOBAL_STATE,
                                             uint16_t frequency_mhz)
{
    ESP_LOGW(TAG,
        "LOAD RELEASE: maintaining continuous work at rejected %u MHz for %u seconds before deliberately quiescing work",
        frequency_mhz, PROTO_REJECT_DIAGNOSTIC_LOADED_SECONDS);

    for (uint32_t second = 0;
         second < PROTO_REJECT_DIAGNOSTIC_LOADED_SECONDS;
         second++) {
        if ((second % PROTO_REJECT_DIAGNOSTIC_REFRESH_SECONDS) == 0) {
            if (MC3_refresh_qualification_work(GLOBAL_STATE)) {
                ESP_LOGW(TAG,
                    "LOAD RELEASE loaded t=%" PRIu32 "s: refreshed qualification work without changing PLLs",
                    second);
            } else {
                ESP_LOGE(TAG,
                    "LOAD RELEASE loaded t=%" PRIu32 "s: qualification work refresh failed",
                    second);
            }
        }

        int16_t regulator_voltage_mv =
            VCORE_get_regulator_voltage_mv(GLOBAL_STATE);
        float regulator_current_a = TPS546_get_iout();
        uint16_t lower_domain_mv = 0;
        uint16_t upper_domain_mv = 0;
        bool domains_valid = regulator_voltage_mv > 0 &&
            VCORE_get_domain_voltages_mv(GLOBAL_STATE,
                (uint16_t)regulator_voltage_mv,
                &lower_domain_mv, &upper_domain_mv) == ESP_OK;

        float voltages_mv[PROTO_MAX_VDD_READINGS] = {0};
        uint8_t voltage_count = MC3_read_vdd_voltages(
            voltages_mv, PROTO_MAX_VDD_READINGS);
        char readings[128] = "unavailable";
        if (voltage_count > 0) {
            readings[0] = '\0';
            size_t used = 0;
            for (uint8_t chip_id = 0;
                 chip_id < voltage_count && used < sizeof(readings);
                 chip_id++) {
                int written = snprintf(readings + used,
                    sizeof(readings) - used, "%sA%u:%.1f",
                    chip_id == 0 ? "" : " ", chip_id + 1,
                    voltages_mv[chip_id]);
                if (written < 0 ||
                    (size_t)written >= sizeof(readings) - used) {
                    break;
                }
                used += (size_t)written;
            }
        }

        if (domains_valid) {
            ESP_LOGW(TAG,
                "LOAD RELEASE loaded t=%" PRIu32 "s %uMHz: lower=%u upper=%u delta=%+d mV total=%d mV TPS current=%.2f A; MC3 VDD mV [%s]",
                second, frequency_mhz, lower_domain_mv, upper_domain_mv,
                (int)upper_domain_mv - (int)lower_domain_mv,
                regulator_voltage_mv, regulator_current_a, readings);
        } else {
            ESP_LOGW(TAG,
                "LOAD RELEASE loaded t=%" PRIu32 "s %uMHz: PCB domains unavailable total=%d mV TPS current=%.2f A; MC3 VDD mV [%s]",
                second, frequency_mhz, regulator_voltage_mv,
                regulator_current_a, readings);
        }

        if (domains_valid &&
            (lower_domain_mv >= PROTO_REJECT_DIAGNOSTIC_MAX_DOMAIN_MV ||
             upper_domain_mv >= PROTO_REJECT_DIAGNOSTIC_MAX_DOMAIN_MV)) {
            ESP_LOGE(TAG,
                "LOAD RELEASE: domain safety limit reached at loaded t=%" PRIu32 "s (lower=%u upper=%u mV); quiescing work immediately without changing PLLs",
                second, lower_domain_mv, upper_domain_mv);
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGW(TAG,
        "LOAD RELEASE: stopping PLL0 now at %u MHz without changing its divider configuration",
        frequency_mhz);
    if (!MC3_set_active_pll_running_preserve_config(GLOBAL_STATE, false)) {
        ESP_LOGE(TAG,
            "LOAD RELEASE: aborting observation because PLL0 could not be stopped cleanly at %u MHz",
            frequency_mhz);
        return;
    }

    TickType_t release_tick = xTaskGetTickCount();
    uint32_t elapsed_ms = 0;
    while (elapsed_ms <= PROTO_REJECT_DIAGNOSTIC_RELEASE_HOLD_MS) {
        int16_t regulator_voltage_mv =
            VCORE_get_regulator_voltage_mv(GLOBAL_STATE);
        float regulator_current_a = TPS546_get_iout();
        uint16_t lower_domain_mv = 0;
        uint16_t upper_domain_mv = 0;
        bool domains_valid = regulator_voltage_mv > 0 &&
            VCORE_get_domain_voltages_mv(GLOBAL_STATE,
                (uint16_t)regulator_voltage_mv,
                &lower_domain_mv, &upper_domain_mv) == ESP_OK;

        float voltages_mv[PROTO_MAX_VDD_READINGS] = {0};
        uint8_t voltage_count = MC3_read_vdd_voltages(
            voltages_mv, PROTO_MAX_VDD_READINGS);
        char readings[128] = "unavailable";
        if (voltage_count > 0) {
            readings[0] = '\0';
            size_t used = 0;
            for (uint8_t chip_id = 0;
                 chip_id < voltage_count && used < sizeof(readings);
                 chip_id++) {
                int written = snprintf(readings + used,
                    sizeof(readings) - used, "%sA%u:%.1f",
                    chip_id == 0 ? "" : " ", chip_id + 1,
                    voltages_mv[chip_id]);
                if (written < 0 ||
                    (size_t)written >= sizeof(readings) - used) {
                    break;
                }
                used += (size_t)written;
            }
        }

        if (domains_valid) {
            ESP_LOGW(TAG,
                "LOAD RELEASE idle t=%" PRIu32 "ms %uMHz: lower=%u upper=%u delta=%+d mV total=%d mV TPS current=%.2f A; MC3 VDD mV [%s]",
                elapsed_ms, frequency_mhz, lower_domain_mv, upper_domain_mv,
                (int)upper_domain_mv - (int)lower_domain_mv,
                regulator_voltage_mv, regulator_current_a, readings);
        } else {
            ESP_LOGW(TAG,
                "LOAD RELEASE idle t=%" PRIu32 "ms %uMHz: PCB domains unavailable total=%d mV TPS current=%.2f A; MC3 VDD mV [%s]",
                elapsed_ms, frequency_mhz, regulator_voltage_mv,
                regulator_current_a, readings);
        }

        if (domains_valid &&
            (lower_domain_mv >= PROTO_REJECT_DIAGNOSTIC_MAX_DOMAIN_MV ||
             upper_domain_mv >= PROTO_REJECT_DIAGNOSTIC_MAX_DOMAIN_MV)) {
            ESP_LOGE(TAG,
                "LOAD RELEASE: domain safety limit reached during PLL-off hold at t=%" PRIu32 "ms (lower=%u upper=%u mV); ending observation",
                elapsed_ms, lower_domain_mv, upper_domain_mv);
            break;
        }


        uint32_t sample_interval_ms =
            elapsed_ms < PROTO_REJECT_DIAGNOSTIC_FAST_SAMPLE_DURATION_MS
            ? PROTO_REJECT_DIAGNOSTIC_FAST_SAMPLE_MS
            : PROTO_REJECT_DIAGNOSTIC_SLOW_SAMPLE_MS;
        vTaskDelay(pdMS_TO_TICKS(sample_interval_ms));
        elapsed_ms = (uint32_t)((xTaskGetTickCount() - release_tick) *
            portTICK_PERIOD_MS);
    }

    ESP_LOGW(TAG,
        "LOAD RELEASE: restarting the unchanged PLL0 configuration before qualified rollback");
    if (!MC3_set_active_pll_running_preserve_config(GLOBAL_STATE, true)) {
        ESP_LOGE(TAG,
            "LOAD RELEASE: failed to restart PLL0 before rollback");
    }

    ESP_LOGW(TAG,
        "LOAD RELEASE observation complete at %u MHz; resuming qualified rollback",
        frequency_mhz);
}

static bool qualify_proto_mixed_frequency_step(GlobalState *GLOBAL_STATE,
                                               const float *frequencies_mhz,
                                               mc3_qualification_result_t *result,
                                               proto_qualification_metrics_t *metrics)
{
    if (!MC3_qualify_chip_frequencies(GLOBAL_STATE, frequencies_mhz,
            GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, result)) {
        ESP_LOGE(TAG,
            "Proto mixed-frequency qualification communication failed at chips=%.3f/%.3f/%.3f/%.3f MHz",
            frequencies_mhz[0], frequencies_mhz[1],
            frequencies_mhz[2], frequencies_mhz[3]);
        return false;
    }

    char profile[80];
    snprintf(profile, sizeof(profile), "chips=%.3f/%.3f/%.3f/%.3fMHz",
        frequencies_mhz[0], frequencies_mhz[1],
        frequencies_mhz[2], frequencies_mhz[3]);
    return evaluate_proto_qualification(GLOBAL_STATE, profile, result, metrics);
}

static bool log_proto_internal_domain_summary(
    const char *profile, uint8_t chip_id, uint32_t global_pass,
    mc3_core_domain_summary_t *summary_out)
{
    mc3_core_domain_summary_t summary = {0};
    if (!MC3_read_core_domain_summary(chip_id, &summary)) {
        ESP_LOGE(TAG,
            "MC3 internal-domain diagnostic %s chip%u: could not read per-core counters",
            profile, chip_id);
        return false;
    }

    double match_percent = global_pass > 0
        ? ((double)summary.core_pass_sum * 100.0) / (double)global_pass
        : 0.0;
    ESP_LOGW(TAG,
        "MC3 internal domains %s chip%u: D0=%" PRIu32 "(%.1f%% ideal,z=%u) D1=%" PRIu32 "(%.1f%% ideal,z=%u) D2=%" PRIu32 "(%.1f%% ideal,z=%u) D3=%" PRIu32 "(%.1f%% ideal,z=%u) sum=%" PRIu64 " global=%" PRIu32 " match=%.2f%% min_core=%u:%" PRIu32,
        profile, chip_id,
        summary.domain_pass[0],
        summary.core_pass_sum > 0
            ? (double)summary.domain_pass[0] * 400.0 /
                (double)summary.core_pass_sum
            : 0.0,
        summary.domain_zero_cores[0],
        summary.domain_pass[1],
        summary.core_pass_sum > 0
            ? (double)summary.domain_pass[1] * 400.0 /
                (double)summary.core_pass_sum
            : 0.0,
        summary.domain_zero_cores[1],
        summary.domain_pass[2],
        summary.core_pass_sum > 0
            ? (double)summary.domain_pass[2] * 400.0 /
                (double)summary.core_pass_sum
            : 0.0,
        summary.domain_zero_cores[2],
        summary.domain_pass[3],
        summary.core_pass_sum > 0
            ? (double)summary.domain_pass[3] * 400.0 /
                (double)summary.core_pass_sum
            : 0.0,
        summary.domain_zero_cores[3],
        summary.core_pass_sum, global_pass, match_percent,
        summary.minimum_core_id, summary.minimum_core_pass);

    ESP_LOGI(TAG,
        "MC3 internal slices %s chip%u: S0=%" PRIu32 " S1=%" PRIu32 " S2=%" PRIu32
        " S3=%" PRIu32 " S4=%" PRIu32 " S5=%" PRIu32
        " S6=%" PRIu32 " S7=%" PRIu32 " S8=%" PRIu32
        " S9=%" PRIu32 " S10=%" PRIu32 " S11=%" PRIu32,
        profile, chip_id,
        summary.slice_pass[0], summary.slice_pass[1],
        summary.slice_pass[2], summary.slice_pass[3],
        summary.slice_pass[4], summary.slice_pass[5],
        summary.slice_pass[6], summary.slice_pass[7],
        summary.slice_pass[8], summary.slice_pass[9],
        summary.slice_pass[10], summary.slice_pass[11]);

    if (summary_out != NULL) {
        *summary_out = summary;
    }
    return true;
}

static bool capture_proto_a1_internal_domain_profile(
    GlobalState *GLOBAL_STATE, const float *frequencies_mhz,
    mc3_qualification_result_t *result,
    proto_qualification_metrics_t *metrics,
    mc3_core_domain_summary_t *a1_summary)
{
    if (!MC3_qualify_chip_frequencies(GLOBAL_STATE, frequencies_mhz,
            GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, result)) {
        ESP_LOGE(TAG,
            "A1 internal-domain diagnostic communication failed at %.3f/%.3f/%.3f/%.3f MHz",
            frequencies_mhz[0], frequencies_mhz[1],
            frequencies_mhz[2], frequencies_mhz[3]);
        return false;
    }

    char profile[80];
    snprintf(profile, sizeof(profile), "A1diag=%.3f/%.3f/%.3f/%.3fMHz",
        frequencies_mhz[0], frequencies_mhz[1],
        frequencies_mhz[2], frequencies_mhz[3]);
    evaluate_proto_qualification(GLOBAL_STATE, profile, result, metrics);

    for (uint8_t chip_id = 0; chip_id < result->chip_count; chip_id++) {
        mc3_core_domain_summary_t *summary = chip_id == 0 ? a1_summary : NULL;
        if (!log_proto_internal_domain_summary(
                profile, chip_id, result->passed[chip_id], summary)) {
            return false;
        }
    }
    return true;
}

typedef struct {
    bool tested;
    bool first_accepted;
    bool final_accepted;
    uint8_t applications;
    float actual_frequency_mhz;
    proto_qualification_metrics_t first_metrics;
    proto_qualification_metrics_t final_metrics;
} proto_vf_matrix_result_t;

static bool qualify_proto_vf_matrix_application(
    GlobalState *GLOBAL_STATE, uint16_t voltage_mv, float frequency_mhz,
    uint8_t application, proto_qualification_metrics_t *metrics)
{
    float frequencies_mhz[MC3_QUALIFICATION_MAX_CHIPS] = {
        frequency_mhz,
        frequency_mhz,
        frequency_mhz,
        frequency_mhz,
    };
    mc3_qualification_result_t result = {0};
    if (!MC3_qualify_chip_frequencies(GLOBAL_STATE, frequencies_mhz,
            GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, &result)) {
        ESP_LOGE(TAG,
            "VF_MATRIX communication failure voltage=%u requested=%.3f application=%u",
            voltage_mv, frequency_mhz, application);
        return false;
    }

    char profile[80];
    snprintf(profile, sizeof(profile),
        "VF_MATRIX V=%umV F=%.3fMHz application=%u",
        voltage_mv, frequency_mhz, application);
    evaluate_proto_qualification(GLOBAL_STATE, profile, &result, metrics);
    for (uint8_t chip_id = 0; chip_id < result.chip_count; chip_id++) {
        if (!log_proto_internal_domain_summary(
                profile, chip_id, result.passed[chip_id], NULL)) {
            return false;
        }
    }
    metrics->average_frequency_mhz = result.frequency_mhz;
    return true;
}

static bool restore_proto_vf_matrix_baseline(GlobalState *GLOBAL_STATE,
                                              uint16_t voltage_mv)
{
    mc3_qualification_result_t result = {0};
    for (uint8_t application = 1;
         application <= PROTO_PLL_PROFILE_REAPPLY_ATTEMPTS + 1;
         application++) {
        ESP_LOGI(TAG,
            "VF_MATRIX baseline voltage=%u frequency=%.3f application=%u",
            voltage_mv, PROTO_VF_MATRIX_BASELINE_MHZ, application);
        result = (mc3_qualification_result_t) {0};
        if (qualify_proto_frequency_step(
                GLOBAL_STATE, PROTO_VF_MATRIX_BASELINE_MHZ, &result)) {
            return true;
        }
        ESP_LOGW(TAG,
            "VF_MATRIX baseline rejected at %u mV; reapplying 400 MHz (%u/%u)",
            voltage_mv, application, PROTO_PLL_PROFILE_REAPPLY_ATTEMPTS + 1);
    }

    ESP_LOGE(TAG,
        "VF_MATRIX could not restore the 400 MHz baseline at %u mV",
        voltage_mv);
    return false;
}

static bool run_proto_voltage_frequency_matrix(GlobalState *GLOBAL_STATE,
                                               uint16_t restore_voltage_mv)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.board_version == NULL ||
        strcmp(GLOBAL_STATE->DEVICE_CONFIG.board_version, "1103") != 0 ||
        GLOBAL_STATE->DEVICE_CONFIG.family.asic_count != 4 ||
        GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value <
            PROTO_INTERNAL_DOMAIN_DIAGNOSTIC_MIN_REQUEST_MHZ) {
        return true;
    }

    const size_t voltage_count = sizeof(PROTO_VF_MATRIX_VOLTAGES_MV) /
        sizeof(PROTO_VF_MATRIX_VOLTAGES_MV[0]);
    const size_t frequency_count = sizeof(PROTO_VF_MATRIX_FREQUENCIES_MHZ) /
        sizeof(PROTO_VF_MATRIX_FREQUENCIES_MHZ[0]);
    proto_vf_matrix_result_t matrix[
        sizeof(PROTO_VF_MATRIX_VOLTAGES_MV) /
            sizeof(PROTO_VF_MATRIX_VOLTAGES_MV[0])][
        sizeof(PROTO_VF_MATRIX_FREQUENCIES_MHZ) /
            sizeof(PROTO_VF_MATRIX_FREQUENCIES_MHZ[0])] = {0};
    uint16_t active_voltage_mv = restore_voltage_mv;

    ESP_LOGW(TAG,
        "VF_MATRIX BEGIN: 3 voltages x 5 frequencies; every cell starts from a qualified 400 MHz baseline");

    for (size_t voltage_index = 0; voltage_index < voltage_count;
         voltage_index++) {
        uint16_t voltage_mv = PROTO_VF_MATRIX_VOLTAGES_MV[voltage_index];
        if (!restore_proto_vf_matrix_baseline(
                GLOBAL_STATE, active_voltage_mv)) {
            return false;
        }
        ESP_LOGW(TAG, "VF_MATRIX setting Vcore to %u mV/domain", voltage_mv);
        if (set_proto_core_voltage(GLOBAL_STATE, voltage_mv) != ESP_OK) {
            ESP_LOGE(TAG, "VF_MATRIX failed to set Vcore to %u mV/domain",
                voltage_mv);
            return false;
        }
        active_voltage_mv = voltage_mv;
        vTaskDelay(pdMS_TO_TICKS(PROTO_VF_MATRIX_SETTLE_MS));
        log_proto_vdd_measurements(GLOBAL_STATE, voltage_mv);
        if (!restore_proto_vf_matrix_baseline(GLOBAL_STATE, voltage_mv)) {
            return false;
        }

        for (size_t frequency_index = 0;
             frequency_index < frequency_count; frequency_index++) {
            if (!restore_proto_vf_matrix_baseline(GLOBAL_STATE, voltage_mv)) {
                return false;
            }

            float frequency_mhz =
                PROTO_VF_MATRIX_FREQUENCIES_MHZ[frequency_index];
            proto_vf_matrix_result_t *cell =
                &matrix[voltage_index][frequency_index];
            for (uint8_t application = 1;
                 application <= PROTO_PLL_PROFILE_REAPPLY_ATTEMPTS + 1;
                 application++) {
                proto_qualification_metrics_t metrics = {0};
                if (!qualify_proto_vf_matrix_application(GLOBAL_STATE,
                        voltage_mv, frequency_mhz, application, &metrics)) {
                    return false;
                }

                cell->tested = true;
                cell->applications = application;
                cell->actual_frequency_mhz = metrics.average_frequency_mhz;
                cell->final_metrics = metrics;
                if (application == 1) {
                    cell->first_accepted = metrics.accepted;
                    cell->first_metrics = metrics;
                }
                cell->final_accepted = metrics.accepted;
                if (metrics.accepted) {
                    break;
                }
                ESP_LOGW(TAG,
                    "VF_MATRIX reapplying identical PLL profile voltage=%u frequency=%.3f (%u/%u)",
                    voltage_mv, frequency_mhz, application,
                    PROTO_PLL_PROFILE_REAPPLY_ATTEMPTS + 1);
            }

            ESP_LOGW(TAG,
                "VF_MATRIX CELL voltage=%u requested=%.3f actual=%.3f first=%s final=%s applications=%u first_delta=%+d first_min_chip=%.2f%% final_delta=%+d final_min_chip=%.2f%%",
                voltage_mv, frequency_mhz, cell->actual_frequency_mhz,
                cell->first_accepted ? "PASS" : "REJECT",
                cell->final_accepted ? "PASS" : "REJECT",
                cell->applications, cell->first_metrics.domain_delta_mv,
                cell->first_metrics.min_chip_throughput_percent,
                cell->final_metrics.domain_delta_mv,
                cell->final_metrics.min_chip_throughput_percent);
        }
    }

    ESP_LOGW(TAG, "VF_MATRIX SUMMARY BEGIN");
    for (size_t voltage_index = 0; voltage_index < voltage_count;
         voltage_index++) {
        for (size_t frequency_index = 0;
             frequency_index < frequency_count; frequency_index++) {
            const proto_vf_matrix_result_t *cell =
                &matrix[voltage_index][frequency_index];
            ESP_LOGW(TAG,
                "VF_MATRIX SUMMARY voltage=%u requested=%.3f actual=%.3f first=%s final=%s applications=%u first_delta=%+d first_min_chip=%.2f%% final_delta=%+d final_min_chip=%.2f%%",
                PROTO_VF_MATRIX_VOLTAGES_MV[voltage_index],
                PROTO_VF_MATRIX_FREQUENCIES_MHZ[frequency_index],
                cell->actual_frequency_mhz,
                cell->first_accepted ? "PASS" : "REJECT",
                cell->final_accepted ? "PASS" : "REJECT",
                cell->applications, cell->first_metrics.domain_delta_mv,
                cell->first_metrics.min_chip_throughput_percent,
                cell->final_metrics.domain_delta_mv,
                cell->final_metrics.min_chip_throughput_percent);
        }
    }
    ESP_LOGW(TAG, "VF_MATRIX SUMMARY END");

    if (!restore_proto_vf_matrix_baseline(GLOBAL_STATE, active_voltage_mv)) {
        return false;
    }
    if (active_voltage_mv != restore_voltage_mv) {
        ESP_LOGW(TAG,
            "VF_MATRIX restoring configured Vcore to %u mV/domain",
            restore_voltage_mv);
        if (set_proto_core_voltage(GLOBAL_STATE, restore_voltage_mv) != ESP_OK) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(PROTO_VF_MATRIX_SETTLE_MS));
        log_proto_vdd_measurements(GLOBAL_STATE, restore_voltage_mv);
    }
    return restore_proto_vf_matrix_baseline(
        GLOBAL_STATE, restore_voltage_mv);
}

static bool restore_proto_staggered_pll_baseline(GlobalState *GLOBAL_STATE)
{
    mc3_qualification_result_t result = {0};
    for (uint8_t application = 1;
         application <= PROTO_PLL_PROFILE_REAPPLY_ATTEMPTS + 1;
         application++) {
        ESP_LOGI(TAG,
            "STAGGER_PLL restoring %.3f MHz baseline application=%u",
            PROTO_STAGGERED_PLL_BASELINE_MHZ, application);
        result = (mc3_qualification_result_t) {0};
        if (qualify_proto_frequency_step(GLOBAL_STATE,
                PROTO_STAGGERED_PLL_BASELINE_MHZ, &result)) {
            return true;
        }
    }
    ESP_LOGE(TAG, "STAGGER_PLL could not restore its 400 MHz baseline");
    return false;
}

static void log_proto_staggered_pll_snapshot(GlobalState *GLOBAL_STATE,
                                             const char *order_name,
                                             float target_frequency_mhz,
                                             uint8_t chip_id,
                                             const char *phase)
{
    int16_t regulator_voltage_mv =
        VCORE_get_regulator_voltage_mv(GLOBAL_STATE);
    uint16_t lower_domain_mv = 0;
    uint16_t upper_domain_mv = 0;
    float regulator_current_a = TPS546_get_iout();
    if (regulator_voltage_mv > 0 &&
        VCORE_get_domain_voltages_mv(GLOBAL_STATE,
            (uint16_t)regulator_voltage_mv, &lower_domain_mv,
            &upper_domain_mv) == ESP_OK) {
        ESP_LOGW(TAG,
            "STAGGER_PLL SNAPSHOT order=%s target=%.3f chip=A%u phase=%s lower=%u upper=%u delta=%+d mV total=%d mV TPS_current=%.2f A",
            order_name, target_frequency_mhz, chip_id + 1, phase,
            lower_domain_mv, upper_domain_mv,
            (int)upper_domain_mv - (int)lower_domain_mv,
            regulator_voltage_mv, regulator_current_a);
    } else {
        ESP_LOGE(TAG,
            "STAGGER_PLL SNAPSHOT order=%s target=%.3f chip=A%u phase=%s domain measurement unavailable TPS_current=%.2f A",
            order_name, target_frequency_mhz, chip_id + 1, phase,
            regulator_current_a);
    }
}

static bool run_proto_staggered_pll_order(GlobalState *GLOBAL_STATE,
                                          const char *order_name,
                                          const uint8_t *chip_order,
                                          bool *order_accepted)
{
    *order_accepted = false;
    if (!restore_proto_staggered_pll_baseline(GLOBAL_STATE)) {
        return false;
    }

    float active_frequencies_mhz[MC3_QUALIFICATION_MAX_CHIPS] = {
        PROTO_STAGGERED_PLL_BASELINE_MHZ,
        PROTO_STAGGERED_PLL_BASELINE_MHZ,
        PROTO_STAGGERED_PLL_BASELINE_MHZ,
        PROTO_STAGGERED_PLL_BASELINE_MHZ,
    };

    ESP_LOGW(TAG, "STAGGER_PLL ORDER BEGIN %s", order_name);
    for (size_t frequency_index = 0;
         frequency_index < sizeof(PROTO_STAGGERED_PLL_FREQUENCIES_MHZ) /
             sizeof(PROTO_STAGGERED_PLL_FREQUENCIES_MHZ[0]);
         frequency_index++) {
        float target_frequency_mhz =
            PROTO_STAGGERED_PLL_FREQUENCIES_MHZ[frequency_index];
        ESP_LOGW(TAG,
            "STAGGER_PLL STEP order=%s target=%.3f MHz from profile %.3f/%.3f/%.3f/%.3f",
            order_name, target_frequency_mhz, active_frequencies_mhz[0],
            active_frequencies_mhz[1], active_frequencies_mhz[2],
            active_frequencies_mhz[3]);

        for (uint8_t order_index = 0; order_index < 4; order_index++) {
            uint8_t chip_id = chip_order[order_index];
            float actual_frequency_mhz = 0.0f;
            if (!MC3_set_chip_hash_frequency_live(GLOBAL_STATE, chip_id,
                    target_frequency_mhz, &actual_frequency_mhz)) {
                ESP_LOGE(TAG,
                    "STAGGER_PLL failed setting A%u to %.3f MHz in %s order",
                    chip_id + 1, target_frequency_mhz, order_name);
                return false;
            }
            active_frequencies_mhz[chip_id] = actual_frequency_mhz;
            log_proto_staggered_pll_snapshot(GLOBAL_STATE, order_name,
                actual_frequency_mhz, chip_id, "immediate");
            vTaskDelay(pdMS_TO_TICKS(PROTO_STAGGERED_PLL_SETTLE_MS));
            log_proto_staggered_pll_snapshot(GLOBAL_STATE, order_name,
                actual_frequency_mhz, chip_id, "settled-250ms");
        }

        mc3_qualification_result_t result = {0};
        if (!MC3_measure_active_frequency_profile(GLOBAL_STATE, &result)) {
            ESP_LOGE(TAG,
                "STAGGER_PLL could not measure active profile for %s at %.3f MHz",
                order_name, target_frequency_mhz);
            return false;
        }

        char profile[96];
        snprintf(profile, sizeof(profile),
            "STAGGER_PLL %s %.3fMHz", order_name,
            result.frequency_mhz);
        proto_qualification_metrics_t metrics = {0};
        bool accepted = evaluate_proto_qualification(GLOBAL_STATE, profile,
            &result, &metrics);
        for (uint8_t chip_id = 0; chip_id < result.chip_count; chip_id++) {
            if (!log_proto_internal_domain_summary(
                    profile, chip_id, result.passed[chip_id], NULL)) {
                return false;
            }
        }
        ESP_LOGW(TAG,
            "STAGGER_PLL RESULT order=%s frequency=%.3f accepted=%s delta=%+d mV throughput=%.2f%% min_chip=%.2f%% min_pass=%.2f%%",
            order_name, result.frequency_mhz,
            accepted ? "PASS" : "REJECT", metrics.domain_delta_mv,
            metrics.throughput_percent,
            metrics.min_chip_throughput_percent,
            metrics.min_pass_rate_percent);
        if (!accepted) {
            ESP_LOGE(TAG,
                "STAGGER_PLL ORDER STOP %s at %.3f MHz",
                order_name, result.frequency_mhz);
            return true;
        }
    }

    *order_accepted = true;
    ESP_LOGW(TAG, "STAGGER_PLL ORDER PASS %s through 450 MHz",
        order_name);
    return true;
}

static bool run_proto_staggered_pll_diagnostic(GlobalState *GLOBAL_STATE,
                                               bool *profile_activated)
{
    *profile_activated = false;
    if (GLOBAL_STATE->DEVICE_CONFIG.board_version == NULL ||
        strcmp(GLOBAL_STATE->DEVICE_CONFIG.board_version, "1103") != 0 ||
        GLOBAL_STATE->DEVICE_CONFIG.family.asic_count != 4 ||
        GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value <
            PROTO_STAGGERED_PLL_MIN_REQUEST_MHZ) {
        return true;
    }

    bool order_accepted[2] = {false, false};
    ESP_LOGW(TAG,
        "STAGGER_PLL DIAGNOSTIC BEGIN: live per-ASIC fine ramp at configured Vcore; comparing upper-first and lower-first orders");
    for (uint8_t order_index = 0; order_index < 2; order_index++) {
        if (!run_proto_staggered_pll_order(GLOBAL_STATE,
                PROTO_STAGGERED_PLL_ORDER_NAMES[order_index],
                PROTO_STAGGERED_PLL_ORDERS[order_index],
                &order_accepted[order_index])) {
            return false;
        }
    }

    ESP_LOGW(TAG,
        "STAGGER_PLL SUMMARY upper-first=%s lower-first=%s",
        order_accepted[0] ? "PASS" : "REJECT",
        order_accepted[1] ? "PASS" : "REJECT");

    if (!order_accepted[1] && order_accepted[0]) {
        ESP_LOGW(TAG,
            "STAGGER_PLL reactivating the passing upper-first profile");
        if (!run_proto_staggered_pll_order(GLOBAL_STATE,
                "upper-first-reactivation(A3,A4,A1,A2)",
                PROTO_STAGGERED_PLL_ORDERS[0], &order_accepted[0])) {
            return false;
        }
    }

    bool target_is_450_mhz =
        GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value <=
            PROTO_STAGGERED_PLL_FREQUENCIES_MHZ[
                sizeof(PROTO_STAGGERED_PLL_FREQUENCIES_MHZ) /
                    sizeof(PROTO_STAGGERED_PLL_FREQUENCIES_MHZ[0]) - 1] +
                0.001f;
    if ((order_accepted[1] || order_accepted[0]) && target_is_450_mhz) {
        float active_frequency_mhz =
            PROTO_STAGGERED_PLL_FREQUENCIES_MHZ[
                sizeof(PROTO_STAGGERED_PLL_FREQUENCIES_MHZ) /
                    sizeof(PROTO_STAGGERED_PLL_FREQUENCIES_MHZ[0]) - 1];
        GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency =
            active_frequency_mhz;
        GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate =
            active_frequency_mhz *
            GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count *
            GLOBAL_STATE->DEVICE_CONFIG.family.asic_count / 1000.0f;
        *profile_activated = true;
        ESP_LOGW(TAG,
            "STAGGER_PLL activated live-ramped 450 MHz profile without a global work restart");
        return true;
    }

    ESP_LOGW(TAG,
        "STAGGER_PLL found no passing 450 MHz order; restoring 400 MHz before the normal safe ramp");
    return restore_proto_staggered_pll_baseline(GLOBAL_STATE);
}

static bool run_proto_a1_internal_domain_diagnostic(GlobalState *GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.board_version == NULL ||
        strcmp(GLOBAL_STATE->DEVICE_CONFIG.board_version, "1103") != 0 ||
        GLOBAL_STATE->DEVICE_CONFIG.family.asic_count != 4 ||
        GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value <
            PROTO_INTERNAL_DOMAIN_DIAGNOSTIC_MIN_REQUEST_MHZ) {
        return true;
    }

    const float baseline_frequency_mhz = 422.2222f;
    float frequencies_mhz[MC3_QUALIFICATION_MAX_CHIPS] = {
        baseline_frequency_mhz,
        baseline_frequency_mhz,
        baseline_frequency_mhz,
        baseline_frequency_mhz,
    };
    bool collapse_captured = false;

    ESP_LOGW(TAG,
        "Starting A1 internal-domain diagnostic: hold A2-A4 at %.3f MHz and sweep A1 across the failure threshold",
        baseline_frequency_mhz);

    mc3_qualification_result_t result = {0};
    proto_qualification_metrics_t metrics = {0};
    mc3_core_domain_summary_t a1_summary = {0};
    if (!capture_proto_a1_internal_domain_profile(
            GLOBAL_STATE, frequencies_mhz, &result, &metrics, &a1_summary)) {
        return false;
    }

    for (size_t i = 0;
         i < sizeof(PROTO_A1_DOMAIN_DIAGNOSTIC_FREQUENCIES_MHZ) /
             sizeof(PROTO_A1_DOMAIN_DIAGNOSTIC_FREQUENCIES_MHZ[0]);
         i++) {
        frequencies_mhz[0] =
            PROTO_A1_DOMAIN_DIAGNOSTIC_FREQUENCIES_MHZ[i];
        result = (mc3_qualification_result_t) {0};
        metrics = (proto_qualification_metrics_t) {0};
        a1_summary = (mc3_core_domain_summary_t) {0};
        if (!capture_proto_a1_internal_domain_profile(
                GLOBAL_STATE, frequencies_mhz, &result, &metrics,
                &a1_summary)) {
            return false;
        }

        float expected_a1_hashrate_ghs = result.chip_frequency_mhz[0] *
            GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count / 1000.0f;
        float a1_throughput_percent = expected_a1_hashrate_ghs > 0.0f
            ? result.hashrate_ghs[0] * 100.0f / expected_a1_hashrate_ghs
            : 0.0f;
        if (a1_throughput_percent <
                PROTO_INTERNAL_DOMAIN_COLLAPSE_PERCENT) {
            uint8_t weakest_domain = 0;
            for (uint8_t domain_id = 1;
                 domain_id < MC3_INTERNAL_VOLTAGE_DOMAIN_COUNT;
                 domain_id++) {
                if (a1_summary.domain_pass[domain_id] <
                    a1_summary.domain_pass[weakest_domain]) {
                    weakest_domain = domain_id;
                }
            }
            ESP_LOGE(TAG,
                "A1 INTERNAL-DOMAIN COLLAPSE CAPTURED at %.3f MHz: chip throughput %.2f%%, weakest D%u=%" PRIu32 " passes with %u zero cores",
                result.chip_frequency_mhz[0], a1_throughput_percent,
                weakest_domain, a1_summary.domain_pass[weakest_domain],
                a1_summary.domain_zero_cores[weakest_domain]);
            collapse_captured = true;
            break;
        }
    }

    if (!collapse_captured) {
        ESP_LOGW(TAG,
            "A1 internal-domain diagnostic did not reproduce a <%.1f%% throughput collapse through %.3f MHz",
            PROTO_INTERNAL_DOMAIN_COLLAPSE_PERCENT,
            PROTO_A1_DOMAIN_DIAGNOSTIC_FREQUENCIES_MHZ[
                sizeof(PROTO_A1_DOMAIN_DIAGNOSTIC_FREQUENCIES_MHZ) /
                    sizeof(PROTO_A1_DOMAIN_DIAGNOSTIC_FREQUENCIES_MHZ[0]) - 1]);
    }

    ESP_LOGW(TAG,
        "Restoring %.3f MHz uniform baseline after A1 internal-domain diagnostic",
        baseline_frequency_mhz);
    for (uint8_t chip_id = 0; chip_id < MC3_QUALIFICATION_MAX_CHIPS;
         chip_id++) {
        frequencies_mhz[chip_id] = baseline_frequency_mhz;
    }
    result = (mc3_qualification_result_t) {0};
    return MC3_qualify_chip_frequencies(GLOBAL_STATE, frequencies_mhz,
        GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, &result);
}

static bool capture_proto_addressed_d2_profile(GlobalState *GLOBAL_STATE,
                                                const char *stage)
{
    mc3_qualification_result_t result = {0};
    if (!MC3_measure_active_frequency_profile(GLOBAL_STATE, &result)) {
        ESP_LOGE(TAG,
            "ADDRESSED_D2 could not measure stage=%s", stage);
        return false;
    }

    char profile[96];
    snprintf(profile, sizeof(profile),
        "ADDRESSED_D2 %s %.3f/%.3f/%.3f/%.3fMHz", stage,
        result.chip_frequency_mhz[0], result.chip_frequency_mhz[1],
        result.chip_frequency_mhz[2], result.chip_frequency_mhz[3]);

    proto_qualification_metrics_t metrics = {0};
    bool accepted = evaluate_proto_qualification(
        GLOBAL_STATE, profile, &result, &metrics);

    // Uniform 400/425 MHz profiles are logged by the common evaluator. Mixed
    // profiles need an explicit PVT and per-core snapshot here.
    if (!proto_is_400_or_425_uniform_profile(&result)) {
        log_proto_pvt_stack_voltages(profile, result.chip_count);
        for (uint8_t chip_id = 0; chip_id < result.chip_count; chip_id++) {
            if (!log_proto_internal_domain_summary(
                    profile, chip_id, result.passed[chip_id], NULL)) {
                return false;
            }
        }
    }

    ESP_LOGW(TAG,
        "ADDRESSED_D2 RESULT stage=%s profile=%.3f/%.3f/%.3f/%.3fMHz accepted=%s delta=%+d mV throughput=%.2f%% min_chip=%.2f%% TPS/domain data above",
        stage, result.chip_frequency_mhz[0],
        result.chip_frequency_mhz[1], result.chip_frequency_mhz[2],
        result.chip_frequency_mhz[3], accepted ? "PASS" : "REJECT",
        metrics.domain_delta_mv, metrics.throughput_percent,
        metrics.min_chip_throughput_percent);
    return true;
}

static bool set_proto_addressed_d2_chip_frequency(
    GlobalState *GLOBAL_STATE, uint8_t chip_id, float frequency_mhz)
{
    float actual_frequency_mhz = 0.0f;
    if (!MC3_set_chip_hash_frequency_live(GLOBAL_STATE, chip_id,
            frequency_mhz, &actual_frequency_mhz)) {
        ESP_LOGE(TAG,
            "ADDRESSED_D2 failed setting A%u to %.3f MHz",
            chip_id + 1, frequency_mhz);
        return false;
    }
    if (actual_frequency_mhz != frequency_mhz) {
        ESP_LOGE(TAG,
            "ADDRESSED_D2 A%u requested %.3f MHz but applied %.3f MHz",
            chip_id + 1, frequency_mhz, actual_frequency_mhz);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(PROTO_ADDRESSED_D2_SETTLE_MS));
    return true;
}

static bool run_proto_addressed_d2_diagnostic(GlobalState *GLOBAL_STATE)
{
    if (!PROTO_RUN_ADDRESSED_D2_DIAGNOSTIC ||
        GLOBAL_STATE->DEVICE_CONFIG.board_version == NULL ||
        strcmp(GLOBAL_STATE->DEVICE_CONFIG.board_version, "1103") != 0 ||
        GLOBAL_STATE->DEVICE_CONFIG.family.asic_count != 4 ||
        GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency <
            PROTO_ADDRESSED_D2_BASELINE_MHZ - 0.001f) {
        return true;
    }

    bool experiment_ok = true;
    ESP_LOGW(TAG,
        "ADDRESSED_D2 BEGIN: all ASICs %.3f MHz; test A1 alone at %.3f MHz, restore, then test A2 alone",
        PROTO_ADDRESSED_D2_BASELINE_MHZ,
        PROTO_ADDRESSED_D2_TEST_MHZ);

    experiment_ok = capture_proto_addressed_d2_profile(
        GLOBAL_STATE, "baseline-all-400") && experiment_ok;

    if (experiment_ok) {
        experiment_ok = set_proto_addressed_d2_chip_frequency(
            GLOBAL_STATE, 0, PROTO_ADDRESSED_D2_TEST_MHZ);
    }
    if (experiment_ok) {
        experiment_ok = capture_proto_addressed_d2_profile(
            GLOBAL_STATE, "A1-only-425");
    }

    // Always restore A1 before touching A2, even if its capture failed.
    bool restore_a1_ok = set_proto_addressed_d2_chip_frequency(
        GLOBAL_STATE, 0, PROTO_ADDRESSED_D2_BASELINE_MHZ);
    experiment_ok = restore_a1_ok && experiment_ok;

    if (experiment_ok) {
        experiment_ok = set_proto_addressed_d2_chip_frequency(
            GLOBAL_STATE, 1, PROTO_ADDRESSED_D2_TEST_MHZ);
    }
    if (experiment_ok) {
        experiment_ok = capture_proto_addressed_d2_profile(
            GLOBAL_STATE, "A2-only-425");
    }

    // Best-effort restoration touches both tested chips, guaranteeing the
    // final profile is all-400 even after a partial experiment failure.
    bool restore_a1_final_ok = set_proto_addressed_d2_chip_frequency(
        GLOBAL_STATE, 0, PROTO_ADDRESSED_D2_BASELINE_MHZ);
    bool restore_a2_final_ok = set_proto_addressed_d2_chip_frequency(
        GLOBAL_STATE, 1, PROTO_ADDRESSED_D2_BASELINE_MHZ);
    bool restored = restore_a1_final_ok && restore_a2_final_ok;
    experiment_ok = restored && experiment_ok;

    if (restored) {
        experiment_ok = capture_proto_addressed_d2_profile(
            GLOBAL_STATE, "final-all-400") && experiment_ok;
    }

    ESP_LOGW(TAG, "ADDRESSED_D2 END result=%s restored=%s",
        experiment_ok ? "COMPLETE" : "ERROR",
        restored ? "all-400" : "FAILED");
    return experiment_ok;
}

static bool capture_proto_clean_all_profile(GlobalState *GLOBAL_STATE,
    const char *stage, bool *domain_collapsed_out, bool *accepted_out)
{
    mc3_qualification_result_t result = {0};
    if (!MC3_measure_active_frequency_profile(GLOBAL_STATE, &result)) {
        ESP_LOGE(TAG, "CLEAN_ALL could not measure stage=%s", stage);
        return false;
    }

    char profile[112];
    snprintf(profile, sizeof(profile),
        "CLEAN_ALL %s %.3f/%.3f/%.3f/%.3fMHz", stage,
        result.chip_frequency_mhz[0], result.chip_frequency_mhz[1],
        result.chip_frequency_mhz[2], result.chip_frequency_mhz[3]);

    proto_qualification_metrics_t metrics = {0};
    bool accepted = evaluate_proto_qualification(
        GLOBAL_STATE, profile, &result, &metrics);
    bool domain_collapsed = false;

    bool common_evaluator_logged_details =
        proto_is_400_or_425_uniform_profile(&result);
    if (!common_evaluator_logged_details) {
        log_proto_pvt_stack_voltages(profile, result.chip_count);
    }
    if (domain_collapsed_out != NULL ||
        !common_evaluator_logged_details) {
        for (uint8_t chip_id = 0; chip_id < result.chip_count; chip_id++) {
            mc3_core_domain_summary_t summary = {0};
            bool summary_ok = common_evaluator_logged_details
                ? MC3_read_core_domain_summary(chip_id, &summary)
                : log_proto_internal_domain_summary(
                    profile, chip_id, result.passed[chip_id], &summary);
            if (!summary_ok) {
                return false;
            }
            for (uint8_t domain = 0;
                 domain < MC3_INTERNAL_VOLTAGE_DOMAIN_COUNT; domain++) {
                if (summary.domain_pass[domain] == 0 ||
                    summary.domain_zero_cores[domain] ==
                        MC3_CORES_PER_SLICE * 3) {
                    domain_collapsed = true;
                }
            }
        }
    }

    if (domain_collapsed_out != NULL) {
        *domain_collapsed_out = domain_collapsed;
    }
    if (accepted_out != NULL) {
        *accepted_out = accepted;
    }
    ESP_LOGW(TAG,
        "CLEAN_ALL RESULT stage=%s profile=%.3f/%.3f/%.3f/%.3fMHz accepted=%s collapsed_domain=%s delta=%+d mV throughput=%.2f%% min_chip=%.2f%%",
        stage, result.chip_frequency_mhz[0],
        result.chip_frequency_mhz[1], result.chip_frequency_mhz[2],
        result.chip_frequency_mhz[3], accepted ? "PASS" : "REJECT",
        domain_collapsed ? "YES" : "no", metrics.domain_delta_mv,
        metrics.throughput_percent, metrics.min_chip_throughput_percent);
    return true;
}

static float proto_pvt_d2_voltage_mv(
    const mc3_pvt_voltage_reading_t *reading)
{
    static const uint8_t d2_bottom_channels[] = {9, 10};
    static const uint8_t d2_top_channels[] = {11, 12};
    const uint16_t required_mask =
        (1U << 9) | (1U << 10) | (1U << 11) | (1U << 12);

    if (reading == NULL ||
        (reading->valid_channel_mask & required_mask) != required_mask) {
        return -1.0f;
    }

    return proto_pvt_average(reading, d2_top_channels,
               sizeof(d2_top_channels) / sizeof(d2_top_channels[0])) -
        proto_pvt_average(reading, d2_bottom_channels,
            sizeof(d2_bottom_channels) /
                sizeof(d2_bottom_channels[0]));
}

static bool run_proto_clean_all_recovery_timeline(GlobalState *GLOBAL_STATE)
{
    static const uint32_t sample_targets_ms[] = {
        0, 750, 1500, 2500, 4000, 6000, 8000,
        12000, 16000, 20000, 25000, 30000,
    };
    mc3_core_domain_summary_t previous[2] = {0};
    uint32_t previous_counter_ms = 0;
    const double work_per_pass = (double)(1ULL << 24);
    const double expected_d2_hashrate_hs =
        (double)PROTO_CLEAN_ALL_TEST_MHZ * 1000000.0 *
        (double)GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count /
        (double)MC3_INTERNAL_VOLTAGE_DOMAIN_COUNT;

    if (!MC3_start_active_spdlog_observation(
            PROTO_CLEAN_ALL_RECOVERY_OBSERVATION_MS)) {
        ESP_LOGE(TAG,
            "CLEAN_ALL RECOVERY could not arm the 30-second SPDLOG counter window");
        return false;
    }

    TickType_t start_tick = xTaskGetTickCount();
    ESP_LOGW(TAG,
        "CLEAN_ALL RECOVERY TIMELINE BEGIN: all PLLs fixed at %.3f MHz; no work or PLL updates after this counter arm",
        PROTO_CLEAN_ALL_TEST_MHZ);

    for (size_t sample_index = 0;
         sample_index < sizeof(sample_targets_ms) /
             sizeof(sample_targets_ms[0]);
         sample_index++) {
        uint32_t target_ms = sample_targets_ms[sample_index];
        uint32_t elapsed_ms = (uint32_t)(
            (xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS);
        if (elapsed_ms < target_ms) {
            vTaskDelay(pdMS_TO_TICKS(target_ms - elapsed_ms));
        }

        mc3_core_domain_summary_t current[2] = {0};
        if (!MC3_read_core_domain_summary(0, &current[0]) ||
            !MC3_read_core_domain_summary(1, &current[1])) {
            ESP_LOGE(TAG,
                "CLEAN_ALL RECOVERY could not read A1/A2 per-core counters at target t=%" PRIu32 "ms",
                target_ms);
            return false;
        }
        uint32_t counter_ms = (uint32_t)(
            (xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS);

        mc3_pvt_voltage_reading_t pvt[2] = {0};
        uint8_t pvt_count = MC3_read_pvt_voltages(pvt, 2);
        float d2_voltage_mv[2] = {-1.0f, -1.0f};
        if (pvt_count == 2) {
            d2_voltage_mv[0] = proto_pvt_d2_voltage_mv(&pvt[0]);
            d2_voltage_mv[1] = proto_pvt_d2_voltage_mv(&pvt[1]);
        }

        int16_t regulator_voltage_mv =
            VCORE_get_regulator_voltage_mv(GLOBAL_STATE);
        float regulator_current_a = TPS546_get_iout();
        uint16_t lower_domain_mv = 0;
        uint16_t upper_domain_mv = 0;
        bool domains_valid = regulator_voltage_mv > 0 &&
            VCORE_get_domain_voltages_mv(GLOBAL_STATE,
                (uint16_t)regulator_voltage_mv,
                &lower_domain_mv, &upper_domain_mv) == ESP_OK;

        double interval_percent[2] = {0.0, 0.0};
        double cumulative_percent[2] = {0.0, 0.0};
        uint32_t interval_ms = counter_ms - previous_counter_ms;
        for (uint8_t chip_id = 0; chip_id < 2; chip_id++) {
            uint32_t current_pass = current[chip_id].domain_pass[2];
            uint32_t previous_pass = previous[chip_id].domain_pass[2];
            uint32_t interval_pass = current_pass >= previous_pass
                ? current_pass - previous_pass
                : current_pass;
            if (interval_ms > 0 && expected_d2_hashrate_hs > 0.0) {
                interval_percent[chip_id] =
                    ((double)interval_pass * work_per_pass * 1000.0 /
                        (double)interval_ms) * 100.0 /
                    expected_d2_hashrate_hs;
            }
            if (counter_ms > 0 && expected_d2_hashrate_hs > 0.0) {
                cumulative_percent[chip_id] =
                    ((double)current_pass * work_per_pass * 1000.0 /
                        (double)counter_ms) * 100.0 /
                    expected_d2_hashrate_hs;
            }
        }

        if (domains_valid) {
            ESP_LOGW(TAG,
                "CLEAN_ALL RECOVERY t=%" PRIu32 "ms counter_t=%" PRIu32 "ms PCB=%u/%u(%+d)mV total=%d TPS=%.2fA | A1 D2=%.1fmV pass=%" PRIu32 " z=%u rate=%.1f%% cumulative=%.1f%% | A2 D2=%.1fmV pass=%" PRIu32 " z=%u rate=%.1f%% cumulative=%.1f%%",
                (uint32_t)((xTaskGetTickCount() - start_tick) *
                    portTICK_PERIOD_MS),
                counter_ms, lower_domain_mv, upper_domain_mv,
                (int)upper_domain_mv - (int)lower_domain_mv,
                regulator_voltage_mv, regulator_current_a,
                d2_voltage_mv[0], current[0].domain_pass[2],
                current[0].domain_zero_cores[2], interval_percent[0],
                cumulative_percent[0], d2_voltage_mv[1],
                current[1].domain_pass[2],
                current[1].domain_zero_cores[2], interval_percent[1],
                cumulative_percent[1]);
        } else {
            ESP_LOGW(TAG,
                "CLEAN_ALL RECOVERY t=%" PRIu32 "ms counter_t=%" PRIu32 "ms PCB=unavailable total=%d TPS=%.2fA | A1 D2=%.1fmV pass=%" PRIu32 " z=%u rate=%.1f%% cumulative=%.1f%% | A2 D2=%.1fmV pass=%" PRIu32 " z=%u rate=%.1f%% cumulative=%.1f%%",
                (uint32_t)((xTaskGetTickCount() - start_tick) *
                    portTICK_PERIOD_MS),
                counter_ms, regulator_voltage_mv, regulator_current_a,
                d2_voltage_mv[0], current[0].domain_pass[2],
                current[0].domain_zero_cores[2], interval_percent[0],
                cumulative_percent[0], d2_voltage_mv[1],
                current[1].domain_pass[2],
                current[1].domain_zero_cores[2], interval_percent[1],
                cumulative_percent[1]);
        }

        if (domains_valid &&
            (lower_domain_mv >= PROTO_CLEAN_ALL_RECOVERY_MAX_DOMAIN_MV ||
             upper_domain_mv >= PROTO_CLEAN_ALL_RECOVERY_MAX_DOMAIN_MV)) {
            ESP_LOGE(TAG,
                "CLEAN_ALL RECOVERY safety limit reached (lower=%u upper=%u mV); ending timeline",
                lower_domain_mv, upper_domain_mv);
            return false;
        }

        previous[0] = current[0];
        previous[1] = current[1];
        previous_counter_ms = counter_ms;
    }

    ESP_LOGW(TAG,
        "CLEAN_ALL RECOVERY TIMELINE END: fixed-PLL observation complete");
    return true;
}

static bool run_proto_clean_all_chips_diagnostic(GlobalState *GLOBAL_STATE)
{
    if (!PROTO_RUN_CLEAN_ALL_CHIPS_DIAGNOSTIC ||
        GLOBAL_STATE->DEVICE_CONFIG.board_version == NULL ||
        strcmp(GLOBAL_STATE->DEVICE_CONFIG.board_version, "1103") != 0 ||
        GLOBAL_STATE->DEVICE_CONFIG.family.asic_count != 4 ||
        GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency <
            PROTO_CLEAN_ALL_BASELINE_MHZ - 0.001f) {
        return true;
    }

    bool experiment_ok = true;
    bool all_domain_collapsed = false;
    bool all_accepted = false;
    ESP_LOGW(TAG,
        "CLEAN_ALL BEGIN: fresh all-%.3f work, set A1/A3/A2/A4 back-to-back to %.3f MHz, immediately reset/reapply identical work",
        PROTO_CLEAN_ALL_BASELINE_MHZ, PROTO_CLEAN_ALL_TEST_MHZ);

    experiment_ok = MC3_reapply_active_qualification_work() &&
        capture_proto_clean_all_profile(
            GLOBAL_STATE, "fresh-all-400", NULL, NULL);

    static const uint8_t update_order[] = {0, 2, 1, 3};
    float actual_mhz[MC3_QUALIFICATION_MAX_CHIPS] = {0};
    int64_t all_start_us = esp_timer_get_time();
    for (size_t i = 0;
         experiment_ok && i < sizeof(update_order) / sizeof(update_order[0]);
         i++) {
        uint8_t chip_id = update_order[i];
        experiment_ok = MC3_set_chip_hash_frequency_live(GLOBAL_STATE,
            chip_id, PROTO_CLEAN_ALL_TEST_MHZ, &actual_mhz[chip_id]);
        if (experiment_ok &&
            actual_mhz[chip_id] != PROTO_CLEAN_ALL_TEST_MHZ) {
            ESP_LOGE(TAG,
                "CLEAN_ALL requested A%u %.3f MHz but applied %.3f MHz",
                chip_id + 1, PROTO_CLEAN_ALL_TEST_MHZ,
                actual_mhz[chip_id]);
            experiment_ok = false;
        }
    }
    if (experiment_ok) {
        ESP_LOGW(TAG,
            "CLEAN_ALL A1/A3/A2/A4 PLL updates completed in %.3f ms; reapplying work now",
            (double)(esp_timer_get_time() - all_start_us) / 1000.0);
        experiment_ok = MC3_reapply_active_qualification_work();
    }
    if (experiment_ok) {
        experiment_ok = capture_proto_clean_all_profile(GLOBAL_STATE,
            "all-425-fresh-work", &all_domain_collapsed, &all_accepted);
    }

    if (experiment_ok && (!all_accepted || all_domain_collapsed)) {
        ESP_LOGW(TAG,
            "CLEAN_ALL RECOVERY: 425 MHz was rejected or collapsed; resetting/reapplying identical work once with every PLL unchanged");
        experiment_ok = MC3_reapply_active_qualification_work();
        if (experiment_ok) {
            experiment_ok =
                run_proto_clean_all_recovery_timeline(GLOBAL_STATE);
        }
    }

    float restore_frequencies_mhz[MC3_QUALIFICATION_MAX_CHIPS] = {
        PROTO_CLEAN_ALL_BASELINE_MHZ,
        PROTO_CLEAN_ALL_BASELINE_MHZ,
        PROTO_CLEAN_ALL_BASELINE_MHZ,
        PROTO_CLEAN_ALL_BASELINE_MHZ,
    };
    mc3_qualification_result_t restore_result = {0};
    bool restored = MC3_qualify_chip_frequencies(GLOBAL_STATE,
        restore_frequencies_mhz,
        GLOBAL_STATE->DEVICE_CONFIG.family.asic_count, &restore_result);
    if (restored) {
        restored = evaluate_proto_qualification(GLOBAL_STATE,
            "CLEAN_ALL restored-all-400", &restore_result, NULL);
    }

    ESP_LOGW(TAG,
        "CLEAN_ALL END result=%s all425_accepted=%s collapsed=%s restored=%s",
        experiment_ok ? "COMPLETE" : "ERROR",
        all_accepted ? "PASS" : "REJECT",
        all_domain_collapsed ? "YES" : "no",
        restored ? "all-400" : "FAILED");
    return experiment_ok && restored;
}

static bool select_proto_domain_frequency_balance(GlobalState *GLOBAL_STATE,
                                                  float last_good_frequency_mhz,
                                                  float rejected_frequency_mhz,
                                                  float *selected_frequencies_mhz,
                                                  float *selected_average_frequency_mhz)
{
    bool tested_candidate = false;
    bool found_accepted = false;
    size_t best_lower_frequency_index = 0;
    float best_frequencies_mhz[MC3_QUALIFICATION_MAX_CHIPS] = {0};
    proto_qualification_metrics_t best_metrics = {0};

    ESP_LOGW(TAG,
        "Sweeping lower-domain chips 0/1 below %.3f MHz while holding upper-domain chips 2/3 at %.3f MHz",
        rejected_frequency_mhz, rejected_frequency_mhz);

    for (size_t i = 0;
         i < sizeof(PROTO_FINE_BALANCE_FREQUENCIES_MHZ) /
             sizeof(PROTO_FINE_BALANCE_FREQUENCIES_MHZ[0]);
         i++) {
        float lower_frequency_mhz = PROTO_FINE_BALANCE_FREQUENCIES_MHZ[i];
        if (lower_frequency_mhz <= last_good_frequency_mhz ||
            lower_frequency_mhz >= rejected_frequency_mhz) {
            continue;
        }

        tested_candidate = true;
        float pair_skew_mhz[MC3_QUALIFICATION_MAX_CHIPS] = {
            lower_frequency_mhz,
            lower_frequency_mhz,
            rejected_frequency_mhz,
            rejected_frequency_mhz,
        };
        mc3_qualification_result_t mixed_result = {0};
        proto_qualification_metrics_t metrics = {0};
        bool accepted = qualify_proto_mixed_frequency_step(
            GLOBAL_STATE, pair_skew_mhz, &mixed_result, &metrics);

        if (accepted && (!found_accepted ||
                proto_balance_candidate_is_better(&metrics, &best_metrics))) {
            found_accepted = true;
            best_lower_frequency_index = i;
            for (uint8_t chip_id = 0; chip_id < MC3_QUALIFICATION_MAX_CHIPS;
                 chip_id++) {
                best_frequencies_mhz[chip_id] = pair_skew_mhz[chip_id];
            }
            best_metrics = metrics;
        }
    }

    size_t fine_frequency_count = sizeof(PROTO_FINE_BALANCE_FREQUENCIES_MHZ) /
        sizeof(PROTO_FINE_BALANCE_FREQUENCIES_MHZ[0]);
    if (found_accepted && best_lower_frequency_index + 1 < fine_frequency_count) {
        float refinement_frequency_mhz =
            PROTO_FINE_BALANCE_FREQUENCIES_MHZ[best_lower_frequency_index + 1];
        if (refinement_frequency_mhz < rejected_frequency_mhz) {
            float pair_best_lower_frequency_mhz = best_frequencies_mhz[0];
            ESP_LOGW(TAG,
                "Refining around the best pair result by raising one lower-domain chip at a time to %.3f MHz",
                refinement_frequency_mhz);
            for (uint8_t raised_chip = 0; raised_chip < 2; raised_chip++) {
                float refined_frequencies_mhz[MC3_QUALIFICATION_MAX_CHIPS] = {
                    pair_best_lower_frequency_mhz,
                    pair_best_lower_frequency_mhz,
                    rejected_frequency_mhz,
                    rejected_frequency_mhz,
                };
                refined_frequencies_mhz[raised_chip] = refinement_frequency_mhz;

                mc3_qualification_result_t refined_result = {0};
                proto_qualification_metrics_t refined_metrics = {0};
                bool accepted = qualify_proto_mixed_frequency_step(
                    GLOBAL_STATE, refined_frequencies_mhz, &refined_result,
                    &refined_metrics);
                if (accepted && proto_balance_candidate_is_better(
                        &refined_metrics, &best_metrics)) {
                    for (uint8_t chip_id = 0;
                         chip_id < MC3_QUALIFICATION_MAX_CHIPS; chip_id++) {
                        best_frequencies_mhz[chip_id] =
                            refined_frequencies_mhz[chip_id];
                    }
                    best_metrics = refined_metrics;
                }
            }
        }
    }

    if (!tested_candidate) {
        ESP_LOGW(TAG,
            "No fine PLL frequencies are available between %.3f and %.3f MHz",
            last_good_frequency_mhz, rejected_frequency_mhz);
        return false;
    }
    if (!found_accepted) {
        ESP_LOGW(TAG,
            "Fine domain-frequency sweep found no profile meeting all qualification limits");
        return false;
    }

    ESP_LOGW(TAG,
        "Best candidate domain-balance profile: chips=%.3f/%.3f/%.3f/%.3f MHz delta=%+d mV throughput=%.2f%% min_chip=%.2f%% min_pass=%.2f%%",
        best_frequencies_mhz[0], best_frequencies_mhz[1],
        best_frequencies_mhz[2], best_frequencies_mhz[3],
        best_metrics.domain_delta_mv, best_metrics.throughput_percent,
        best_metrics.min_chip_throughput_percent,
        best_metrics.min_pass_rate_percent);
    ESP_LOGW(TAG, "Requalifying the selected mixed-frequency mining profile");

    mc3_qualification_result_t selected_result = {0};
    proto_qualification_metrics_t selected_metrics = {0};
    if (!qualify_proto_mixed_frequency_step(GLOBAL_STATE,
            best_frequencies_mhz, &selected_result, &selected_metrics)) {
        ESP_LOGE(TAG, "Selected mixed-frequency profile failed requalification");
        return false;
    }

    *selected_average_frequency_mhz = selected_result.frequency_mhz;
    for (uint8_t chip_id = 0; chip_id < selected_result.chip_count; chip_id++) {
        selected_frequencies_mhz[chip_id] =
            selected_result.chip_frequency_mhz[chip_id];
    }
    ESP_LOGW(TAG,
        "Activated mixed-frequency mining profile at %.3f MHz average: delta=%+d mV throughput=%.2f%% min_chip=%.2f%% min_pass=%.2f%%",
        selected_result.frequency_mhz, selected_metrics.domain_delta_mv,
        selected_metrics.throughput_percent,
        selected_metrics.min_chip_throughput_percent,
        selected_metrics.min_pass_rate_percent);
    return true;
}

static bool ramp_proto_balanced_frequency_profile(
    GlobalState *GLOBAL_STATE,
    float *active_frequencies_mhz,
    float requested_frequency_mhz,
    float *active_average_frequency_mhz)
{
    const size_t frequency_count =
        sizeof(PROTO_POST_BALANCE_FREQUENCIES_MHZ) /
        sizeof(PROTO_POST_BALANCE_FREQUENCIES_MHZ[0]);
    size_t target_index = 0;
    while (target_index + 1 < frequency_count &&
           PROTO_POST_BALANCE_FREQUENCIES_MHZ[target_index + 1] <=
               requested_frequency_mhz + 0.001f) {
        target_index++;
    }
    if (PROTO_POST_BALANCE_FREQUENCIES_MHZ[0] >
        requested_frequency_mhz + 0.001f) {
        return true;
    }

    float highest_start_frequency_mhz = active_frequencies_mhz[0];
    for (uint8_t chip_id = 1;
         chip_id < GLOBAL_STATE->DEVICE_CONFIG.family.asic_count; chip_id++) {
        if (active_frequencies_mhz[chip_id] > highest_start_frequency_mhz) {
            highest_start_frequency_mhz = active_frequencies_mhz[chip_id];
        }
    }

    uint8_t lag_steps[MC3_QUALIFICATION_MAX_CHIPS] = {0};
    uint8_t maximum_lag_steps = 0;
    for (uint8_t chip_id = 0;
         chip_id < GLOBAL_STATE->DEVICE_CONFIG.family.asic_count; chip_id++) {
        float lag_mhz = highest_start_frequency_mhz -
            active_frequencies_mhz[chip_id];
        lag_steps[chip_id] = lag_mhz > 0.0f
            ? (uint8_t)((lag_mhz / 2.77778f) + 0.5f)
            : 0;
        if (lag_steps[chip_id] > maximum_lag_steps) {
            maximum_lag_steps = lag_steps[chip_id];
        }
    }

    ESP_LOGW(TAG,
        "Continuing addressed Proto ramp toward %.3f MHz while preserving up to %u fine-step(s) of measured chip skew",
        requested_frequency_mhz, maximum_lag_steps);

    uint8_t consecutive_rejections = 0;
    for (size_t base_index = 0; base_index <= target_index; base_index++) {
        float candidate_frequencies_mhz[MC3_QUALIFICATION_MAX_CHIPS] = {0};
        bool profile_changed = false;
        bool all_at_target = true;
        for (uint8_t chip_id = 0;
             chip_id < GLOBAL_STATE->DEVICE_CONFIG.family.asic_count; chip_id++) {
            size_t lead_steps = maximum_lag_steps - lag_steps[chip_id];
            size_t candidate_index = base_index + lead_steps;
            if (candidate_index > target_index) {
                candidate_index = target_index;
            }
            candidate_frequencies_mhz[chip_id] =
                PROTO_POST_BALANCE_FREQUENCIES_MHZ[candidate_index];
            profile_changed = profile_changed ||
                candidate_frequencies_mhz[chip_id] !=
                    active_frequencies_mhz[chip_id];
            all_at_target = all_at_target && candidate_index == target_index;
        }
        if (!profile_changed) {
            if (all_at_target) {
                break;
            }
            continue;
        }

        mc3_qualification_result_t result = {0};
        proto_qualification_metrics_t metrics = {0};
        bool candidate_accepted = qualify_proto_mixed_frequency_step(
            GLOBAL_STATE, candidate_frequencies_mhz, &result, &metrics);
        for (uint8_t reapply_attempt = 1;
             !candidate_accepted &&
                 reapply_attempt <= PROTO_PLL_PROFILE_REAPPLY_ATTEMPTS;
             reapply_attempt++) {
            ESP_LOGW(TAG,
                "Reapplying rejected %.3f MHz addressed profile to clear a possible incomplete PLL transition (%u/%u)",
                result.frequency_mhz, reapply_attempt,
                PROTO_PLL_PROFILE_REAPPLY_ATTEMPTS);
            result = (mc3_qualification_result_t) {0};
            metrics = (proto_qualification_metrics_t) {0};
            candidate_accepted = qualify_proto_mixed_frequency_step(
                GLOBAL_STATE, candidate_frequencies_mhz, &result, &metrics);
        }
        if (!candidate_accepted) {
            consecutive_rejections++;
            if (consecutive_rejections <
                    PROTO_POST_BALANCE_MAX_CONSECUTIVE_REJECTIONS &&
                base_index < target_index) {
                ESP_LOGW(TAG,
                    "Addressed post-balance profile at %.3f MHz average rejected; trying the next PLL tuple (%u/%u consecutive rejects)",
                    result.frequency_mhz, consecutive_rejections,
                    PROTO_POST_BALANCE_MAX_CONSECUTIVE_REJECTIONS);
                continue;
            }
            ESP_LOGW(TAG,
                "Addressed post-balance ramp stopped after %u consecutive rejects at %.3f MHz average; restoring last-qualified profile",
                consecutive_rejections, result.frequency_mhz);

            float weakest_throughput_percent = 100.0f;
            uint8_t weakest_chip = 0;
            for (uint8_t chip_id = 0; chip_id < result.chip_count; chip_id++) {
                float expected_hashrate_ghs =
                    result.chip_frequency_mhz[chip_id] *
                    GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count /
                    1000.0f;
                float chip_throughput_percent = expected_hashrate_ghs > 0.0f
                    ? result.hashrate_ghs[chip_id] * 100.0f /
                        expected_hashrate_ghs
                    : 0.0f;
                if (chip_throughput_percent < weakest_throughput_percent) {
                    weakest_throughput_percent = chip_throughput_percent;
                    weakest_chip = chip_id;
                }
            }

            if (result.chip_count == 4 &&
                weakest_throughput_percent <
                    PROTO_MIN_CHIP_THROUGHPUT_PERCENT) {
                uint8_t partner_chip = weakest_chip ^ 1U;
                size_t weak_index = 0;
                float best_error_mhz = 10000.0f;
                for (size_t i = 0; i < frequency_count; i++) {
                    float error_mhz =
                        PROTO_POST_BALANCE_FREQUENCIES_MHZ[i] -
                        active_frequencies_mhz[weakest_chip];
                    if (error_mhz < 0.0f) {
                        error_mhz = -error_mhz;
                    }
                    if (error_mhz < best_error_mhz) {
                        best_error_mhz = error_mhz;
                        weak_index = i;
                    }
                }

                size_t partner_index = weak_index + 3;
                if (partner_index > target_index) {
                    partner_index = target_index;
                }
                size_t bridge_index = weak_index + 1;
                if (bridge_index > target_index) {
                    bridge_index = target_index;
                }

                if (bridge_index > weak_index &&
                    partner_index > bridge_index) {
                    float prebias_frequencies_mhz[
                        MC3_QUALIFICATION_MAX_CHIPS] = {0};
                    for (uint8_t chip_id = 0; chip_id < result.chip_count;
                         chip_id++) {
                        prebias_frequencies_mhz[chip_id] =
                            active_frequencies_mhz[chip_id];
                    }
                    prebias_frequencies_mhz[partner_chip] =
                        PROTO_POST_BALANCE_FREQUENCIES_MHZ[partner_index];

                    ESP_LOGW(TAG,
                        "Trying weak-chip bridge: pinning chip%u at %.3f MHz and preloading same-domain chip%u at %.3f MHz",
                        weakest_chip,
                        active_frequencies_mhz[weakest_chip],
                        partner_chip,
                        prebias_frequencies_mhz[partner_chip]);

                    mc3_qualification_result_t prebias_result = {0};
                    proto_qualification_metrics_t prebias_metrics = {0};
                    if (qualify_proto_mixed_frequency_step(GLOBAL_STATE,
                            prebias_frequencies_mhz, &prebias_result,
                            &prebias_metrics)) {
                        float bridge_frequencies_mhz[
                            MC3_QUALIFICATION_MAX_CHIPS] = {0};
                        for (uint8_t chip_id = 0;
                             chip_id < result.chip_count; chip_id++) {
                            bridge_frequencies_mhz[chip_id] =
                                prebias_frequencies_mhz[chip_id];
                        }
                        bridge_frequencies_mhz[weakest_chip] =
                            PROTO_POST_BALANCE_FREQUENCIES_MHZ[bridge_index];

                        ESP_LOGW(TAG,
                            "Retrying weak chip%u at %.3f MHz with chip%u preloaded",
                            weakest_chip,
                            bridge_frequencies_mhz[weakest_chip],
                            partner_chip);

                        mc3_qualification_result_t bridge_result = {0};
                        proto_qualification_metrics_t bridge_metrics = {0};
                        if (qualify_proto_mixed_frequency_step(GLOBAL_STATE,
                                bridge_frequencies_mhz, &bridge_result,
                                &bridge_metrics)) {
                            for (uint8_t chip_id = 0;
                                 chip_id < bridge_result.chip_count;
                                 chip_id++) {
                                active_frequencies_mhz[chip_id] =
                                    bridge_result.chip_frequency_mhz[chip_id];
                            }
                            *active_average_frequency_mhz =
                                bridge_result.frequency_mhz;
                            ESP_LOGW(TAG,
                                "Weak-chip bridge passed; continuing with chip%u held two PLL steps ahead",
                                partner_chip);

                            for (size_t recovery_index = bridge_index + 1;
                                 recovery_index <= target_index;
                                 recovery_index++) {
                                float recovery_frequencies_mhz[
                                    MC3_QUALIFICATION_MAX_CHIPS] = {0};
                                for (uint8_t chip_id = 0;
                                     chip_id < bridge_result.chip_count;
                                     chip_id++) {
                                    recovery_frequencies_mhz[chip_id] =
                                        PROTO_POST_BALANCE_FREQUENCIES_MHZ[
                                            recovery_index];
                                }
                                size_t recovery_partner_index =
                                    recovery_index + 2;
                                if (recovery_partner_index > target_index) {
                                    recovery_partner_index = target_index;
                                }
                                recovery_frequencies_mhz[partner_chip] =
                                    PROTO_POST_BALANCE_FREQUENCIES_MHZ[
                                        recovery_partner_index];

                                mc3_qualification_result_t recovery_result = {0};
                                proto_qualification_metrics_t recovery_metrics = {0};
                                if (!qualify_proto_mixed_frequency_step(
                                        GLOBAL_STATE,
                                        recovery_frequencies_mhz,
                                        &recovery_result,
                                        &recovery_metrics)) {
                                    ESP_LOGW(TAG,
                                        "Weak-chip recovery ramp rejected %.3f MHz average; restoring last-qualified profile",
                                        recovery_result.frequency_mhz);
                                    break;
                                }
                                for (uint8_t chip_id = 0;
                                     chip_id < recovery_result.chip_count;
                                     chip_id++) {
                                    active_frequencies_mhz[chip_id] =
                                        recovery_result
                                            .chip_frequency_mhz[chip_id];
                                }
                                *active_average_frequency_mhz =
                                    recovery_result.frequency_mhz;
                                ESP_LOGI(TAG,
                                    "Weak-chip recovery ramp qualified %.3f MHz average (balance=%+d mV, throughput=%.2f%%, min_chip=%.2f%%)",
                                    recovery_result.frequency_mhz,
                                    recovery_metrics.domain_delta_mv,
                                    recovery_metrics.throughput_percent,
                                    recovery_metrics
                                        .min_chip_throughput_percent);
                            }

                            mc3_qualification_result_t final_result = {0};
                            proto_qualification_metrics_t final_metrics = {0};
                            if (!qualify_proto_mixed_frequency_step(
                                    GLOBAL_STATE, active_frequencies_mhz,
                                    &final_result, &final_metrics)) {
                                ESP_LOGE(TAG,
                                    "Failed to restore the last-qualified weak-chip recovery profile");
                                return false;
                            }
                            *active_average_frequency_mhz =
                                final_result.frequency_mhz;
                            return true;
                        }
                    }

                    ESP_LOGW(TAG,
                        "Weak-chip bridge did not recover chip%u; restoring the original last-qualified profile",
                        weakest_chip);
                }
            }

            mc3_qualification_result_t rollback_result = {0};
            proto_qualification_metrics_t rollback_metrics = {0};
            if (!qualify_proto_mixed_frequency_step(GLOBAL_STATE,
                    active_frequencies_mhz, &rollback_result,
                    &rollback_metrics)) {
                ESP_LOGE(TAG,
                    "Failed to restore the last-qualified addressed profile");
                return false;
            }
            *active_average_frequency_mhz = rollback_result.frequency_mhz;
            return true;
        }

        consecutive_rejections = 0;
        for (uint8_t chip_id = 0; chip_id < result.chip_count; chip_id++) {
            active_frequencies_mhz[chip_id] =
                result.chip_frequency_mhz[chip_id];
        }
        *active_average_frequency_mhz = result.frequency_mhz;
        ESP_LOGI(TAG,
            "Addressed post-balance ramp qualified %.3f MHz average (balance=%+d mV, throughput=%.2f%%, min_chip=%.2f%%)",
            result.frequency_mhz, metrics.domain_delta_mv,
            metrics.throughput_percent,
            metrics.min_chip_throughput_percent);
        if (all_at_target) {
            ESP_LOGI(TAG,
                "Addressed Proto ramp reached %.3f MHz on every chip",
                PROTO_POST_BALANCE_FREQUENCIES_MHZ[target_index]);
            break;
        }
    }

    return true;
}

static bool qualify_proto_frequency_ramp(GlobalState *GLOBAL_STATE)
{
    float requested_frequency_mhz = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value;
    uint16_t target_frequency_mhz = (uint16_t)(requested_frequency_mhz + 12.5f);
    target_frequency_mhz = (target_frequency_mhz / PROTO_FREQUENCY_STEP_MHZ) *
        PROTO_FREQUENCY_STEP_MHZ;
    if (target_frequency_mhz < MC3_STARTUP_FREQUENCY_MHZ) {
        target_frequency_mhz = MC3_STARTUP_FREQUENCY_MHZ;
    } else if (target_frequency_mhz > 1000) {
        target_frequency_mhz = 1000;
    }

    if (PROTO_RUN_425_REJECT_DIAGNOSTIC_HOLD &&
        !proto_load_release_diagnostic_ran_this_boot &&
        target_frequency_mhz < 425) {
        ESP_LOGW(TAG,
            "LOAD RELEASE diagnostic overrides the configured %.0f MHz target and ramps to 425 MHz",
            requested_frequency_mhz);
        target_frequency_mhz = 425;
    }

    ESP_LOGI(TAG,
        "Starting Proto qualification ramp from %u to %u MHz in %u MHz steps",
        MC3_STARTUP_FREQUENCY_MHZ, target_frequency_mhz, PROTO_FREQUENCY_STEP_MHZ);

    uint16_t last_good_frequency_mhz = 0;
    for (uint16_t frequency_mhz = MC3_STARTUP_FREQUENCY_MHZ;
         frequency_mhz <= target_frequency_mhz;
         frequency_mhz += PROTO_FREQUENCY_STEP_MHZ) {
        mc3_qualification_result_t result = {0};
        bool qualified = qualify_proto_frequency_step(
            GLOBAL_STATE, frequency_mhz, &result);
        bool diagnostic_completed = false;
        if (PROTO_RUN_425_REJECT_DIAGNOSTIC_HOLD &&
            !proto_load_release_diagnostic_ran_this_boot &&
            PROTO_FORCE_BROADCAST_PLL_UPDATES &&
            frequency_mhz == 425 && result.chip_count > 0) {
            proto_load_release_diagnostic_ran_this_boot = true;
            run_proto_reject_diagnostic_hold(GLOBAL_STATE, frequency_mhz);
            diagnostic_completed = true;
        }

        if (!qualified || diagnostic_completed) {
            if (last_good_frequency_mhz == 0) {
                ESP_LOGE(TAG, "Proto failed its baseline %u MHz qualification",
                    MC3_STARTUP_FREQUENCY_MHZ);
                return false;
            }

            if (!diagnostic_completed &&
                !PROTO_FORCE_BROADCAST_PLL_UPDATES &&
                frequency_mhz >= 425 &&
                GLOBAL_STATE->DEVICE_CONFIG.family.asic_count == 4) {
                float balanced_frequencies_mhz[MC3_QUALIFICATION_MAX_CHIPS] = {0};
                float balanced_average_frequency_mhz = 0.0f;
                if (select_proto_domain_frequency_balance(GLOBAL_STATE,
                        last_good_frequency_mhz, frequency_mhz,
                        balanced_frequencies_mhz,
                        &balanced_average_frequency_mhz)) {
                    if (!ramp_proto_balanced_frequency_profile(GLOBAL_STATE,
                            balanced_frequencies_mhz,
                            requested_frequency_mhz,
                            &balanced_average_frequency_mhz)) {
                        return false;
                    }
                    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency =
                        balanced_average_frequency_mhz;
                    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate =
                        balanced_average_frequency_mhz *
                        GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count *
                        GLOBAL_STATE->DEVICE_CONFIG.family.asic_count / 1000.0f;
                    ESP_LOGW(TAG,
                        "Activated balanced Proto operating point %.3f MHz average for requested %.0f MHz",
                        balanced_average_frequency_mhz,
                        requested_frequency_mhz);
                    return true;
                }
                ESP_LOGW(TAG,
                    "No mixed-frequency profile survived final qualification; restoring uniform safe frequency");
            }

            if (diagnostic_completed) {
                ESP_LOGW(TAG,
                    "LOAD RELEASE diagnostic complete at %u MHz; restoring last-qualified %u MHz",
                    frequency_mhz, last_good_frequency_mhz);
            } else {
                ESP_LOGW(TAG,
                    "Proto rejected %u MHz; rolling back to last-qualified %u MHz",
                    frequency_mhz, last_good_frequency_mhz);
            }
            mc3_qualification_result_t rollback_result = {0};
            if (!qualify_proto_frequency_step(
                    GLOBAL_STATE, last_good_frequency_mhz, &rollback_result)) {
                ESP_LOGE(TAG, "Proto rollback qualification failed at %u MHz",
                    last_good_frequency_mhz);
                return false;
            }

            GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value = last_good_frequency_mhz;
            GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate =
                last_good_frequency_mhz *
                GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count *
                GLOBAL_STATE->DEVICE_CONFIG.family.asic_count / 1000.0f;
            nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, last_good_frequency_mhz);
            ESP_LOGW(TAG,
                "Persisted safe Proto frequency %u MHz instead of requested %.0f MHz",
                last_good_frequency_mhz, requested_frequency_mhz);
            return true;
        }
        last_good_frequency_mhz = (uint16_t)(result.frequency_mhz + 0.5f);
    }

    ESP_LOGI(TAG, "Proto qualified through %u MHz", last_good_frequency_mhz);
    return true;
}

uint8_t asic_initialize(GlobalState *GLOBAL_STATE, asic_init_mode_t mode, uint32_t stabilization_delay_ms)
{
    const char *mode_str = (mode == ASIC_INIT_COLD_BOOT) ? "cold boot" : "recovery";
    ESP_LOGI(TAG, "Starting ASIC initialization (%s mode)", mode_str);

    if (asic_hold_reset_low() != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "ASIC reset hold failed";
        ESP_LOGE(TAG, "Failed to hold ASIC reset low");
        return 0;
    }

    // Check actual UART state for safety
    bool uart_initialized = SERIAL_is_initialized();
    
    // Verify mode matches actual state
    if (mode == ASIC_INIT_COLD_BOOT && uart_initialized) {
        ESP_LOGW(TAG, "Cold boot mode but UART already initialized - will reset baud only");
    } else if (mode == ASIC_INIT_RECOVERY && !uart_initialized) {
        ESP_LOGW(TAG, "Recovery mode but UART not initialized - will do full init");
    }
    
    // Use actual state for decision, not just mode
    if (!uart_initialized) {
        // Fresh boot - full UART initialization
        ESP_LOGI(TAG, "Performing full UART initialization");
        SERIAL_init();
    } else {
        // Live recovery - ASIC was reset, UART needs baud reset to 115200
        // This preserves the running system and avoids reboot
        ESP_LOGI(TAG, "UART already initialized, resetting baud to %d", UART_FREQ);
        SERIAL_set_baud(UART_FREQ);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    bool is_proto = GLOBAL_STATE->DEVICE_CONFIG.family.id == PROTO;
    bool is_proto_mc3 = is_proto &&
        GLOBAL_STATE->DEVICE_CONFIG.family.asic.id == MC3;
    if (is_proto_mc3) {
        MC3_set_ping_pong_pll_experiment_enabled(
            !PROTO_FORCE_BROADCAST_PLL_UPDATES &&
            PROTO_RUN_PING_PONG_PLL_EXPERIMENT != 0);
    }
    uint16_t target_core_voltage_mv = is_proto
        ? get_proto_target_core_voltage_mv(GLOBAL_STATE)
        : 0;
    uint16_t init_core_voltage_mv = is_proto_mc3
        ? (target_core_voltage_mv < PROTO_VCORE_STARTUP_MV
            ? target_core_voltage_mv
            : PROTO_VCORE_STARTUP_MV)
        : target_core_voltage_mv;

    if (enable_proto_power(GLOBAL_STATE, init_core_voltage_mv) != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "Proto power sequence failed";
        return 0;
    }

    if (asic_reset() != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "ASIC reset failed";
        ESP_LOGE(TAG, "ASIC reset failed!");
        return 0;
    }

    uint8_t chip_count = 0;
    while (true) {
        if (is_proto_mc3) {
            ESP_LOGI(TAG, "Detecting ASIC chips at %u mV/domain...", init_core_voltage_mv);
        } else {
            ESP_LOGI(TAG, "Detecting ASIC chips...");
        }
        clear_asic_chain_error();
        chip_count = ASIC_init(GLOBAL_STATE);

        if (chip_count > 0 || !is_proto_mc3) {
            break;
        }

        uint16_t fallback_max_mv = target_core_voltage_mv < PROTO_VCORE_INIT_FALLBACK_MAX_MV
            ? target_core_voltage_mv
            : PROTO_VCORE_INIT_FALLBACK_MAX_MV;
        if (init_core_voltage_mv >= fallback_max_mv) {
            break;
        }

        uint16_t remaining_mv = fallback_max_mv - init_core_voltage_mv;
        init_core_voltage_mv += remaining_mv < PROTO_VCORE_INIT_FALLBACK_STEP_MV
            ? remaining_mv
            : PROTO_VCORE_INIT_FALLBACK_STEP_MV;

        ESP_LOGW(TAG, "MC3 chain did not initialize; retrying at %u mV/domain",
            init_core_voltage_mv);
        if (asic_hold_reset_low() != ESP_OK ||
            set_proto_core_voltage(GLOBAL_STATE, init_core_voltage_mv) != ESP_OK) {
            GLOBAL_STATE->SYSTEM_MODULE.asic_status = "Proto Vcore fallback failed";
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(PROTO_VCORE_STABILIZATION_MS));
        SERIAL_clear_buffer();
        if (asic_reset() != ESP_OK) {
            GLOBAL_STATE->SYSTEM_MODULE.asic_status = "ASIC reset failed";
            return 0;
        }
    }
    
    if (chip_count == 0) {
        const char *chain_error = get_asic_chain_error();
        ESP_LOGE(TAG, "ASIC initialization failed - chip chain detection failed");
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = chain_error != NULL ? chain_error : "ASIC chain detection failed";
        return 0;
    }

    if (is_proto_mc3) {
        if (!MC3_start_qualification_work(GLOBAL_STATE)) {
            GLOBAL_STATE->SYSTEM_MODULE.asic_status = "Proto qualification work failed";
            return 0;
        }
        if (ramp_proto_core_voltage(
                GLOBAL_STATE, init_core_voltage_mv, target_core_voltage_mv) != ESP_OK) {
            GLOBAL_STATE->SYSTEM_MODULE.asic_status = "Proto Vcore ramp failed";
            return 0;
        }
    }

    if (is_proto_mc3 && !GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
        bool staggered_profile_activated = false;
        if (PROTO_FORCE_BROADCAST_PLL_UPDATES) {
            ESP_LOGW(TAG,
                "BROADCAST_PLL_ONLY: all frequency transitions use uniform broadcast PLL0 updates");
        } else if (PROTO_RUN_PING_PONG_PLL_EXPERIMENT) {
            ESP_LOGW(TAG,
                "PING_PONG EXPERIMENT: skipping stop/restart diagnostics and qualifying live PLL0/PLL1 clock transfers");
        } else if (PROTO_RUN_STAGGERED_PLL_DIAGNOSTIC) {
            if (!run_proto_staggered_pll_diagnostic(
                    GLOBAL_STATE, &staggered_profile_activated)) {
                GLOBAL_STATE->SYSTEM_MODULE.asic_status =
                    "MC3 staggered PLL diagnostic failed";
                ESP_LOGE(TAG, "MC3 staggered PLL diagnostic failed");
                return 0;
            }
        } else if (PROTO_RUN_VOLTAGE_FREQUENCY_MATRIX) {
            if (!run_proto_voltage_frequency_matrix(
                    GLOBAL_STATE, target_core_voltage_mv)) {
                GLOBAL_STATE->SYSTEM_MODULE.asic_status =
                    "MC3 voltage/frequency matrix failed";
                ESP_LOGE(TAG, "MC3 voltage/frequency matrix failed");
                return 0;
            }
        } else {
            if (!run_proto_a1_internal_domain_diagnostic(GLOBAL_STATE)) {
                GLOBAL_STATE->SYSTEM_MODULE.asic_status =
                    "MC3 A1 internal-domain diagnostic failed";
                ESP_LOGE(TAG, "MC3 A1 internal-domain diagnostic failed");
                return 0;
            }
        }
        if (!staggered_profile_activated &&
            !qualify_proto_frequency_ramp(GLOBAL_STATE)) {
            GLOBAL_STATE->SYSTEM_MODULE.asic_status = "MC3 startup qualification failed";
            ESP_LOGE(TAG, "MC3 startup qualification failed");
            return 0;
        }
        if (!staggered_profile_activated &&
            !run_proto_addressed_d2_diagnostic(GLOBAL_STATE)) {
            GLOBAL_STATE->SYSTEM_MODULE.asic_status =
                "MC3 addressed D2 diagnostic failed";
            ESP_LOGE(TAG, "MC3 addressed D2 diagnostic failed");
            return 0;
        }
        if (!staggered_profile_activated &&
            !run_proto_clean_all_chips_diagnostic(GLOBAL_STATE)) {
            GLOBAL_STATE->SYSTEM_MODULE.asic_status =
                "MC3 clean all-chip diagnostic failed";
            ESP_LOGE(TAG, "MC3 clean all-chip diagnostic failed");
            return 0;
        }
    } else if (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id == MC3) {
        if (!MC3_ramp_hash_frequency(GLOBAL_STATE)) {
            GLOBAL_STATE->SYSTEM_MODULE.asic_status = "MC3 frequency ramp failed";
            ESP_LOGE(TAG, "MC3 frequency ramp failed after initialization voltage ramp");
            return 0;
        }
    }

    ESP_LOGI(TAG, "Setting max baud rate and clearing buffers");
    SERIAL_set_baud(ASIC_set_max_baud(GLOBAL_STATE));
    SERIAL_clear_buffer();

    GLOBAL_STATE->ASIC_initalized = true;
    
    if (stabilization_delay_ms > 0) {
        ESP_LOGI(TAG, "Waiting %u ms for tasks to stabilize...", stabilization_delay_ms);
        vTaskDelay(stabilization_delay_ms / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "ASIC initialized successfully with %d chip(s) (%s mode)", chip_count, mode_str);
    return chip_count;
}
