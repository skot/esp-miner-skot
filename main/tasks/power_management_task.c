#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "nvs_config.h"
#include "vcore.h"
#include "thermal.h"
#include "power.h"
#include "asic.h"
#include "mc3.h"
#include "utils.h"
#include "asic_init.h"
#include "asic_reset.h"
#include "TPS546.h"
#include "driver/uart.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

#define POLL_RATE 100
#define MAX_TEMP 90.0
#define THROTTLE_TEMP 75.0
#define SAFE_TEMP 45.0

#define VOLTAGE_START_THROTTLE 4900
#define VOLTAGE_MIN_THROTTLE 3500
#define VOLTAGE_RANGE (VOLTAGE_START_THROTTLE - VOLTAGE_MIN_THROTTLE)

#define TPS546_THROTTLE_TEMP 105.0
#define TPS546_MAX_TEMP 145.0

#define ASIC_REDUCTION 100.0

#define PROTO_DOMAIN_LOG_INTERVAL_MS 5000

static const char * TAG = "power_management";

static void log_proto_frequency_step_telemetry(void *context, float actual_frequency)
{
    GlobalState *GLOBAL_STATE = (GlobalState *)context;
    int16_t regulator_voltage_mv = VCORE_get_regulator_voltage_mv(GLOBAL_STATE);
    float regulator_current_a = TPS546_get_iout();
    uint16_t lower_domain_mv = 0;
    uint16_t upper_domain_mv = 0;

    if (regulator_voltage_mv <= 0 ||
        VCORE_get_domain_voltages_mv(GLOBAL_STATE,
            (uint16_t)regulator_voltage_mv,
            &lower_domain_mv, &upper_domain_mv) != ESP_OK) {
        ESP_LOGW(TAG,
            "Proto PLL step %.0f MHz: domain voltage unavailable; TPS current=%.2f A",
            actual_frequency, regulator_current_a);
        return;
    }

    ESP_LOGI(TAG,
        "Proto PLL step %.0f MHz: lower=%u upper=%u delta=%+d mV total=%d mV TPS current=%.2f A",
        actual_frequency,
        lower_domain_mv,
        upper_domain_mv,
        (int)upper_domain_mv - (int)lower_domain_mv,
        regulator_voltage_mv,
        regulator_current_a);
}

