#include "asic_init.h"
#include <stdio.h>
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

static bool qualify_proto_frequency_step(GlobalState *GLOBAL_STATE, float frequency_mhz,
                                         mc3_qualification_result_t *result)
{
    if (!MC3_qualify_frequency(GLOBAL_STATE, frequency_mhz, result)) {
        ESP_LOGE(TAG, "Proto qualification communication failed at %.0f MHz", frequency_mhz);
        return false;
    }

    float min_pass_rate_percent = 100.0f;
    bool counters_valid = result->chip_count > 0;
    for (uint8_t chip_id = 0; chip_id < result->chip_count; chip_id++) {
        uint64_t total = (uint64_t)result->passed[chip_id] + result->failed[chip_id];
        float pass_rate_percent = total > 0
            ? ((float)result->passed[chip_id] * 100.0f) / (float)total
            : 0.0f;
        if (pass_rate_percent < min_pass_rate_percent) {
            min_pass_rate_percent = pass_rate_percent;
        }
        counters_valid = counters_valid && total > 0;
        ESP_LOGI(TAG,
            "Proto qualification %.0f MHz chip%u: %.2f GH/s pass/fail=%lu/%lu pass_rate=%.2f%%",
            result->frequency_mhz, chip_id, result->hashrate_ghs[chip_id],
            (unsigned long)result->passed[chip_id],
            (unsigned long)result->failed[chip_id], pass_rate_percent);
    }

    float expected_hashrate_ghs = result->frequency_mhz *
        GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count * result->chip_count / 1000.0f;
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
            "Proto qualification %.0f MHz domains: lower=%u upper=%u delta=%+d mV total=%d mV TPS current=%.2f A",
            result->frequency_mhz, lower_domain_mv, upper_domain_mv,
            domain_delta_mv, regulator_voltage_mv, regulator_current_a);
    } else if (!domains_valid) {
        ESP_LOGE(TAG,
            "Proto qualification %.0f MHz: domain measurement unavailable; TPS current=%.2f A",
            result->frequency_mhz, regulator_current_a);
    }

    bool accepted = counters_valid && domains_valid &&
        min_pass_rate_percent >= PROTO_MIN_PASS_RATE_PERCENT &&
        throughput_percent >= PROTO_MIN_THROUGHPUT_PERCENT &&
        absolute_domain_delta_mv <= PROTO_MAX_DOMAIN_DELTA_MV;
    ESP_LOGI(TAG,
        "Proto qualification %.0f MHz: total=%.2f/%.2f GH/s throughput=%.2f%% min_pass=%.2f%% balance=%d mV => %s",
        result->frequency_mhz, result->total_hashrate_ghs, expected_hashrate_ghs,
        throughput_percent, min_pass_rate_percent, absolute_domain_delta_mv,
        accepted ? "PASS" : "REJECT");
    return accepted;
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

    ESP_LOGI(TAG,
        "Starting Proto qualification ramp from %u to %u MHz in %u MHz steps",
        MC3_STARTUP_FREQUENCY_MHZ, target_frequency_mhz, PROTO_FREQUENCY_STEP_MHZ);

    uint16_t last_good_frequency_mhz = 0;
    for (uint16_t frequency_mhz = MC3_STARTUP_FREQUENCY_MHZ;
         frequency_mhz <= target_frequency_mhz;
         frequency_mhz += PROTO_FREQUENCY_STEP_MHZ) {
        mc3_qualification_result_t result = {0};
        if (!qualify_proto_frequency_step(GLOBAL_STATE, frequency_mhz, &result)) {
            if (last_good_frequency_mhz == 0) {
                ESP_LOGE(TAG, "Proto failed its baseline %u MHz qualification",
                    MC3_STARTUP_FREQUENCY_MHZ);
                return false;
            }

            ESP_LOGW(TAG,
                "Proto rejected %u MHz; rolling back to last-qualified %u MHz",
                frequency_mhz, last_good_frequency_mhz);
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
        if (!qualify_proto_frequency_ramp(GLOBAL_STATE)) {
            GLOBAL_STATE->SYSTEM_MODULE.asic_status = "MC3 startup qualification failed";
            ESP_LOGE(TAG, "MC3 startup qualification failed");
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
