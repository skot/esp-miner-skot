#include "asic_init.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "esp_check.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "asic.h"
#include "asic_common.h"
#include "mc3.h"
#include "serial.h"
#include "asic_reset.h"
#include "device_config.h"
#include "nvs_config.h"
#include "vcore.h"
#include "adc.h"
#include "TPS546.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "fan_controller_task.h"

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
#define PROTO_RUN_STAGGERED_PLL_DIAGNOSTIC 0
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
// Temporary bench experiment; leave disabled in normal mining firmware.
#define PROTO_RUN_WORK_RESET_DELAY_SWEEP 0
#define PROTO_RUN_CLOCK_DELIVERY_PROBE 0
#define PROTO_RUN_BROADCAST_RECOVERY_PROBE 0
#define PROTO_RUN_FIXED_PLL_VOLTAGE_PROBE 0
#define PROTO_RUN_WARM_RAMP_PROBE 0
// Explicitly selected 1102/1103 bench comparison: voltage/throughput report-only.
#define PROTO_WARM_REPORT_ONLY 0
#define PROTO_WARM_HOLD_FIRST_ZERO 0
#define PROTO_WARM_DEFER_WORK 0
#define PROTO_WARM_DEFER_THERMAL_READS 0
#define PROTO_WARM_CYCLE_POWER 0
// Explicit log-only bench: no scope commands or waveform acknowledgement.
#define PROTO_WARM_NO_SCOPE 0
#define PROTO_WARM_BOARD_VERSION "1103"
#define PROTO_WARM_VOLTAGE_MV 1160
#define PROTO_FIXED_PROBE_MHZ 350
#define PROTO_FIXED_BASELINE_MV 1120
#define PROTO_FIXED_TEST_MV 1100
#define PROTO_BROADCAST_PROBE_VOLTAGE_MV 1140
#define PROTO_BROADCAST_PROBE_BASELINE_MHZ 375
#define PROTO_BROADCAST_PROBE_TEST_MHZ 400
#define PROTO_WORK_RESET_DELAY_REPEATS 1
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
    if (GLOBAL_STATE->DEVICE_CONFIG.domain_voltage_sense && regulator_voltage_mv > 0 &&
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
    char balance_text[24] = "unavailable";
    if (GLOBAL_STATE->DEVICE_CONFIG.domain_voltage_sense && domains_valid) {
        snprintf(balance_text, sizeof(balance_text), "%d mV", absolute_domain_delta_mv);
    }
    ESP_LOGI(TAG,
        "Proto qualification %s: total=%.2f/%.2f GH/s throughput=%.2f%% min_chip=%.2f%% min_pass=%.2f%% balance=%s => %s",
        profile, result->total_hashrate_ghs, expected_hashrate_ghs,
        throughput_percent, min_chip_throughput_percent,
        min_pass_rate_percent, balance_text,
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

typedef struct {
    GlobalState *global;
    atomic_bool stop;
    atomic_bool tripped;
    atomic_bool zero_rail_expected;
    SemaphoreHandle_t stopped;
    uint16_t max_lower_mv;
    uint16_t max_upper_mv;
    uint16_t max_total_mv;
} proto_delay_guard_t;

// Both boards route GPIO10 to active-high TPS EN. Also issue PMBus OFF on
// 1102 as a second shutdown command; preserve the established 1103 sequence.
static esp_err_t proto_probe_power_off(GlobalState *global)
{
    esp_err_t gpio_err = VCORE_prepare_asic_enable(global);
    bool is_1102 = global->DEVICE_CONFIG.board_version != NULL &&
        strcmp(global->DEVICE_CONFIG.board_version, "1102") == 0;
    if (is_1102 || !global->DEVICE_CONFIG.asic_enable ||
        !global->DEVICE_CONFIG.asic_enable_active_high) {
        esp_err_t regulator_err = VCORE_set_voltage(global, 0);
        return regulator_err != ESP_OK ? regulator_err : gpio_err;
    }
    return gpio_err;
}

static bool proto_warm_report_only(const GlobalState *global)
{
    return PROTO_RUN_WARM_RAMP_PROBE && PROTO_WARM_REPORT_ONLY &&
        global->DEVICE_CONFIG.board_version != NULL &&
        strcmp(global->DEVICE_CONFIG.board_version, PROTO_WARM_BOARD_VERSION) == 0 &&
        (strcmp(PROTO_WARM_BOARD_VERSION, "1102") == 0 ||
         (strcmp(PROTO_WARM_BOARD_VERSION, "1103") == 0 &&
          global->DEVICE_CONFIG.domain_voltage_sense));
}

static bool proto_probe_read_rails(GlobalState *global, int16_t *total,
    uint16_t *lower, uint16_t *upper, bool power_cycle_active)
{
    *total = VCORE_get_regulator_voltage_mv_fresh(global);
    *lower = 0;
    *upper = 0;
    if (*total < 0) return false;
    // TPS and ADC samples are sequential. During the bounded supply cycle,
    // combining them can describe different points on a rising/falling rail.
    // Keep checking both reads, then resume half-stack calculations after
    // enable_proto_power has settled and the total rail has been verified.
    if (power_cycle_active && proto_warm_report_only(global)) {
        uint16_t midpoint;
        return !global->DEVICE_CONFIG.domain_voltage_sense ||
            ADC_get_domain_midpoint_mv(&midpoint) == ESP_OK;
    }
    if (*total <= 0) return false;
    if (global->DEVICE_CONFIG.domain_voltage_sense) {
        return VCORE_get_domain_voltages_mv(global, *total, lower, upper) == ESP_OK &&
            (proto_warm_report_only(global) ||
             (*lower < PROTO_CLEAN_ALL_RECOVERY_MAX_DOMAIN_MV &&
              *upper < PROTO_CLEAN_ALL_RECOVERY_MAX_DOMAIN_MV));
    }
    // Only the explicitly supported 1102 warm comparison may lack midpoint
    // sensing. This guards the total rail; it cannot detect redistribution.
    return global->DEVICE_CONFIG.board_version != NULL &&
        strcmp(global->DEVICE_CONFIG.board_version, "1102") == 0 &&
        (proto_warm_report_only(global) ||
         *total < 2 * PROTO_CLEAN_ALL_RECOVERY_MAX_DOMAIN_MV);
}

// Independent of the ASIC UART lock: also watches the PLL updates, settling
// interval, and blocking SPDLOG windows. This is sampled protection, not a
// substitute for an oscilloscope or hardware overvoltage protection.
static void proto_delay_guard_task(void *arg)
{
    proto_delay_guard_t *guard = arg;
    while (!atomic_load(&guard->stop)) {
        int16_t total = 0;
        uint16_t lower = 0, upper = 0;
        bool valid = proto_probe_read_rails(guard->global, &total, &lower, &upper,
            atomic_load(&guard->zero_rail_expected));
        if (atomic_load(&guard->stop)) break;
        if (total > guard->max_total_mv) guard->max_total_mv = total;
        if (lower > guard->max_lower_mv) guard->max_lower_mv = lower;
        if (upper > guard->max_upper_mv) guard->max_upper_mv = upper;
        if (!valid) {
            atomic_store(&guard->tripped, true);
            esp_err_t off_err = proto_probe_power_off(guard->global);
            ESP_LOGE(TAG,
                "RESET_DELAY SAFETY STOP valid=%u PCB_sense=%u PCB=%u/%u total=%d mV; shutdown=%s",
                valid, guard->global->DEVICE_CONFIG.domain_voltage_sense,
                lower, upper, total, esp_err_to_name(off_err));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    xSemaphoreGive(guard->stopped);
    vTaskDelete(NULL);
}

static bool capture_proto_delay_profile(GlobalState *global,
    const char *stage, bool *accepted_out)
{
    mc3_qualification_result_t result = {0};
    if (!MC3_measure_active_frequency_profile(global, &result)) return false;
    bool accepted = evaluate_proto_qualification(global, stage, &result, NULL);
    for (uint8_t chip = 0; chip < result.chip_count; chip++) {
        mc3_core_domain_summary_t summary = {0};
        if (!MC3_read_core_domain_summary(chip, &summary)) return false;
        double expected_ghs = result.chip_frequency_mhz[chip] *
            global->DEVICE_CONFIG.family.asic.small_core_count / 1000.0;
        for (uint8_t domain = 0; domain < MC3_INTERNAL_VOLTAGE_DOMAIN_COUNT; domain++) {
            // Convert per-core counts to absolute expected domain throughput,
            // rather than the relative share of an already weakened ASIC.
            double percent = result.passed[chip] > 0 && expected_ghs > 0
                ? summary.domain_pass[domain] * 400.0 * result.hashrate_ghs[chip] /
                    (result.passed[chip] * expected_ghs) : 0.0;
            accepted = accepted && percent >= PROTO_MIN_THROUGHPUT_PERCENT &&
                summary.domain_zero_cores[domain] == 0;
            ESP_LOGW(TAG, "RESET_DELAY DOMAIN stage=%s A%u D%u absolute=%.2f%% zero=%u",
                stage, chip + 1, domain, percent, summary.domain_zero_cores[domain]);
        }
    }
    *accepted_out = accepted;
    ESP_LOGW(TAG, "RESET_DELAY PROFILE stage=%s accepted=%s", stage,
        accepted ? "PASS" : "REJECT");
    return true;
}

// Preserve progress across deliberate ESP software restarts; each trial gets
// a fully powered-off ASIC rail and the normal ordered 400 MHz startup ramp.
static RTC_NOINIT_ATTR unsigned proto_delay_trial_index;
static RTC_NOINIT_ATTR bool proto_delay_trial_in_progress;
static RTC_NOINIT_ATTR uint32_t proto_delay_progress_magic;

static bool proto_clock_probe_abort(void *context)
{
    proto_delay_guard_t *guard = context;
    return atomic_load(&guard->tripped);
}

typedef struct {
    mc3_core_domain_summary_t previous[4];
    int64_t previous_us[4];
    unsigned healthy_streak;
    float temperature_c[4];
} proto_bcast_observation_t;

static bool proto_bcast_snapshot(GlobalState *global, proto_delay_guard_t *guard,
    const char *phase, proto_bcast_observation_t *observation)
{
    bool healthy = true;
    mc3_pvt_voltage_reading_t pvt[4] = {0};
    float temperatures[4] = {0};
    if (atomic_load(&guard->tripped) || MC3_read_pvt_voltages(pvt, 4) != 4 ||
        MC3_read_temperatures(temperatures, 4) != 4) return false;
    for (unsigned chip = 0; chip < 4; chip++) {
        if (!isfinite(temperatures[chip]) || temperatures[chip] <= 0 || temperatures[chip] >= 70) {
            ESP_LOGE(TAG, "BCAST temperature invalid/high A%u %.2f C", chip + 1, temperatures[chip]);
            return false;
        }
    }
    for (uint8_t chip = 0; chip < 4; chip++) {
        mc3_core_domain_summary_t current = {0};
        int64_t start = esp_timer_get_time();
        if (atomic_load(&guard->tripped) || !MC3_read_core_domain_summary(chip, &current)) return false;
        int64_t now = (start + esp_timer_get_time()) / 2;
        int64_t dt = now - observation->previous_us[chip];
        double rate[4] = {-1, -1, -1, -1};
        for (unsigned d = 0; d < 4; d++) {
            if (observation->previous_us[chip]) {
                if (current.domain_pass[d] < observation->previous[chip].domain_pass[d] ||
                    current.domain_fail[d] < observation->previous[chip].domain_fail[d]) {
                    ESP_LOGE(TAG, "BCAST counters reset/wrapped A%u D%u", chip + 1, d);
                    return false;
                }
                uint32_t delta = current.domain_pass[d] - observation->previous[chip].domain_pass[d];
                double expected = global->POWER_MANAGEMENT_MODULE.actual_frequency * global->DEVICE_CONFIG.family.asic.small_core_count / 4000.0;
                rate[d] = delta * 16777.216 / dt * 100.0 / expected;
            }
            // Only used to avoid unnecessary interventions when already
            // recovered; production qualification still requires longer data.
            healthy = healthy && rate[d] >= 95.0;
        }
        ESP_LOGW(TAG, "BCAST SAMPLE phase=%s A%u counter_us=%" PRId64 " dt_us=%" PRId64
            " pass=%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
            " rate=%.2f,%.2f,%.2f,%.2f zero=%u,%u,%u,%u D2_mV=%.2f"
            " fail=%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 " temp_C=%.2f freq=%.0f",
            phase, chip + 1, now, observation->previous_us[chip] ? dt : 0,
            current.domain_pass[0], current.domain_pass[1], current.domain_pass[2], current.domain_pass[3],
            rate[0], rate[1], rate[2], rate[3], current.domain_zero_cores[0], current.domain_zero_cores[1],
            current.domain_zero_cores[2], current.domain_zero_cores[3],
            proto_pvt_d2_voltage_mv(&pvt[chip]),
            current.domain_fail[0], current.domain_fail[1], current.domain_fail[2], current.domain_fail[3], temperatures[chip],
            global->POWER_MANAGEMENT_MODULE.actual_frequency);
        observation->previous[chip] = current;
        observation->previous_us[chip] = now;
        observation->temperature_c[chip] = temperatures[chip];
    }
    int16_t total = 0;
    uint16_t lower = 0, upper = 0;
    if (!proto_probe_read_rails(global, &total, &lower, &upper, false)) return false;
    if (global->DEVICE_CONFIG.domain_voltage_sense) {
        ESP_LOGW(TAG, "BCAST RAIL phase=%s PCB=%u/%u mV TPS=%.3fA fan=%.0f%% rpm=%u", phase, lower, upper, TPS546_get_iout(),
            global->POWER_MANAGEMENT_MODULE.fan_perc, (unsigned)global->POWER_MANAGEMENT_MODULE.fan_rpm);
    } else {
        ESP_LOGW(TAG, "BCAST RAIL phase=%s PCB=unavailable TPS_total=%d mV TPS=%.3fA fan=%.0f%% rpm=%u", phase, total, TPS546_get_iout(),
            global->POWER_MANAGEMENT_MODULE.fan_perc, (unsigned)global->POWER_MANAGEMENT_MODULE.fan_rpm);
    }
    observation->healthy_streak = healthy ? observation->healthy_streak + 1 : 0;
    return !atomic_load(&guard->tripped);
}

static int proto_bcast_command(void)
{
    char command = 0;
    return usb_serial_jtag_read_bytes(&command, 1, 0) == 1 ? command : 0;
}

static void run_proto_broadcast_recovery_probe(GlobalState *global)
{
    proto_delay_guard_t guard = {.global = global};
    proto_bcast_observation_t observation = {0};
    atomic_init(&guard.stop, false);
    atomic_init(&guard.tripped, false);
    bool ok = false;
    usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (!usb_serial_jtag_is_driver_installed() && usb_serial_jtag_driver_install(&usb_config) != ESP_OK) goto power_off;
    guard.stopped = xSemaphoreCreateBinary();
    if (!guard.stopped) goto power_off;
    if (xTaskCreate(proto_delay_guard_task, "bcast_guard", 4096, &guard, 6, NULL) != pdPASS) {
        vSemaphoreDelete(guard.stopped);
        goto power_off;
    }
    ESP_LOGW(TAG, "BCAST BEGIN voltage=%u baseline=%u test=%u; guard=1200; NVS unchanged", PROTO_BROADCAST_PROBE_VOLTAGE_MV, PROTO_BROADCAST_PROBE_BASELINE_MHZ, PROTO_BROADCAST_PROBE_TEST_MHZ);
    mc3_qualification_result_t baseline_result = {0};
    for (unsigned frequency = 100; frequency <= PROTO_BROADCAST_PROBE_BASELINE_MHZ; frequency += 25) {
        if (atomic_load(&guard.tripped) || !qualify_proto_frequency_step(global, frequency, &baseline_result)) {
            ESP_LOGE(TAG, "BCAST baseline ramp rejected at %u MHz; no test transition", frequency);
            goto finish;
        }
    }
    // Use the completed qualification window without another SPD/FIFO arm.
    // The normal per-ASIC criteria already passed. Require healthy D2 and no
    // inactive cores; record smaller deviations in other domains explicitly.
    bool baseline_accepted = true;
    for (uint8_t chip = 0; chip < 4; chip++) {
        mc3_core_domain_summary_t summary = {0};
        if (atomic_load(&guard.tripped) || !MC3_read_core_domain_summary(chip, &summary)) goto finish;
        double expected = PROTO_BROADCAST_PROBE_BASELINE_MHZ * global->DEVICE_CONFIG.family.asic.small_core_count / 1000.0;
        for (unsigned d = 0; d < 4; d++) {
            double percent = baseline_result.passed[chip] > 0
                ? summary.domain_pass[d] * 400.0 * baseline_result.hashrate_ghs[chip] /
                    (baseline_result.passed[chip] * expected) : 0;
            ESP_LOGW(TAG, "BCAST BASE_DOMAIN A%u D%u absolute=%.2f zero=%u", chip + 1, d, percent, summary.domain_zero_cores[d]);
            baseline_accepted = baseline_accepted && percent >= (d == 2 ? 97.0 : 90.0) && summary.domain_zero_cores[d] == 0;
        }
    }
    if (!baseline_accepted) goto finish;
    // Verify host-control reception at the healthy baseline before any test-frequency
    // command. This USB channel is used only by the disabled bench harness.
    while (proto_bcast_command()) {}
    ESP_LOGW(TAG, "BCAST READY: wait up to 30s for G after scope arming");
    int64_t deadline = esp_timer_get_time() + 30000000;
    int command = 0;
    do {
        command = proto_bcast_command();
        if (command == 'G' || command == 'X' || atomic_load(&guard.tripped)) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    } while (esp_timer_get_time() < deadline);
    if (command != 'G' || atomic_load(&guard.tripped)) goto finish;
    ESP_LOGW(TAG, "BCAST TRIAL broadcast %u->%u voltage=%u", PROTO_BROADCAST_PROBE_BASELINE_MHZ, PROTO_BROADCAST_PROBE_TEST_MHZ, PROTO_BROADCAST_PROBE_VOLTAGE_MV);
    if (!MC3_start_broadcast_recovery_probe(global, PROTO_BROADCAST_PROBE_BASELINE_MHZ, PROTO_BROADCAST_PROBE_TEST_MHZ, proto_clock_probe_abort, &guard)) goto finish;
    ESP_LOGW(TAG, "BCAST LAUNCHED; passive counters until scope verdict R; X aborts");
    deadline = esp_timer_get_time() + 120000000;
    command = 0;
    while (esp_timer_get_time() < deadline) {
        if (!proto_bcast_snapshot(global, &guard, "passive", &observation)) goto finish;
        command = proto_bcast_command();
        if (command == 'R' || command == 'X') break;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (command != 'R' || atomic_load(&guard.tripped)) {
        ESP_LOGE(TAG, "BCAST scope verdict missing/rejected; no recovery intervention");
        goto finish;
    }
    ESP_LOGW(TAG, "BCAST SCOPE ACCEPTED healthy_streak=%u", observation.healthy_streak);
    const char *phases[] = {"fifo_only", "spd_only", "full_work"};
    for (unsigned action = 1; action <= 3; action++) {
        if (observation.healthy_streak >= 3) {
            ESP_LOGW(TAG, "BCAST RECOVERED before action=%u; no further strobes", action);
            ok = true;
            goto finish;
        }
        ESP_LOGW(TAG, "BCAST ACTION phase=%s call_us=%" PRId64 "; PLLs fixed, counters not rearmed",
            phases[action - 1], esp_timer_get_time());
        if (atomic_load(&guard.tripped) || !MC3_broadcast_recovery_action(action)) goto finish;
        observation.healthy_streak = 0;
        deadline = esp_timer_get_time() + (action == 3 ? 15000000 : 8000000);
        do {
            if (proto_bcast_command() == 'X' || !proto_bcast_snapshot(global, &guard, phases[action - 1], &observation)) goto finish;
            vTaskDelay(pdMS_TO_TICKS(250));
        } while (esp_timer_get_time() < deadline);
    }
    ok = true;
finish:
    atomic_store(&guard.stop, true);
    VCORE_prepare_asic_enable(global);
    xSemaphoreTake(guard.stopped, portMAX_DELAY);
    vSemaphoreDelete(guard.stopped);
    ESP_LOGW(TAG, "BCAST END result=%s healthy_streak=%u max_PCB=%u/%u mV ASIC_power=OFF",
        ok && !atomic_load(&guard.tripped) ? "COMPLETE" : "ERROR", observation.healthy_streak,
        guard.max_lower_mv, guard.max_upper_mv);
    return;
power_off:
    VCORE_prepare_asic_enable(global);
    ESP_LOGE(TAG, "BCAST setup failed; ASIC power off");
}

static void proto_warm_fan(const proto_bcast_observation_t *observation, int64_t elapsed_us)
{
    float hottest = 0;
    for (unsigned chip = 0; chip < 4; chip++) {
        if (observation->temperature_c[chip] > hottest) hottest = observation->temperature_c[chip];
    }
    int fan = hottest < 50 ? (elapsed_us > 60000000 ? 10 : 20) :
        hottest < 54 ? 15 : hottest < 56 ? 20 : hottest < 58 ? 30 : hottest < 60 ? 50 : 100;
    FAN_CONTROLLER_set_bench_percent(fan);
}

static bool proto_warm_snapshot(GlobalState *global, proto_delay_guard_t *guard,
    const char *phase, proto_bcast_observation_t *observation, int64_t warm_start)
{
    if (global->SYSTEM_MODULE.hardware_fault ||
        !proto_bcast_snapshot(global, guard, phase, observation)) return false;
    proto_warm_fan(observation, esp_timer_get_time() - warm_start);
    return true;
}

static bool proto_warm_rates(GlobalState *global, const proto_bcast_observation_t *first,
    const proto_bcast_observation_t *last)
{
    bool healthy = true;
    for (unsigned chip = 0; chip < 4; chip++) {
        double dt = last->previous_us[chip] - first->previous_us[chip];
        if (dt <= 0) return false;
        for (unsigned d = 0; d < 4; d++) {
            uint32_t pass = last->previous[chip].domain_pass[d] - first->previous[chip].domain_pass[d];
            uint32_t fail = last->previous[chip].domain_fail[d] - first->previous[chip].domain_fail[d];
            double expected = global->POWER_MANAGEMENT_MODULE.actual_frequency *
                global->DEVICE_CONFIG.family.asic.small_core_count / 4000.0;
            double percent = pass * 16777.216 / dt / expected * 100.0;
            double pass_rate = pass + fail ? 100.0 * pass / ((uint64_t)pass + fail) : 0;
            healthy = healthy && percent >= 97 && pass_rate >= 97 &&
                last->previous[chip].domain_zero_cores[d] == 0;
            ESP_LOGW(TAG, "WARM DOMAIN freq=%.0f A%u D%u seconds=%.3f throughput=%.2f pass_rate=%.2f",
                global->POWER_MANAGEMENT_MODULE.actual_frequency, chip + 1, d, dt / 1e6, percent, pass_rate);
        }
    }
    return healthy;
}

static uint16_t proto_warm_zero_pass_mask(const proto_bcast_observation_t *observation)
{
    uint16_t mask = 0;
    for (unsigned chip = 0; chip < 4; chip++) {
        if (!observation->previous_us[chip]) continue;
        for (unsigned d = 0; d < 4; d++) {
            if (observation->previous[chip].domain_pass[d] == 0 &&
                observation->previous[chip].domain_zero_cores[d] == 39) {
                mask |= (uint16_t)(1U << (chip * 4 + d));
            }
        }
    }
    return mask;
}

static void proto_warm_latch_zero(const proto_bcast_observation_t *observation,
    unsigned target, int64_t *first_zero_at, uint16_t *zero_mask)
{
    if (*first_zero_at) return;
    uint16_t mask = proto_warm_zero_pass_mask(observation);
    if (!mask) return;
    *first_zero_at = esp_timer_get_time();
    *zero_mask = mask;
    ESP_LOGW(TAG, "WARM ZERO DETECTED target=%u mask=0x%04x hold_seconds=120; no further PLL/work/counter rearm", target, mask);
}

static bool proto_warm_hold_zero(GlobalState *global, proto_delay_guard_t *guard,
    const char *phase, proto_bcast_observation_t *observation, int64_t warm_start,
    int64_t first_zero_at, uint16_t zero_mask)
{
    // The launch armed 180 seconds of counting; this hold starts within the
    // first 20 seconds. Preserve that epoch and the active work throughout.
    const int64_t deadline = first_zero_at + 120000000;
    proto_bcast_observation_t first = *observation, tail = *observation;
    bool tail_saved = false;
    ESP_LOGW(TAG, "WARM ZERO HOLD freq=%.0f mask=0x%04x; fixed PLL/work/voltage, counters not rearmed", global->POWER_MANAGEMENT_MODULE.actual_frequency, zero_mask);
    do {
        if (proto_bcast_command() == 'X' || !proto_warm_snapshot(global, guard, phase, observation, warm_start)) return false;
        if (!tail_saved && deadline - esp_timer_get_time() <= 10000000) {
            tail = *observation;
            tail_saved = true;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    } while (esp_timer_get_time() < deadline);
    ESP_LOGW(TAG, "WARM ZERO HOLD WHOLE_WINDOW");
    proto_warm_rates(global, &first, observation);
    ESP_LOGW(TAG, "WARM ZERO HOLD LAST_10_SECONDS");
    bool recovered = tail_saved && proto_warm_rates(global, &tail, observation);
    ESP_LOGW(TAG, "WARM ZERO HOLD END elapsed_seconds=%.3f recovered_all=%u initial_mask=0x%04x; powering off",
        (esp_timer_get_time() - first_zero_at) / 1e6, recovered, zero_mask);
    return true;
}

static bool proto_warm_deferred_wait(proto_delay_guard_t *guard, uint32_t milliseconds)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)milliseconds * 1000;
    do {
        if (atomic_load(&guard->tripped) || guard->global->SYSTEM_MODULE.hardware_fault ||
            proto_bcast_command() == 'X') return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (esp_timer_get_time() < deadline);
    return true;
}

static bool proto_warm_deferred_temperature(GlobalState *global, proto_delay_guard_t *guard)
{
    // No SPDLOG arm, work configuration or per-core counter reads before work.
    proto_bcast_observation_t temperatures = {0};
    if (atomic_load(&guard->tripped) || global->SYSTEM_MODULE.hardware_fault ||
        MC3_read_temperatures(temperatures.temperature_c, 4) != 4) return false;
    for (unsigned chip = 0; chip < 4; chip++) {
        float t = temperatures.temperature_c[chip];
        if (!isfinite(t) || t <= 0 || t >= 70) {
            ESP_LOGE(TAG, "DEFER temperature invalid/high A%u %.2f C", chip + 1, t);
            return false;
        }
    }
    proto_warm_fan(&temperatures, 0);
    ESP_LOGW(TAG, "DEFER TEMP freq=%.0f A1=%.2f A2=%.2f A3=%.2f A4=%.2f; no work sent",
        global->POWER_MANAGEMENT_MODULE.actual_frequency,
        temperatures.temperature_c[0], temperatures.temperature_c[1],
        temperatures.temperature_c[2], temperatures.temperature_c[3]);
    return true;
}

static bool proto_warm_cycle_asic_power(GlobalState *global, proto_delay_guard_t *guard)
{
    if (!proto_warm_report_only(global)) return false;
    bool ok = false;
    atomic_store(&guard->zero_rail_expected, true);
    ESP_LOGW(TAG, "DEFER POWER_CYCLE disable Vcore and VDDIO while holding reset");
    if (asic_prepare_power(global) != ESP_OK || VCORE_set_voltage(global, 0) != ESP_OK ||
        !proto_warm_deferred_wait(guard, 250)) goto finish;
    int64_t deadline = esp_timer_get_time() + 2000000;
    int16_t total;
    do {
        total = VCORE_get_regulator_voltage_mv_fresh(global);
        if (total < 0) goto finish;
        if (total <= 100) break;
        if (!proto_warm_deferred_wait(guard, 25)) goto finish;
    } while (esp_timer_get_time() < deadline);
    if (total > 100) goto finish;
    ESP_LOGW(TAG, "DEFER POWER_CYCLE discharged TPS_total=%d mV; restart at 800mV/half", total);
    if (enable_proto_power(global, 800) != ESP_OK ||
        atomic_load(&guard->tripped) || proto_bcast_command() == 'X') goto finish;
    total = VCORE_get_regulator_voltage_mv_fresh(global);
    ok = total >= 1500 && total <= 1700;
finish:
    atomic_store(&guard->zero_rail_expected, false);
    return ok;
}

static bool proto_warm_reset_ramp_first_work(GlobalState *global, proto_delay_guard_t *guard)
{
    // This bench runs before ASIC_initalized releases normal work producers.
    // The optional power-cycle comparison resets both ASIC supplies while
    // keeping the ESP32 and fan control running; otherwise retain the rails.
    if (global->ASIC_initalized || !proto_warm_report_only(global) ||
        PROTO_WARM_VOLTAGE_MV != 1160 || atomic_load(&guard->tripped)) return false;
    if (asic_hold_reset_low() != ESP_OK) return false;
    if (PROTO_WARM_CYCLE_POWER) {
        if (!PROTO_WARM_DEFER_THERMAL_READS ||
            !proto_warm_cycle_asic_power(global, guard)) return false;
    } else {
        ESP_LOGW(TAG, "DEFER RESET begin; ASIC rail stays at 1160mV/half, no power cycle");
    }
    SERIAL_set_baud(UART_FREQ);
    SERIAL_clear_buffer();
    if (asic_reset() != ESP_OK) return false; // 100 ms low, 100 ms after release.
    if (atomic_load(&guard->tripped) || proto_bcast_command() == 'X') return false;
    clear_asic_chain_error();
    if (MC3_init(global) != 4 ||
        global->POWER_MANAGEMENT_MODULE.actual_frequency != 100.0f) return false;
    SERIAL_clear_buffer();
    ESP_LOGW(TAG, "DEFER REINITIALIZED chips=4 frequency=100; no work/WORK_CFG/SPDLOG arm since reset");
    if (PROTO_WARM_CYCLE_POWER) {
        for (unsigned mv = 810; mv <= PROTO_WARM_VOLTAGE_MV; mv += 10) {
            if (set_proto_core_voltage(global, mv) != ESP_OK ||
                !proto_warm_deferred_wait(guard, 150)) return false;
            ESP_LOGI(TAG, "DEFER VCORE target=%u mV/half TPS_total=%d mV; 100MHz no work",
                mv, VCORE_get_regulator_voltage_mv(global));
        }
    }
    // In the historical sequence, the first thermal conversion follows the
    // completed PLL ramp. The ASICs were measured warm immediately before
    // reset; bound this interval and require fresh temperatures before work.
    int64_t ramp_deadline = esp_timer_get_time() + 20000000;
    if (PROTO_WARM_DEFER_THERMAL_READS) {
        ESP_LOGW(TAG, "DEFER THERMAL first conversion after 600MHz settling; ramp limited to 20s");
    } else if (!proto_warm_deferred_temperature(global, guard)) return false;
    if (!proto_warm_deferred_wait(guard, 500)) return false;
    // Do not use the generic table walker: it includes fractional frequencies.
    // Keep the current divider tuples and broadcast CFG/EN packet unchanged.
    for (unsigned frequency = 125; frequency <= 600; frequency += 25) {
        if (atomic_load(&guard->tripped) || proto_bcast_command() == 'X' ||
            esp_timer_get_time() >= ramp_deadline) return false;
        float actual = MC3_send_hash_frequency(frequency);
        if (actual != frequency) return false;
        global->POWER_MANAGEMENT_MODULE.actual_frequency = actual;
        uint32_t settle_ms = frequency == 600 ? 1500 : frequency > 500 ? 1000 : 500;
        ESP_LOGW(TAG, "DEFER PLL LOCKED target=%u settle_ms=%u; no work sent", frequency, settle_ms);
        if (!proto_warm_deferred_wait(guard, settle_ms)) return false;
        if (!PROTO_WARM_DEFER_THERMAL_READS &&
            !proto_warm_deferred_temperature(global, guard)) return false;
    }
    if (esp_timer_get_time() >= ramp_deadline) return false;
    if (PROTO_WARM_DEFER_THERMAL_READS &&
        !proto_warm_deferred_temperature(global, guard)) return false;
    if (atomic_load(&guard->tripped) || proto_bcast_command() == 'X') return false;
    ESP_LOGW(TAG, "DEFER FIRST_WORK frequency=600 call_us=%" PRId64 "; first job since warm reset", esp_timer_get_time());
    if (!MC3_start_qualification_work(global) || atomic_load(&guard->tripped)) return false;
    return MC3_start_active_spdlog_observation(180000);
}

static bool proto_warm_deferred_work(GlobalState *global, proto_delay_guard_t *guard,
    proto_bcast_observation_t *observation, int64_t warm_start)
{
    int64_t deadline;
    int command = 0;
    while (proto_bcast_command()) {}
    if (!PROTO_WARM_NO_SCOPE) {
        ESP_LOGW(TAG, "DEFER READY; wait up to 30s for scope G, then T triggers warm reset/ramp/first work");
        deadline = esp_timer_get_time() + 30000000;
        do {
            command = proto_bcast_command();
            if (command == 'G' || command == 'X') break;
            if (!proto_warm_snapshot(global, guard, "wait_defer", observation, warm_start)) return false;
            vTaskDelay(pdMS_TO_TICKS(100));
        } while (esp_timer_get_time() < deadline);
        if (command != 'G' || atomic_load(&guard->tripped)) return false;
        ESP_LOGW(TAG, "DEFER ARMED; wait up to 5s for scope trigger T");
        deadline = esp_timer_get_time() + 5000000;
        do {
            command = proto_bcast_command();
            if (command == 'T' || command == 'X' || atomic_load(&guard->tripped)) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (esp_timer_get_time() < deadline);
        if (command != 'T' || atomic_load(&guard->tripped)) return false;
    } else {
        ESP_LOGW(TAG, "DEFER NO_SCOPE: log-only run; no waveform captured or scope acknowledgement required");
    }
    ESP_LOGW(TAG, "DEFER TRIAL call_us=%" PRId64, esp_timer_get_time());
    if (!proto_warm_reset_ramp_first_work(global, guard)) return false;
    int64_t launched_at = esp_timer_get_time();
    ESP_LOGW(TAG, "DEFER LAUNCHED frequency=600; observe 120s without work/PLL/counter rearm; %s",
        PROTO_WARM_NO_SCOPE ? "NO_SCOPE log-only" : "scope R required within 60s");
    *observation = (proto_bcast_observation_t) {0};
    proto_bcast_observation_t first = {0}, tail = {0};
    bool first_saved = false, tail_saved = false, scope_recorded = false;
    deadline = launched_at + 120000000;
    do {
        command = proto_bcast_command();
        if (command == 'X') return false;
        if (command == 'R') scope_recorded = true;
        if (!PROTO_WARM_NO_SCOPE && !scope_recorded && esp_timer_get_time() - launched_at >= 60000000) {
            ESP_LOGE(TAG, "DEFER scope acknowledgement timeout");
            return false;
        }
        if (!proto_warm_snapshot(global, guard, "warm600", observation, warm_start)) return false;
        if (!first_saved) { first = *observation; first_saved = true; }
        if (!tail_saved && deadline - esp_timer_get_time() <= 10000000) {
            tail = *observation; tail_saved = true;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    } while (esp_timer_get_time() < deadline);
    ESP_LOGW(TAG, "DEFER WHOLE_WINDOW");
    proto_warm_rates(global, &first, observation);
    ESP_LOGW(TAG, "DEFER LAST_10_SECONDS");
    bool healthy = tail_saved && proto_warm_rates(global, &tail, observation);
    ESP_LOGW(TAG, "DEFER END observed_seconds=%.3f healthy_all=%u; powering off",
        (esp_timer_get_time() - launched_at) / 1e6, healthy);
    return PROTO_WARM_NO_SCOPE || scope_recorded;
}

static void run_proto_warm_ramp_probe(GlobalState *global)
{
    proto_delay_guard_t guard = {.global = global};
    proto_bcast_observation_t observation = {0};
    atomic_init(&guard.stop, false);
    atomic_init(&guard.tripped, false);
    bool ok = false;
    const bool report_only = proto_warm_report_only(global);
    const bool hold_first_zero = report_only && PROTO_WARM_HOLD_FIRST_ZERO;
    esp_err_t off_err = ESP_OK;
    char max_pcb_text[32] = "unavailable";
    int64_t warm_start = esp_timer_get_time();
    usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (!usb_serial_jtag_is_driver_installed() && usb_serial_jtag_driver_install(&usb_config) != ESP_OK) goto power_off;
    guard.stopped = xSemaphoreCreateBinary();
    if (!guard.stopped) goto power_off;
    if (xTaskCreate(proto_delay_guard_task, "warm_guard", 4096, &guard, 6, NULL) != pdPASS) {
        vSemaphoreDelete(guard.stopped);
        goto power_off;
    }
    ESP_LOGW(TAG, "WARM BEGIN baseline=300 voltage=%u target_temp=55 max_frequency=600; fan lease only, NVS unchanged", PROTO_WARM_VOLTAGE_MV);
    ESP_LOGW(TAG, "WARM BOARD version=%s PCB_sense=%u shutdown=%s; sampled limit=%u mV %s",
        global->DEVICE_CONFIG.board_version, global->DEVICE_CONFIG.domain_voltage_sense,
        strcmp(global->DEVICE_CONFIG.board_version, "1102") == 0 ? "GPIO+PMBus" : "GPIO",
        global->DEVICE_CONFIG.domain_voltage_sense ? PROTO_CLEAN_ALL_RECOVERY_MAX_DOMAIN_MV : 2 * PROTO_CLEAN_ALL_RECOVERY_MAX_DOMAIN_MV,
        global->DEVICE_CONFIG.domain_voltage_sense ? "per half" : "total only; scope required for half-stack voltage");
    ESP_LOGW(TAG, "WARM POLICY voltage_limits=%s throughput_limits=%s final_hold_seconds=%u; temperature/communications/manual stop retained",
        report_only ? "REPORT_ONLY" : "ENFORCED", report_only ? "REPORT_ONLY" : "ENFORCED", report_only ? 120 : 10);
    ESP_LOGW(TAG, "WARM POLICY hold_first_zero=%u; stop after 120s observation at first fully zero-PASS domain", hold_first_zero);
    ESP_LOGW(TAG, "WARM POLICY deferred_work=%u; warm reset, no-work PLL ramp, 1500ms final settle", PROTO_WARM_DEFER_WORK);
    if (PROTO_WARM_DEFER_WORK && (!report_only || hold_first_zero || PROTO_WARM_VOLTAGE_MV != 1160)) goto finish;
    if (PROTO_WARM_NO_SCOPE && (!report_only || !PROTO_WARM_DEFER_WORK ||
        !PROTO_WARM_DEFER_THERMAL_READS || !PROTO_WARM_CYCLE_POWER)) goto finish;
    ESP_LOGW(TAG, "WARM FAN saved auto=%u min=%u manual=%u target_temp=%u; temporary fan starts at 20%%",
        nvs_config_get_bool(NVS_CONFIG_AUTO_FAN_SPEED), nvs_config_get_u16(NVS_CONFIG_MIN_FAN_SPEED),
        nvs_config_get_u16(NVS_CONFIG_MANUAL_FAN_SPEED), nvs_config_get_u16(NVS_CONFIG_TEMP_TARGET));
    for (unsigned frequency = 100; frequency <= 300; frequency += 25) {
        mc3_qualification_result_t result = {0};
        FAN_CONTROLLER_set_bench_percent(20);
        if (atomic_load(&guard.tripped) || !MC3_qualify_frequency(global, frequency, &result)) goto finish;
        char profile[24];
        snprintf(profile, sizeof(profile), "%.3fMHz", result.frequency_mhz);
        bool passed = evaluate_proto_qualification(global, profile, &result, NULL);
        if (!passed && !report_only) goto finish;
    }
    if (!proto_warm_snapshot(global, &guard, "cold300", &observation, warm_start)) goto finish;
    warm_start = esp_timer_get_time();
    if (!MC3_start_active_spdlog_observation(180000)) goto finish;
    observation = (proto_bcast_observation_t) {0};
    int64_t last_arm = esp_timer_get_time();
    int64_t ready_since = 0;
    bool warm = false;
    while (esp_timer_get_time() - warm_start < 600000000) {
        if (proto_bcast_command() == 'X' || !proto_warm_snapshot(global, &guard, "warm300", &observation, warm_start)) goto finish;
        float hottest = 0, coldest = 100;
        for (unsigned chip = 0; chip < 4; chip++) {
            float t = observation.temperature_c[chip];
            if (t > hottest) hottest = t;
            if (t < coldest) coldest = t;
        }
        if (hottest >= 54.5f && hottest <= 60 && coldest >= 52) {
            if (!ready_since) ready_since = esp_timer_get_time();
            if (esp_timer_get_time() - ready_since >= 10000000) {warm = true; break;}
        } else ready_since = 0;
        if (esp_timer_get_time() - last_arm >= 100000000) {
            if (!MC3_start_active_spdlog_observation(180000)) goto finish;
            observation = (proto_bcast_observation_t) {0};
            last_arm = esp_timer_get_time();
            ESP_LOGW(TAG, "WARM REARM at 300MHz; prevents counter expiration during warmup");
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (!warm) {ESP_LOGE(TAG, "WARM temperature target not reached in 10 minutes; no higher-frequency transition"); goto finish;}
    ESP_LOGW(TAG, "WARM TEMPERATURE READY; qualify 300MHz at the warm temperature without a PLL update");
    mc3_qualification_result_t result = {0};
    if (!MC3_measure_active_frequency_profile(global, &result)) goto finish;
    bool baseline_passed = evaluate_proto_qualification(global, "WARM baseline300", &result, NULL);
    if (!baseline_passed && !report_only) goto finish;
    observation = (proto_bcast_observation_t) {0};
    if (!proto_warm_snapshot(global, &guard, "warm_qualified300", &observation, warm_start)) goto finish;
    if (PROTO_WARM_DEFER_WORK) {
        ok = proto_warm_deferred_work(global, &guard, &observation, warm_start);
        goto finish;
    }
    for (unsigned target = 325; target <= 600; target += 25) {
        while (proto_bcast_command()) {}
        ESP_LOGW(TAG, "WARM READY target=%u; wait for scope G", target);
        int64_t deadline = esp_timer_get_time() + 30000000;
        int command = 0;
        do {
            command = proto_bcast_command();
            if (command == 'G' || command == 'X') break;
            if (!proto_warm_snapshot(global, &guard, "wait_scope", &observation, warm_start)) goto finish;
            vTaskDelay(pdMS_TO_TICKS(100));
        } while (esp_timer_get_time() < deadline);
        if (command != 'G' || atomic_load(&guard.tripped)) goto finish;
        // Rendezvous after the slow sensor snapshot, then poll tightly so
        // the forced scope record covers the actual transition.
        proto_warm_fan(&observation, esp_timer_get_time() - warm_start);
        ESP_LOGW(TAG, "WARM ARMED target=%u; wait for scope trigger T", target);
        deadline = esp_timer_get_time() + 5000000;
        do {
            command = proto_bcast_command();
            if (command == 'T' || command == 'X' || atomic_load(&guard.tripped)) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (esp_timer_get_time() < deadline);
        if (command != 'T' || atomic_load(&guard.tripped)) goto finish;
        ESP_LOGW(TAG, "WARM TRIAL target=%u call_us=%" PRId64, target, esp_timer_get_time());
        if (!MC3_start_broadcast_recovery_probe(global, target - 25, target, proto_clock_probe_abort, &guard)) goto finish;
        observation = (proto_bcast_observation_t) {0};
        ESP_LOGW(TAG, "WARM LAUNCHED target=%u; await R, X powers off", target);
        int64_t first_zero_at = 0;
        uint16_t zero_mask = 0;
        char phase[24];
        snprintf(phase, sizeof(phase), "warm%u", target);
        deadline = esp_timer_get_time() + 15000000;
        command = 0;
        do {
            if (!proto_warm_snapshot(global, &guard, phase, &observation, warm_start)) goto finish;
            if (hold_first_zero) proto_warm_latch_zero(&observation, target, &first_zero_at, &zero_mask);
            command = proto_bcast_command();
            if (command == 'R' || command == 'X') break;
            vTaskDelay(pdMS_TO_TICKS(100));
        } while (esp_timer_get_time() < deadline);
        if (command != 'R' || atomic_load(&guard.tripped)) goto finish;
        ESP_LOGW(TAG, "WARM SCOPE %s target=%u", report_only ? "RECORDED_REPORT_ONLY" : "ACCEPTED", target);
        proto_bcast_observation_t first = observation;
        deadline = esp_timer_get_time() + (report_only && !hold_first_zero && target == 600 ? 120000000 : 10000000);
        if (report_only && !hold_first_zero && target == 600) ESP_LOGW(TAG, "WARM FINAL HOLD 600MHz for 120s; fixed PLL/work, no counter rearm");
        do {
            if (first_zero_at) break;
            if (proto_bcast_command() == 'X' || !proto_warm_snapshot(global, &guard, phase, &observation, warm_start)) goto finish;
            if (hold_first_zero) proto_warm_latch_zero(&observation, target, &first_zero_at, &zero_mask);
            vTaskDelay(pdMS_TO_TICKS(250));
        } while (esp_timer_get_time() < deadline);
        if (first_zero_at) {
            ok = proto_warm_hold_zero(global, &guard, phase, &observation, warm_start, first_zero_at, zero_mask);
            goto finish;
        }
        bool step_passed = proto_warm_rates(global, &first, &observation);
        if (!step_passed && !report_only) {
            ESP_LOGW(TAG, "WARM REJECT target=%u; hold fixed PLL/work for 120s to observe recovery", target);
            deadline = esp_timer_get_time() + 120000000;
            bool tail_saved = false;
            do {
                if (proto_bcast_command() == 'X' || !proto_warm_snapshot(global, &guard, phase, &observation, warm_start)) goto finish;
                if (!tail_saved && deadline - esp_timer_get_time() <= 10000000) {first = observation; tail_saved = true;}
                vTaskDelay(pdMS_TO_TICKS(250));
            } while (esp_timer_get_time() < deadline);
            step_passed = tail_saved && proto_warm_rates(global, &first, &observation);
            if (!step_passed) goto finish;
        }
        ESP_LOGW(TAG, "WARM STEP %s target=%u policy=%s", step_passed ? "PASS" : "WEAK", target, report_only ? "REPORT_ONLY" : "ENFORCED");
    }
    ok = true;
finish:
    atomic_store(&guard.stop, true);
    off_err = proto_probe_power_off(global);
    FAN_CONTROLLER_set_bench_percent(-1);
    xSemaphoreTake(guard.stopped, portMAX_DELAY);
    vSemaphoreDelete(guard.stopped);
    if (global->DEVICE_CONFIG.domain_voltage_sense) {
        snprintf(max_pcb_text, sizeof(max_pcb_text), "%u/%u mV", guard.max_lower_mv, guard.max_upper_mv);
    }
    ESP_LOGW(TAG, "WARM END result=%s frequency=%.0f max_PCB=%s PCB_sense=%u max_total=%u mV ASIC_power=%s",
        ok && off_err == ESP_OK && !atomic_load(&guard.tripped) ? "COMPLETE" : "STOPPED", global->POWER_MANAGEMENT_MODULE.actual_frequency,
        max_pcb_text, global->DEVICE_CONFIG.domain_voltage_sense,
        guard.max_total_mv, off_err == ESP_OK ? "OFF" : "SHUTDOWN_FAILED");
    return;
power_off:
    off_err = proto_probe_power_off(global);
    FAN_CONTROLLER_set_bench_percent(-1);
    ESP_LOGE(TAG, "WARM END setup failed; shutdown=%s", esp_err_to_name(off_err));
}

static void run_proto_fixed_pll_voltage_probe(GlobalState *global)
{
    proto_delay_guard_t guard = {.global = global};
    proto_bcast_observation_t observation = {0};
    atomic_init(&guard.stop, false);
    atomic_init(&guard.tripped, false);
    bool ok = false;
    usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (!usb_serial_jtag_is_driver_installed() && usb_serial_jtag_driver_install(&usb_config) != ESP_OK) goto power_off;
    guard.stopped = xSemaphoreCreateBinary();
    if (!guard.stopped) goto power_off;
    if (xTaskCreate(proto_delay_guard_task, "fixed_guard", 4096, &guard, 6, NULL) != pdPASS) {
        vSemaphoreDelete(guard.stopped);
        goto power_off;
    }
    ESP_LOGW(TAG, "BCAST BEGIN fixed_PLL=%u baseline_voltage=%u test_voltage=%u; guard=1200; NVS unchanged", PROTO_FIXED_PROBE_MHZ, PROTO_FIXED_BASELINE_MV, PROTO_FIXED_TEST_MV);
    mc3_qualification_result_t baseline_result = {0};
    for (unsigned frequency = 100; frequency <= PROTO_FIXED_PROBE_MHZ; frequency += 25) {
        if (atomic_load(&guard.tripped)) goto finish;
        // Distinguish failed measurement transport from valid but marginal
        // throughput: only the latter is eligible for passive observation.
        if (!MC3_qualify_frequency(global, frequency, &baseline_result)) goto finish;
        char profile[24];
        snprintf(profile, sizeof(profile), "%.3fMHz", (double)frequency);
        bool qualified = evaluate_proto_qualification(global, profile, &baseline_result, NULL);
        if (!qualified) {
            if (atomic_load(&guard.tripped) || baseline_result.chip_count != 4 ||
                baseline_result.frequency_mhz != frequency) goto finish;
            ESP_LOGW(TAG, "BCAST HOLD frequency=%u; rejected baseline; capture frozen counters then one new arm, observe 90s", frequency);
            if (!proto_bcast_snapshot(global, &guard, "rejected_frozen", &observation)) goto finish;
            if (!MC3_start_active_spdlog_observation(180000)) goto finish;
            observation = (proto_bcast_observation_t) {0};
            int64_t hold_deadline = esp_timer_get_time() + 90000000;
            do {
                if (proto_bcast_command() == 'X' ||
                    !proto_bcast_snapshot(global, &guard, "rejected_hold", &observation)) goto finish;
                vTaskDelay(pdMS_TO_TICKS(250));
            } while (esp_timer_get_time() < hold_deadline);
            ok = true;
            goto finish;
        }
    }
    // Arm before the healthy baseline. No mining/clock configuration writes
    // after this point; only the regulator setpoint changes in the trial.
    if (atomic_load(&guard.tripped) || !MC3_start_active_spdlog_observation(180000)) goto finish;
    int64_t deadline = esp_timer_get_time() + 10000000;
    do {
        if (!proto_bcast_snapshot(global, &guard, "baseline", &observation)) goto finish;
        vTaskDelay(pdMS_TO_TICKS(250));
    } while (esp_timer_get_time() < deadline);
    if (observation.healthy_streak < 3) {
        ESP_LOGE(TAG, "BCAST fixed-PLL baseline lacks three healthy snapshots");
        goto finish;
    }
    while (proto_bcast_command()) {}
    ESP_LOGW(TAG, "BCAST READY: fixed PLL; wait up to 30s for G after scope arming");
    deadline = esp_timer_get_time() + 30000000;
    int command = 0;
    do {
        command = proto_bcast_command();
        if (command == 'G' || command == 'X' || atomic_load(&guard.tripped)) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    } while (esp_timer_get_time() < deadline);
    if (command != 'G' || atomic_load(&guard.tripped)) goto finish;
    ESP_LOGW(TAG, "BCAST TRIAL fixed_PLL=%u voltage=%u->%u; no work/reset/PLL writes", PROTO_FIXED_PROBE_MHZ, PROTO_FIXED_BASELINE_MV, PROTO_FIXED_TEST_MV);
    if (VCORE_set_voltage(global, (float)PROTO_FIXED_TEST_MV / 1000.0f) != ESP_OK || atomic_load(&guard.tripped)) goto finish;
    ESP_LOGW(TAG, "BCAST LAUNCHED; voltage changed; passive counters until scope verdict R; X aborts");
    deadline = esp_timer_get_time() + 15000000;
    command = 0;
    while (esp_timer_get_time() < deadline) {
        if (!proto_bcast_snapshot(global, &guard, "voltage_only", &observation)) goto finish;
        command = proto_bcast_command();
        if (command == 'R' || command == 'X') break;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (command != 'R' || atomic_load(&guard.tripped)) goto finish;
    ESP_LOGW(TAG, "BCAST SCOPE ACCEPTED; observe 90s with fixed PLL/work and continuous counters");
    deadline = esp_timer_get_time() + 90000000;
    do {
        if (proto_bcast_command() == 'X' || !proto_bcast_snapshot(global, &guard, "voltage_only", &observation)) goto finish;
        vTaskDelay(pdMS_TO_TICKS(250));
    } while (esp_timer_get_time() < deadline);
    ok = true;
finish:
    atomic_store(&guard.stop, true);
    VCORE_prepare_asic_enable(global);
    xSemaphoreTake(guard.stopped, portMAX_DELAY);
    vSemaphoreDelete(guard.stopped);
    ESP_LOGW(TAG, "BCAST END result=%s healthy_streak=%u max_PCB=%u/%u mV ASIC_power=OFF",
        ok && !atomic_load(&guard.tripped) ? "COMPLETE" : "ERROR", observation.healthy_streak,
        guard.max_lower_mv, guard.max_upper_mv);
    return;
power_off:
    VCORE_prepare_asic_enable(global);
    ESP_LOGE(TAG, "BCAST setup failed; ASIC power off");
}

static void run_proto_clock_delivery_probe(GlobalState *global)
{
    // The boot path limits this experiment to 960 mV and never enters the
    // normal frequency ramp. A single attempt always ends with ASIC power off.
    proto_delay_guard_t guard = {.global = global};
    atomic_init(&guard.stop, false);
    atomic_init(&guard.tripped, false);
    guard.stopped = xSemaphoreCreateBinary();
    if (guard.stopped == NULL) {
        VCORE_prepare_asic_enable(global);
        return;
    }
    if (xTaskCreate(proto_delay_guard_task, "clock_probe_guard", 4096,
            &guard, 6, NULL) != pdPASS) {
        vSemaphoreDelete(guard.stopped);
        VCORE_prepare_asic_enable(global);
        return;
    }
    ESP_LOGW(TAG, "CLOCK_PROBE BEGIN baseline=100 target=125 voltage=960; power off after one attempt");
    mc3_qualification_result_t baseline = {0};
    bool ok = !atomic_load(&guard.tripped) &&
        MC3_qualify_frequency(global, 100, &baseline) &&
        !atomic_load(&guard.tripped);
    if (ok) {
        bool mining_accepted = evaluate_proto_qualification(global,
            "CLOCK_PROBE baseline100", &baseline, NULL);
        // At 960 mV these chips execute work but return many hash errors.
        // This probe asks only whether clocks are withheld. Require continuing
        // work on every chip, count both pass/fail activity, and explicitly do
        // not interpret completion as mining qualification at this voltage.
        int16_t total = VCORE_get_regulator_voltage_mv(global);
        uint16_t lower = 0, upper = 0;
        ok = baseline.chip_count == 4 && total > 0 &&
            VCORE_get_domain_voltages_mv(global, total, &lower, &upper) == ESP_OK &&
            abs((int)lower - (int)upper) <= PROTO_MAX_DOMAIN_DELTA_MV;
        for (uint8_t chip = 0; ok && chip < baseline.chip_count; chip++) {
            double expected = 100.0 * global->DEVICE_CONFIG.family.asic.small_core_count / 1000.0;
            double activity = baseline.passed[chip] > 0
                ? baseline.hashrate_ghs[chip] *
                    ((double)baseline.passed[chip] + baseline.failed[chip]) / baseline.passed[chip]
                : 0;
            ok = activity >= expected * 0.90;
            ESP_LOGW(TAG, "CLOCK_PROBE BASELINE A%u pass_plus_fail_GHs=%.3f expected=%.3f",
                chip + 1, activity, expected);
        }
        ESP_LOGW(TAG, "CLOCK_PROBE BASELINE mining=%s clock_activity=%s",
            mining_accepted ? "PASS" : "REJECT", ok ? "PASS" : "REJECT");
    }
    if (ok && !atomic_load(&guard.tripped)) {
        ok = MC3_probe_clock_delivery(global, proto_clock_probe_abort, &guard);
    }
    // Power off before joining the guard, with no PLL rollback or auto retry.
    atomic_store(&guard.stop, true);
    VCORE_prepare_asic_enable(global);
    xSemaphoreTake(guard.stopped, portMAX_DELAY);
    vSemaphoreDelete(guard.stopped);
    ESP_LOGW(TAG, "CLOCK_PROBE END result=%s max_PCB=%u/%u mV ASIC_power=OFF",
        ok && !atomic_load(&guard.tripped) ? "COMPLETE" : "ERROR",
        guard.max_lower_mv, guard.max_upper_mv);
}

static bool run_proto_work_reset_delay_sweep(GlobalState *global)
{
    if (!PROTO_RUN_WORK_RESET_DELAY_SWEEP) return true;
    // RTC_DATA_ATTR is reloaded on software reset; only RTC_NOINIT_ATTR
    // preserves this state. Initialize it explicitly on a new bench run.
    esp_reset_reason_t reason = esp_reset_reason();
    if (proto_delay_progress_magic != 0x52445336 ||
        reason == ESP_RST_POWERON || reason == ESP_RST_EXT || reason == ESP_RST_USB) {
        proto_delay_trial_index = 0;
        proto_delay_trial_in_progress = false;
        proto_delay_progress_magic = 0x52445336;
    }
    static const uint32_t delays_ms[] = {1000, 500, 250, 100, 50, 10, 0};
    static const uint8_t order[] = {0, 2, 1, 3};
    const size_t count = sizeof(delays_ms) / sizeof(delays_ms[0]);
    if (proto_delay_trial_in_progress) {
        VCORE_prepare_asic_enable(global);
        ESP_LOGE(TAG, "RESET_DELAY previous trial interrupted; refusing automatic retry");
        return false;
    }
    if (proto_delay_trial_index >= count * PROTO_WORK_RESET_DELAY_REPEATS) {
        ESP_LOGW(TAG, "RESET_DELAY SWEEP COMPLETE; normal all-400 mining restored");
        return true;
    }
    if (global->DEVICE_CONFIG.board_version == NULL ||
        strcmp(global->DEVICE_CONFIG.board_version, "1103") != 0 ||
        global->DEVICE_CONFIG.family.asic_count != 4 ||
        global->POWER_MANAGEMENT_MODULE.actual_frequency != 400.0f ||
        global->POWER_MANAGEMENT_MODULE.frequency_value != 400.0f ||
        get_proto_target_core_voltage_mv(global) != 1160) {
        VCORE_prepare_asic_enable(global);
        ESP_LOGE(TAG, "RESET_DELAY requires 1103, four ASICs, 400 MHz and 1160 mV");
        return false;
    }

    unsigned repeat = proto_delay_trial_index / count + 1;
    uint32_t delay_ms = delays_ms[proto_delay_trial_index % count];
    proto_delay_guard_t guard = {.global = global};
    atomic_init(&guard.stop, false);
    atomic_init(&guard.tripped, false);
    guard.stopped = xSemaphoreCreateBinary();
    if (guard.stopped == NULL) {
        VCORE_prepare_asic_enable(global);
        return false;
    }
    if (xTaskCreate(proto_delay_guard_task, "reset_delay_guard", 4096,
            &guard, 6, NULL) != pdPASS) {
        vSemaphoreDelete(guard.stopped);
        VCORE_prepare_asic_enable(global);
        return false;
    }
    proto_delay_trial_in_progress = true;
    ESP_LOGW(TAG, "RESET_DELAY BEGIN repeat=%u delay_ms=%" PRIu32
        " baseline=400 test=425 voltage=1160; one full work reapplication, one SPDLOG arm; power off between trials",
        repeat, delay_ms);
    char stage[80];
    bool baseline_accepted = false;
    snprintf(stage, sizeof(stage), "RESET_DELAY-r%u-d%" PRIu32 "-baseline", repeat, delay_ms);
    bool ok = !atomic_load(&guard.tripped) &&
        MC3_reapply_active_qualification_work() &&
        capture_proto_delay_profile(global, stage, &baseline_accepted) && baseline_accepted;

    ESP_LOGW(TAG, "RESET_DELAY TRIAL repeat=%u delay_ms=%" PRIu32, repeat, delay_ms);
    int64_t start_us = esp_timer_get_time();
    for (size_t i = 0; ok && i < sizeof(order) / sizeof(order[0]); i++) {
        float actual = 0;
        ok = !atomic_load(&guard.tripped) &&
            MC3_set_chip_hash_frequency_live(global, order[i], 425, &actual) && actual == 425;
    }
    int64_t pll_done_us = esp_timer_get_time();
    // Never shorten the requested delay by tick rounding. No intentional
    // wait or log is inserted for the zero-delay control.
    while (ok && esp_timer_get_time() < pll_done_us + delay_ms * 1000LL) {
        vTaskDelay(1);
        ok = !atomic_load(&guard.tripped);
    }
    int64_t reset_call_us = esp_timer_get_time();
    ok = ok && !atomic_load(&guard.tripped) && MC3_reapply_active_qualification_work();
    ESP_LOGW(TAG, "RESET_DELAY TIMING repeat=%u delay_ms=%" PRIu32 " pll_ms=%.3f reset_call_gap_ms=%.3f reset_count=%u",
        repeat, delay_ms, (pll_done_us - start_us) / 1000.0,
        (reset_call_us - pll_done_us) / 1000.0, ok ? 1 : 0);

    // Arming counters itself applies SPD and resets the check FIFO. Do not
    // rearm at 425 MHz: that would add a late action strobe to the experiment.
    bool accepted = false;
    snprintf(stage, sizeof(stage), "RESET_DELAY-r%u-d%" PRIu32 "-w1", repeat, delay_ms);
    ok = ok && !atomic_load(&guard.tripped) &&
        capture_proto_delay_profile(global, stage, &accepted);
    ESP_LOGW(TAG, "RESET_DELAY RESULT repeat=%u delay_ms=%" PRIu32 " first=%s measurement=%s",
        repeat, delay_ms, ok ? (accepted ? "PASS" : "REJECT") : "NOT_MEASURED",
        ok ? "OK" : "ERROR");

    atomic_store(&guard.stop, true);
    xSemaphoreTake(guard.stopped, portMAX_DELAY);
    vSemaphoreDelete(guard.stopped);
    ok = ok && !atomic_load(&guard.tripped);
    // Both quiesced and live PLL rollback caused >1.37 V excursions in pilot
    // runs. Disable the entire rail first; never roll a failing profile back.
    ok = (VCORE_prepare_asic_enable(global) == ESP_OK) && ok;
    ESP_LOGW(TAG, "RESET_DELAY END result=%s max_PCB=%u/%u mV ASIC_power=OFF",
        ok ? "COMPLETE" : "ERROR", guard.max_lower_mv, guard.max_upper_mv);
    if (!ok) return false;
    proto_delay_trial_index++;
    proto_delay_trial_in_progress = false;
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGW(TAG, "RESET_DELAY REBOOT next_trial=%u; rail has been disabled for 1 second",
        proto_delay_trial_index);
    esp_restart();
    return false;
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

// Production startup for the two supported stacked MC3 boards. Pool producers
// remain blocked by ASIC_initalized until this entire sequence returns true.
typedef struct {
    GlobalState *global;
    atomic_bool stop;
    atomic_bool tripped;
    atomic_bool cycling;
    SemaphoreHandle_t stopped;
} proto_startup_guard_t;

static bool proto_startup_cancelled(const proto_startup_guard_t *guard)
{
    return atomic_load(&guard->tripped) || guard->global->SYSTEM_MODULE.hardware_fault ||
        guard->global->SYSTEM_MODULE.mining_paused;
}

static void proto_startup_guard_task(void *arg)
{
    proto_startup_guard_t *guard = arg;
    while (!atomic_load(&guard->stop)) {
        int16_t total = VCORE_get_regulator_voltage_mv_fresh(guard->global);
        bool valid = total >= 0;
        bool cycling = atomic_load(&guard->cycling);
        if (guard->global->DEVICE_CONFIG.domain_voltage_sense) {
            if (cycling) {
                uint16_t raw;
                valid = valid && ADC_get_domain_midpoint_mv(&raw) == ESP_OK;
            } else {
                uint16_t lower, upper;
                valid = valid && total > 0 && VCORE_get_domain_voltages_mv(
                    guard->global, total, &lower, &upper) == ESP_OK;
            }
        } else {
            valid = valid && (cycling || total > 0);
        }
        if (atomic_load(&guard->stop)) break;
        if (!valid || proto_startup_cancelled(guard)) {
            atomic_store(&guard->tripped, true);
            asic_prepare_power(guard->global);
            VCORE_set_voltage(guard->global, 0);
            ESP_LOGE(TAG, "PROTO STARTUP STOP rail_valid=%u total=%d cycle=%u", valid, total, cycling);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    xSemaphoreGive(guard->stopped);
    vTaskDelete(NULL);
}

static bool proto_startup_wait(proto_startup_guard_t *guard, uint32_t ms)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)ms * 1000;
    do {
        if (proto_startup_cancelled(guard)) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    } while (esp_timer_get_time() < deadline);
    return !proto_startup_cancelled(guard);
}

static bool proto_startup_temperature(proto_startup_guard_t *guard,
    int64_t warm_elapsed, float *coldest, float *hottest)
{
    proto_bcast_observation_t sample = {0};
    if (proto_startup_cancelled(guard) || MC3_read_temperatures(sample.temperature_c, 4) != 4) return false;
    *coldest = 100;
    *hottest = 0;
    for (unsigned chip = 0; chip < 4; chip++) {
        float t = sample.temperature_c[chip];
        if (!isfinite(t) || t <= 0 || t >= 70) return false;
        if (t < *coldest) *coldest = t;
        if (t > *hottest) *hottest = t;
        guard->global->POWER_MANAGEMENT_MODULE.asic_temps[chip] = t;
    }
    guard->global->POWER_MANAGEMENT_MODULE.asic_temp_count = 4;
    guard->global->POWER_MANAGEMENT_MODULE.chip_temp_avg = *hottest;
    proto_warm_fan(&sample, warm_elapsed);
    ESP_LOGI(TAG, "PROTO STARTUP TEMP freq=%.0f A1=%.2f A2=%.2f A3=%.2f A4=%.2f",
        guard->global->POWER_MANAGEMENT_MODULE.actual_frequency,
        sample.temperature_c[0], sample.temperature_c[1], sample.temperature_c[2], sample.temperature_c[3]);
    return !proto_startup_cancelled(guard);
}

static bool proto_startup_check_result(GlobalState *global, const mc3_qualification_result_t *result)
{
    if (result->chip_count != 4) return false;
    bool passed = true;
    for (unsigned chip = 0; chip < 4; chip++) {
        double ideal = result->chip_frequency_mhz[chip] *
            global->DEVICE_CONFIG.family.asic.small_core_count / 1000.0;
        uint64_t counts = (uint64_t)result->passed[chip] + result->failed[chip];
        double throughput = ideal > 0 ? 100.0 * result->hashrate_ghs[chip] / ideal : 0;
        double pass_rate = counts ? 100.0 * result->passed[chip] / counts : 0;
        if (!isfinite(throughput) || throughput < PROTO_MIN_CHIP_THROUGHPUT_PERCENT ||
            pass_rate < PROTO_MIN_PASS_RATE_PERCENT) passed = false;
        mc3_core_domain_summary_t domains = {0};
        if (!MC3_read_core_domain_summary(chip, &domains)) return false;
        for (unsigned domain = 0; domain < 4; domain++) {
            double share = domains.core_pass_sum
                ? 400.0 * domains.domain_pass[domain] / domains.core_pass_sum : 0;
            // Permit isolated weak cores; reject a substantially missing domain.
            if (share < PROTO_INTERNAL_DOMAIN_COLLAPSE_PERCENT) passed = false;
            ESP_LOGI(TAG, "PROTO STARTUP DOMAIN A%u D%u relative_share=%.2f zero=%u",
                chip + 1, domain, share, domains.domain_zero_cores[domain]);
        }
        ESP_LOGI(TAG, "PROTO STARTUP CHIP A%u frequency=%.0f GHs=%.2f throughput=%.2f pass_rate=%.2f",
            chip + 1, result->chip_frequency_mhz[chip], result->hashrate_ghs[chip], throughput, pass_rate);
    }
    ESP_LOGI(TAG, "PROTO STARTUP QUALIFY frequency=%.0f total=%.2f result=%s",
        result->frequency_mhz, result->total_hashrate_ghs, passed ? "PASS" : "FAIL");
    return passed;
}

static bool proto_startup_power_cycle(proto_startup_guard_t *guard)
{
    GlobalState *global = guard->global;
    bool ok = false;
    atomic_store(&guard->cycling, true);
    ESP_LOGI(TAG, "PROTO STARTUP POWER_CYCLE disabling Vcore and VDDIO");
    if (asic_prepare_power(global) != ESP_OK || VCORE_set_voltage(global, 0) != ESP_OK ||
        !proto_startup_wait(guard, 250)) goto done;
    int64_t deadline = esp_timer_get_time() + 2000000;
    int16_t total;
    do {
        total = VCORE_get_regulator_voltage_mv_fresh(global);
        if (total < 0) goto done;
        if (total <= 100) break;
        if (!proto_startup_wait(guard, 25)) goto done;
    } while (esp_timer_get_time() < deadline);
    if (total > 100 || proto_startup_cancelled(guard)) goto done;
    ESP_LOGI(TAG, "PROTO STARTUP DISCHARGED total=%d mV", total);
    if (enable_proto_power(global, 800) != ESP_OK) goto done;
    total = VCORE_get_regulator_voltage_mv_fresh(global);
    ok = total >= 1500 && total <= 1700 && !proto_startup_cancelled(guard);
done:
    atomic_store(&guard->cycling, false);
    return ok;
}

static bool proto_pool_startup_settings_valid(const GlobalState *global, uint16_t voltage_mv)
{
    float request = global->POWER_MANAGEMENT_MODULE.frequency_value;
    return !global->ASIC_initalized && global->DEVICE_CONFIG.family.asic_count == 4 &&
        isfinite(request) && request >= 100 && request <= 600 && voltage_mv >= 800 && voltage_mv <= 1160;
}

static bool run_proto_pool_startup(GlobalState *global, uint16_t voltage_mv)
{
    float request = global->POWER_MANAGEMENT_MODULE.frequency_value;
    if (!proto_pool_startup_settings_valid(global, voltage_mv)) {
        ESP_LOGE(TAG, "PROTO STARTUP requires four ASICs, 100..600 MHz and 800..1160 mV/half");
        return false;
    }
    unsigned target = (unsigned)(request / 25.0f + 0.5f) * 25;
    unsigned warm_frequency = target < 300 ? target : 300;
    proto_startup_guard_t guard = {.global = global};
    atomic_init(&guard.stop, false);
    atomic_init(&guard.tripped, false);
    atomic_init(&guard.cycling, false);
    guard.stopped = xSemaphoreCreateBinary();
    if (!guard.stopped) return false;
    if (xTaskCreate(proto_startup_guard_task, "startup_guard", 4096, &guard, 6, NULL) != pdPASS) {
        vSemaphoreDelete(guard.stopped);
        return false;
    }
    bool ok = false;
    float coldest, hottest;
    mc3_qualification_result_t result = {0};
    global->SYSTEM_MODULE.asic_status = "Warming ASICs";
    ESP_LOGI(TAG, "PROTO STARTUP BEGIN target=%u voltage=%u; warmup=%u then full supply cycle; sampled voltage report-only",
        target, voltage_mv, warm_frequency);
    for (unsigned f = 100; f <= warm_frequency; f += 25) {
        FAN_CONTROLLER_set_bench_percent(20);
        if (proto_startup_cancelled(&guard) || !MC3_qualify_frequency(global, f, &result) ||
            !proto_startup_check_result(global, &result) ||
            !proto_startup_temperature(&guard, 0, &coldest, &hottest)) goto done;
    }
    if (target > 300) {
        int64_t start = esp_timer_get_time(), ready_since = 0;
        bool warm = false;
        while (esp_timer_get_time() - start < 600000000) {
            if (!proto_startup_temperature(&guard, esp_timer_get_time() - start, &coldest, &hottest)) goto done;
            if (hottest >= 54.5f && hottest <= 60 && coldest >= 52) {
                if (!ready_since) ready_since = esp_timer_get_time();
                if (esp_timer_get_time() - ready_since >= 10000000) {warm = true; break;}
            } else ready_since = 0;
            if (!proto_startup_wait(&guard, 1000)) goto done;
        }
        if (!warm) {ESP_LOGE(TAG, "PROTO STARTUP warmup timed out"); goto done;}
        if (!MC3_measure_active_frequency_profile(global, &result) ||
            !proto_startup_check_result(global, &result)) goto done;
        global->SYSTEM_MODULE.asic_status = "Starting warm ASICs";
        if (!proto_startup_power_cycle(&guard)) goto done;
        SERIAL_set_baud(UART_FREQ);
        SERIAL_clear_buffer();
        if (asic_reset() != ESP_OK || proto_startup_cancelled(&guard)) goto done;
        clear_asic_chain_error();
        if (MC3_init(global) != 4 || global->POWER_MANAGEMENT_MODULE.actual_frequency != 100) goto done;
        SERIAL_clear_buffer();
        ESP_LOGI(TAG, "PROTO STARTUP REINITIALIZED four ASICs; no work since supply cycle");
        for (unsigned mv = 800; mv < voltage_mv;) {
            mv += voltage_mv - mv < 10 ? voltage_mv - mv : 10;
            if (set_proto_core_voltage(global, mv) != ESP_OK || !proto_startup_wait(&guard, 150)) goto done;
        }
        // Do not trigger thermal conversions during the bounded unloaded ramp.
        FAN_CONTROLLER_set_bench_percent(70);
        int64_t deadline = esp_timer_get_time() + 20000000;
        if (!proto_startup_wait(&guard, 500)) goto done;
        for (unsigned f = 125; f <= target; f += 25) {
            if (proto_startup_cancelled(&guard) || esp_timer_get_time() >= deadline ||
                MC3_send_hash_frequency(f) != f) goto done;
            global->POWER_MANAGEMENT_MODULE.actual_frequency = f;
            uint32_t settle = f == target ? 1500 : f > 500 ? 1000 : 500;
            ESP_LOGI(TAG, "PROTO STARTUP PLL frequency=%u settle_ms=%u; no work sent", f, settle);
            FAN_CONTROLLER_set_bench_percent(70);
            if (!proto_startup_wait(&guard, settle)) goto done;
        }
        if (esp_timer_get_time() >= deadline ||
            !proto_startup_temperature(&guard, 0, &coldest, &hottest)) goto done;
        ESP_LOGI(TAG, "PROTO STARTUP FIRST_WORK frequency=%u", target);
        if (!MC3_start_qualification_work(global) ||
            !MC3_measure_active_frequency_profile(global, &result) ||
            !proto_startup_check_result(global, &result) ||
            !proto_startup_temperature(&guard, 0, &coldest, &hottest)) goto done;
    }
    ok = !proto_startup_cancelled(&guard);
done:
    atomic_store(&guard.stop, true);
    xSemaphoreTake(guard.stopped, portMAX_DELAY);
    vSemaphoreDelete(guard.stopped);
    ok = ok && !proto_startup_cancelled(&guard);
    FAN_CONTROLLER_set_bench_percent(-1);
    if (ok) {
        global->POWER_MANAGEMENT_MODULE.expected_hashrate =
            global->POWER_MANAGEMENT_MODULE.actual_frequency *
            global->DEVICE_CONFIG.family.asic.small_core_count * 4 / 1000.0f;
        global->SYSTEM_MODULE.asic_status = "Ready for pool work";
        ESP_LOGI(TAG, "PROTO STARTUP READY frequency=%.0f; handing off to pool work",
            global->POWER_MANAGEMENT_MODULE.actual_frequency);
    } else {
        global->SYSTEM_MODULE.asic_status = "Warm ASIC startup failed";
        asic_prepare_power(global);
        VCORE_set_voltage(global, 0);
        ESP_LOGE(TAG, "PROTO STARTUP FAILED; ASIC power off, settings retained");
    }
    return ok;
}

static uint8_t asic_initialize_impl(GlobalState *GLOBAL_STATE, asic_init_mode_t mode, uint32_t stabilization_delay_ms)
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
    if ((PROTO_RUN_CLOCK_DELIVERY_PROBE || PROTO_RUN_BROADCAST_RECOVERY_PROBE || PROTO_RUN_FIXED_PLL_VOLTAGE_PROBE || PROTO_RUN_WARM_RAMP_PROBE) && is_proto_mc3) {
        const char *probe_board = PROTO_RUN_WARM_RAMP_PROBE ? PROTO_WARM_BOARD_VERSION : "1103";
        if ((strcmp(probe_board, "1103") != 0 && strcmp(probe_board, "1102") != 0) ||
            GLOBAL_STATE->DEVICE_CONFIG.board_version == NULL ||
            strcmp(GLOBAL_STATE->DEVICE_CONFIG.board_version, probe_board) != 0 ||
            GLOBAL_STATE->DEVICE_CONFIG.family.asic_count != 4 ||
            GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
            proto_probe_power_off(GLOBAL_STATE);
            ESP_LOGE(TAG, "Probe board/chip/self-test mismatch; expected board=%s", probe_board);
            return 0;
        }
        target_core_voltage_mv = PROTO_RUN_WARM_RAMP_PROBE ? PROTO_WARM_VOLTAGE_MV : PROTO_RUN_FIXED_PLL_VOLTAGE_PROBE ? PROTO_FIXED_BASELINE_MV :
            (PROTO_RUN_BROADCAST_RECOVERY_PROBE ? PROTO_BROADCAST_PROBE_VOLTAGE_MV : 960);
    }
    bool supported_warm_startup = is_proto_mc3 && !GLOBAL_STATE->SELF_TEST_MODULE.is_active &&
        GLOBAL_STATE->DEVICE_CONFIG.board_version != NULL &&
        (strcmp(GLOBAL_STATE->DEVICE_CONFIG.board_version, "1103") == 0 ||
         strcmp(GLOBAL_STATE->DEVICE_CONFIG.board_version, "1102") == 0);
    if (supported_warm_startup && !PROTO_RUN_WARM_RAMP_PROBE &&
        !PROTO_RUN_FIXED_PLL_VOLTAGE_PROBE && !PROTO_RUN_BROADCAST_RECOVERY_PROBE &&
        !PROTO_RUN_CLOCK_DELIVERY_PROBE &&
        !proto_pool_startup_settings_valid(GLOBAL_STATE, target_core_voltage_mv)) {
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "Supported startup: 100-600 MHz, 800-1160 mV";
        ESP_LOGE(TAG, "Proto startup settings outside supported range; ASIC power stays off");
        return 0;
    }
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
            if (PROTO_RUN_WARM_RAMP_PROBE) {
                ESP_LOGW(TAG, "WARM STARTUP requested_total=%u mV measured_total=%d mV",
                    2 * init_core_voltage_mv, VCORE_get_regulator_voltage_mv_fresh(GLOBAL_STATE));
            }
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
        if (is_proto_mc3 && PROTO_RUN_WARM_RAMP_PROBE) {
            asic_prepare_power(GLOBAL_STATE);
            esp_err_t off_err = VCORE_set_voltage(GLOBAL_STATE, 0);
            ESP_LOGE(TAG, "WARM END startup chain failure; PMBus shutdown=%s", esp_err_to_name(off_err));
        }
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

    if (PROTO_RUN_WARM_RAMP_PROBE && is_proto_mc3) {
        run_proto_warm_ramp_probe(GLOBAL_STATE);
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "Warm ramp probe ended; ASIC power off";
        return 0;
    }

    if (PROTO_RUN_FIXED_PLL_VOLTAGE_PROBE && is_proto_mc3) {
        run_proto_fixed_pll_voltage_probe(GLOBAL_STATE);
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "Fixed-PLL voltage probe ended; ASIC power off";
        return 0;
    }

    if (PROTO_RUN_BROADCAST_RECOVERY_PROBE && is_proto_mc3) {
        run_proto_broadcast_recovery_probe(GLOBAL_STATE);
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "Broadcast probe ended; ASIC power off";
        return 0;
    }

    if (PROTO_RUN_CLOCK_DELIVERY_PROBE && is_proto_mc3) {
        run_proto_clock_delivery_probe(GLOBAL_STATE);
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "Clock probe ended; ASIC power off";
        return 0;
    }

    if (supported_warm_startup) {
        if (!run_proto_pool_startup(GLOBAL_STATE, target_core_voltage_mv)) return 0;
        MC3_prepare_pool_work(GLOBAL_STATE);
    } else if (is_proto_mc3 && !GLOBAL_STATE->SELF_TEST_MODULE.is_active) {
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
        if (!run_proto_work_reset_delay_sweep(GLOBAL_STATE)) {
            GLOBAL_STATE->SYSTEM_MODULE.asic_status = "MC3 work-reset delay sweep failed";
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

uint8_t asic_initialize(GlobalState *global, asic_init_mode_t mode, uint32_t stabilization_delay_ms)
{
    global->ASIC_initalized = false;
    uint8_t chips = asic_initialize_impl(global, mode, stabilization_delay_ms);
    if (chips == 0 && global->DEVICE_CONFIG.family.id == PROTO) {
        FAN_CONTROLLER_set_bench_percent(-1);
        asic_prepare_power(global);
        VCORE_set_voltage(global, 0);
        global->POWER_MANAGEMENT_MODULE.expected_hashrate = 0;
    }
    return chips;
}