static void mining_stop(GlobalState * GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Stopping mining");

    if (GLOBAL_STATE->DEVICE_CONFIG.family.id == PROTO) {
        // Stop producers first, then remove the auxiliary rails before VDD.
        // Proto restarts re-run the complete ordered power/qualification path.
        GLOBAL_STATE->ASIC_initalized = false;
        GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate = 0;
        vTaskDelay(100 / portTICK_PERIOD_MS);
        asic_prepare_power(GLOBAL_STATE);
        VCORE_set_voltage(GLOBAL_STATE, 0.0f);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        uart_flush(UART_NUM_1);
        ESP_LOGI(TAG, "Mining stopped");
        return;
    }

    // Wind frequency down to 50 MHz before cutting power. This also updates
    // the transition tracker so the ramp starts from 50 MHz on next start,
    // rather than the stale pre-reset frequency.
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value = 50;
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate = 0;

    ASIC_set_frequency(GLOBAL_STATE);
    ASIC_set_nonce_space(GLOBAL_STATE);

    // Cut ASIC power and hold in reset
    VCORE_set_voltage(GLOBAL_STATE, 0.0f);
    asic_hold_reset_low();

    // Mark uninitialized immediately so tasks stop issuing UART commands
    GLOBAL_STATE->ASIC_initalized = false;

    // Give tasks time to complete any in-progress UART operation
    vTaskDelay(500 / portTICK_PERIOD_MS);

    // Flush any stale data from the UART buffers
    uart_flush(UART_NUM_1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Mining stopped");
}

static uint8_t mining_start(GlobalState * GLOBAL_STATE)
{
    ESP_LOGI(TAG, "Starting mining");

    // Proto ASIC initialization owns its reset -> VDDIO -> Vcore sequence.
    if (GLOBAL_STATE->DEVICE_CONFIG.family.id != PROTO) {
        uint16_t voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
        VCORE_set_voltage(GLOBAL_STATE, (double) voltage / 1000.0);

        // Wait for voltage to stabilize before touching the ASIC
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    // Clear any accumulated UART garbage before init
    uart_flush(UART_NUM_1);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    POWER_MANAGEMENT_init_frequency(GLOBAL_STATE);
    // Stabilization delay of 2000ms prevents race conditions where tasks are
    // just starting to use the ASIC while power management tries to change frequency
    uint8_t chip_count = asic_initialize(GLOBAL_STATE, ASIC_INIT_RECOVERY, 2000);

    if (chip_count > 0) {
        ESP_LOGI(TAG, "Mining started successfully (%d chip(s))", chip_count);
    } else {
        ESP_LOGE(TAG, "Mining start failed - ASIC not detected");
    }

    return chip_count;
}

static float expected_hashrate(GlobalState * GLOBAL_STATE)
{
    return GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value * GLOBAL_STATE->DEVICE_CONFIG.family.asic.small_core_count * GLOBAL_STATE->DEVICE_CONFIG.family.asic_count / 1000.0;
}

static void update_asic_telemetry(GlobalState *GLOBAL_STATE)
{
    PowerManagementModule *power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    if (GLOBAL_STATE->DEVICE_CONFIG.family.asic.id == MC3) {
        if (!GLOBAL_STATE->ASIC_initalized) {
            return;
        }

        float temperatures[MAX_ASIC_TEMPS] = {0};
        uint8_t count = MC3_read_temperatures(temperatures, MAX_ASIC_TEMPS);
        if (count > 0) {
            for (uint8_t i = 0; i < count; i++) {
                if (temperatures[i] > 0.0f) {
                    power_management->asic_temps[i] = temperatures[i];
                }
            }
            power_management->asic_temp_count = count;
        }

        float max_temp = -1.0f;
        for (uint8_t i = 0; i < power_management->asic_temp_count; i++) {
            if (power_management->asic_temps[i] > max_temp) {
                max_temp = power_management->asic_temps[i];
            }
        }

        power_management->chip_temp_avg = max_temp;
        power_management->chip_temp2_avg = -1.0f;

        float voltages[MAX_ASIC_TEMPS] = {0};
        uint8_t voltage_count = MC3_read_vdd_voltages(voltages, MAX_ASIC_TEMPS);
        if (voltage_count > 0) {
            for (uint8_t i = 0; i < voltage_count; i++) {
                if (voltages[i] > 0.0f) {
                    power_management->asic_voltages[i] = voltages[i];
                }
            }
            power_management->asic_voltage_count = voltage_count;
        }

        float voltage_total = 0.0f;
        uint8_t valid_voltage_count = 0;
        for (uint8_t i = 0; i < power_management->asic_voltage_count; i++) {
            if (power_management->asic_voltages[i] > 0.0f) {
                voltage_total += power_management->asic_voltages[i];
                valid_voltage_count++;
            }
        }
        if (valid_voltage_count > 0) {
            power_management->core_voltage = voltage_total / valid_voltage_count;
        }

        return;
    }

    memset(power_management->asic_temps, 0, sizeof(power_management->asic_temps));
    power_management->asic_temp_count = 0;
    memset(power_management->asic_voltages, 0, sizeof(power_management->asic_voltages));
    power_management->asic_voltage_count = 0;
    power_management->chip_temp_avg = Thermal_get_chip_temp(GLOBAL_STATE);
    power_management->chip_temp2_avg = Thermal_get_chip_temp2(GLOBAL_STATE);
}

static void update_domain_voltage_telemetry(GlobalState *GLOBAL_STATE)
{
    PowerManagementModule *power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;

    if (!GLOBAL_STATE->DEVICE_CONFIG.domain_voltage_sense ||
        power_management->regulator_voltage <= 0.0f) {
        memset(power_management->domain_voltages, 0,
            sizeof(power_management->domain_voltages));
        power_management->domain_voltage_count = 0;
        return;
    }

    uint16_t lower_domain_mv = 0;
    uint16_t upper_domain_mv = 0;
    esp_err_t err = VCORE_get_domain_voltages_mv(
        GLOBAL_STATE,
        (uint16_t)(power_management->regulator_voltage + 0.5f),
        &lower_domain_mv,
        &upper_domain_mv);
    if (err != ESP_OK) {
        power_management->domain_voltage_count = 0;
        return;
    }

    power_management->domain_voltages[0] = lower_domain_mv;
    power_management->domain_voltages[1] = upper_domain_mv;
    power_management->domain_voltage_count = 2;
}

void POWER_MANAGEMENT_init_frequency(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    float frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);

    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.frequency_value = frequency;
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.actual_frequency = 50.0;
    GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate = expected_hashrate(GLOBAL_STATE);

    if (GLOBAL_STATE->DEVICE_CONFIG.domain_voltage_sense &&
        GLOBAL_STATE->DEVICE_CONFIG.family.asic.id == MC3) {
        MC3_set_frequency_step_callback(log_proto_frequency_step_telemetry);
    } else {
        MC3_set_frequency_step_callback(NULL);
    }
    
    char expected_hashrate_str[16] = {0};
    suffixString(GLOBAL_STATE->POWER_MANAGEMENT_MODULE.expected_hashrate * 1e6, expected_hashrate_str, sizeof(expected_hashrate_str), 0);
    ESP_LOGI(TAG, "ASIC Frequency: %g MHz, Expected hashrate: %sH/s", frequency, expected_hashrate_str);
}

void POWER_MANAGEMENT_task(void * pvParameters)
{
    ESP_LOGI(TAG, "Starting");

    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    PowerManagementModule * power_management = &GLOBAL_STATE->POWER_MANAGEMENT_MODULE;
    SystemModule * sys_module = &GLOBAL_STATE->SYSTEM_MODULE;

    POWER_MANAGEMENT_init_frequency(GLOBAL_STATE);
    
    float last_asic_frequency = power_management->frequency_value;

    vTaskDelay(500 / portTICK_PERIOD_MS);
    bool is_proto_mc3 =
        GLOBAL_STATE->DEVICE_CONFIG.family.id == PROTO &&
        GLOBAL_STATE->DEVICE_CONFIG.family.asic.id == MC3;
    uint16_t last_core_voltage = is_proto_mc3
        ? nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE)
        : 0;

    uint16_t last_known_asic_voltage = 0;
    float last_known_asic_frequency = 0.0;
    bool is_paused = false;
    int64_t proto_domain_last_log_us = 0;

    while (1) {
        if (GLOBAL_STATE->SELF_TEST_MODULE.is_finished) {
            ESP_LOGI(TAG, "Stopped");
            vTaskDelete(NULL);
            return;
        }

        power_management->voltage = Power_get_input_voltage(GLOBAL_STATE);
        Power_get_output(GLOBAL_STATE, &power_management->power, &power_management->current);
        if (GLOBAL_STATE->DEVICE_CONFIG.TPS546) {
            power_management->regulator_voltage = VCORE_get_regulator_voltage_mv(GLOBAL_STATE);
            power_management->core_voltage = power_management->regulator_voltage /
                GLOBAL_STATE->DEVICE_CONFIG.family.voltage_domains;
        } else {
            power_management->regulator_voltage = 0.0f;
            power_management->core_voltage = VCORE_get_voltage_mv(GLOBAL_STATE);
        }

        update_domain_voltage_telemetry(GLOBAL_STATE);
        update_asic_telemetry(GLOBAL_STATE);

        if (GLOBAL_STATE->ASIC_initalized &&
            power_management->domain_voltage_count == 2) {
            int64_t now_us = esp_timer_get_time();
            if (proto_domain_last_log_us == 0 ||
                now_us - proto_domain_last_log_us >=
                    (int64_t)PROTO_DOMAIN_LOG_INTERVAL_MS * 1000) {
                float lower_domain_mv = power_management->domain_voltages[0];
                float upper_domain_mv = power_management->domain_voltages[1];
                ESP_LOGI(TAG,
                    "Proto PCB domains: lower=%.1f upper=%.1f delta=%+.1f mV total=%.1f mV",
                    lower_domain_mv,
                    upper_domain_mv,
                    upper_domain_mv - lower_domain_mv,
                    power_management->regulator_voltage);
                proto_domain_last_log_us = now_us;
            }
        }

        power_management->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);
        // User pause, hardware fault, or all pools unreachable
        bool wants_stop = sys_module->mining_paused || sys_module->hardware_fault || sys_module->pools_unavailable;
        if (wants_stop && !is_paused) {
            mining_stop(GLOBAL_STATE);
            is_paused = true;
        } else if (!wants_stop && is_paused) {
            mining_start(GLOBAL_STATE);
            is_paused = false;
        }

        // If we've paused or have a hardware fault, skip doing anything else
        if (is_paused || sys_module->hardware_fault) {
            vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
            continue;
        }

        bool asic_overheat =
            power_management->chip_temp_avg > THROTTLE_TEMP
            || power_management->chip_temp2_avg > THROTTLE_TEMP;

        if ((power_management->vr_temp > TPS546_THROTTLE_TEMP || asic_overheat) && (power_management->frequency_value > 50 || power_management->voltage > 1000)) {
            if (power_management->chip_temp2_avg > 0) {
                ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC1: %fC ASIC2: %fC", power_management->vr_temp, power_management->chip_temp_avg, power_management->chip_temp2_avg);
            } else {
                ESP_LOGE(TAG, "OVERHEAT! VR: %fC ASIC: %fC", power_management->vr_temp, power_management->chip_temp_avg);
            }

            last_known_asic_voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
            last_known_asic_frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);
            nvs_config_set_bool(NVS_CONFIG_AUTO_FAN_SPEED, false);
            nvs_config_set_u16(NVS_CONFIG_MANUAL_FAN_SPEED, 100);
            nvs_config_set_bool(NVS_CONFIG_OVERHEAT_MODE, true);
            ESP_LOGW(TAG, "Entering safe mode due to overheat condition. System operation halted.");
            mining_stop(GLOBAL_STATE);
            
            // Note: ASIC temperature readings are invalid when ASIC is powered down (returns -1)
            // For 600-series boards that use ASIC thermal diode, we rely on VR temp and fixed cooling time
            // For boards with EMC internal temp sensor, readings remain valid
            bool asic_temp_valid = GLOBAL_STATE->DEVICE_CONFIG.emc_internal_temp;
            int cooling_cycles = 0;
            const int MIN_COOLING_CYCLES = 6; // Minimum 30 seconds cooling
            
            while (cooling_cycles < MIN_COOLING_CYCLES || power_management->vr_temp > TPS546_THROTTLE_TEMP - 10) {
                vTaskDelay(5000 / portTICK_PERIOD_MS); // Wait 5 seconds
                cooling_cycles++;
                
                power_management->vr_temp = Power_get_vreg_temp(GLOBAL_STATE);
                
                // Only check ASIC temps if they're valid (not using ASIC thermal diode)
                if (asic_temp_valid) {
                    update_asic_telemetry(GLOBAL_STATE);
                    ESP_LOGW(TAG, "Safe mode active (cycle %d) - VR: %.1f°C ASIC1: %.1f°C ASIC2: %.1f°C",
                             cooling_cycles, power_management->vr_temp, power_management->chip_temp_avg, power_management->chip_temp2_avg);
                    
                    // Continue if ASIC temps still too high
                    if (power_management->chip_temp_avg >  SAFE_TEMP || power_management->chip_temp2_avg > SAFE_TEMP) {
                        cooling_cycles = 0; // Reset cycle count if still hot
                    }
                } else {
                    // For boards using ASIC thermal diode (600 series), rely on VR temp and time
                    ESP_LOGW(TAG, "Safe mode active (cycle %d/%d) - VR: %.1f°C (ASIC temps unavailable while powered down)",
                             cooling_cycles, MIN_COOLING_CYCLES, power_management->vr_temp);
                }
            }
            ESP_LOGI(TAG, "Temperature normalized after %d cooling cycles. Reinitializing ASIC...", cooling_cycles);
            
            uint16_t reduced_voltage = last_known_asic_voltage > ASIC_REDUCTION ? last_known_asic_voltage - ASIC_REDUCTION : 1000;
            float reduced_asic_frequency = last_known_asic_frequency > ASIC_REDUCTION ? last_known_asic_frequency - ASIC_REDUCTION : 400.0;
            
            nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, reduced_voltage);
            nvs_config_set_float(NVS_CONFIG_ASIC_FREQUENCY, reduced_asic_frequency);
            
            ESP_LOGI(TAG, "Restoring at reduced settings: %umV (was %umV), %.0f MHz (was %.0f MHz)",
                     reduced_voltage, last_known_asic_voltage, reduced_asic_frequency, last_known_asic_frequency);

            uint8_t chip_count = mining_start(GLOBAL_STATE);

            if (chip_count > 0) {
                // Frequency reduction will now be applied by normal power management loop
                nvs_config_set_bool(NVS_CONFIG_OVERHEAT_MODE, false);
                ESP_LOGI(TAG, "Resuming normal operation. Reduced frequency (%.0f MHz) will be applied automatically.", reduced_asic_frequency);
            }
        }

        uint16_t core_voltage = GLOBAL_STATE->SELF_TEST_MODULE.is_active
                                 ? GLOBAL_STATE->DEVICE_CONFIG.family.asic.default_voltage_mv
                                 : nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
        float asic_frequency = GLOBAL_STATE->SELF_TEST_MODULE.is_active
                                 ? GLOBAL_STATE-> DEVICE_CONFIG.family.asic.default_frequency_mhz
                                 : nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);

        bool proto_power_sequence_active =
            GLOBAL_STATE->DEVICE_CONFIG.family.id == PROTO && !GLOBAL_STATE->ASIC_initalized;
        bool voltage_changed = core_voltage != last_core_voltage;
        bool frequency_changed = asic_frequency != last_asic_frequency;

        if (is_proto_mc3 && GLOBAL_STATE->ASIC_initalized &&
            !GLOBAL_STATE->SELF_TEST_MODULE.is_active &&
            (voltage_changed || frequency_changed)) {
            ESP_LOGI(TAG,
                "Proto operating point changed to %u mV/domain, %.0f MHz; restarting ordered qualification",
                core_voltage, asic_frequency);
            last_core_voltage = core_voltage;
            last_asic_frequency = asic_frequency;
            mining_stop(GLOBAL_STATE);
            if (mining_start(GLOBAL_STATE) == 0) {
                ESP_LOGE(TAG, "Proto operating-point qualification restart failed");
            }
            last_core_voltage = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
            last_asic_frequency = nvs_config_get_float(NVS_CONFIG_ASIC_FREQUENCY);
            continue;
        }

        if (voltage_changed && !proto_power_sequence_active) {
            ESP_LOGI(TAG, "setting new vcore voltage to %umV", core_voltage);
            VCORE_set_voltage(GLOBAL_STATE, (double) core_voltage / 1000.0);
            last_core_voltage = core_voltage;
        }

        if (frequency_changed) {
            ESP_LOGI(TAG, "New ASIC frequency requested: %g MHz (current: %g MHz)", asic_frequency, last_asic_frequency);
            
            power_management->frequency_value = asic_frequency;
            power_management->expected_hashrate = expected_hashrate(GLOBAL_STATE);
            last_asic_frequency = asic_frequency;
        }

        if (frequency_changed && GLOBAL_STATE->ASIC_initalized) {
            ASIC_set_frequency(GLOBAL_STATE);
            ASIC_set_nonce_space(GLOBAL_STATE);
        }

        // Check for changing of overheat mode
        bool new_overheat_mode = nvs_config_get_bool(NVS_CONFIG_OVERHEAT_MODE);
        
        if (new_overheat_mode != sys_module->overheat_mode) {
            sys_module->overheat_mode = new_overheat_mode;
            ESP_LOGI(TAG, "Overheat mode updated to: %d", sys_module->overheat_mode);
        }

        VCORE_check_fault(GLOBAL_STATE);

        // looper:
        vTaskDelay(POLL_RATE / portTICK_PERIOD_MS);
    }
}
