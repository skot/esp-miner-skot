#!/usr/bin/env python3
"""Fault-inject the production power-management loop without ASIC hardware."""

from pathlib import Path
import subprocess
import tempfile
import unittest


SUPPORT = r"""
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "production_power_management_task.h"
typedef int esp_err_t;
#define ESP_OK 0
#define BITAIR 11
#define UART_NUM_1 1
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) (ms)
#define ASIC_INIT_RECOVERY 1
typedef struct {
    bool mining_paused, hardware_fault, pools_unavailable, overheat_mode;
    char hardware_fault_msg[64];
} SystemModule;
struct GlobalState {
    struct {
        struct {
            int id, asic_count;
            struct { int small_core_count, default_voltage_mv, default_frequency_mhz; } asic;
        } family;
        bool emc_internal_temp;
    } DEVICE_CONFIG;
    PowerManagementModule POWER_MANAGEMENT_MODULE;
    SystemModule SYSTEM_MODULE;
    struct { bool is_finished, is_active; } SELF_TEST_MODULE;
    bool ASIC_initalized;
    volatile bool ASIC_startup_pending;
};
enum { NVS_CONFIG_ASIC_VOLTAGE, NVS_CONFIG_ASIC_FREQUENCY, NVS_CONFIG_AUTO_FAN_SPEED,
       NVS_CONFIG_MANUAL_FAN_SPEED, NVS_CONFIG_OVERHEAT_MODE };
void test_log(const char *, const char *, ...);
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
void vTaskDelay(unsigned);
void vTaskDelete(void *);
uint16_t nvs_config_get_u16(int);
float nvs_config_get_float(int);
bool nvs_config_get_bool(int);
void nvs_config_set_u16(int, uint16_t);
void nvs_config_set_float(int, float);
void nvs_config_set_bool(int, bool);
bool VCORE_is_initialized(void);
esp_err_t VCORE_set_voltage(GlobalState *, float);
int16_t VCORE_get_voltage_mv(GlobalState *);
int16_t VCORE_get_voltage_min_mv(GlobalState *);
esp_err_t VCORE_check_fault(GlobalState *);
esp_err_t asic_hold_reset_low(void);
void ASIC_set_frequency(GlobalState *);
void ASIC_set_nonce_space(GlobalState *);
uint8_t asic_initialize(GlobalState *, int, uint32_t);
float Power_get_input_voltage(GlobalState *);
void Power_get_output(GlobalState *, float *, float *);
float Power_get_vreg_temp(GlobalState *);
float Thermal_get_chip_temp(GlobalState *);
float Thermal_get_chip_temp2(GlobalState *);
void difficulty_controller_update(GlobalState *);
void suffixString(double, char *, size_t, int);
int uart_flush(int);
bool bitair_mining_disabled(void);
"""

TEST = r"""
#include <assert.h>
#include <setjmp.h>
#include <string.h>
#include "production_power_management.c"

static jmp_buf done;
static int scenario, loops, loop_limit, init_result, init_calls, voltage_writes, asic_writes, faults_checked;
static bool physical_off, reset_low, regulator_initialized, fail_power_off, fail_power_on, overheat;
static float voltage, frequency;
static uint16_t setting_voltage;
enum { PENDING, INITIAL_FAILURE, RESUME_FAILURE, THERMAL_FAILURE, USER_PAUSE, RESTORE_FAILURE };
void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
void vTaskDelay(unsigned delay) { (void)delay; }
void vTaskDelete(void *task) { (void)task; }
uint16_t nvs_config_get_u16(int key) { (void)key; return setting_voltage; }
float nvs_config_get_float(int key) { (void)key; return frequency; }
bool nvs_config_get_bool(int key) { return key == NVS_CONFIG_OVERHEAT_MODE && overheat; }
void nvs_config_set_u16(int key, uint16_t value) { if (key == NVS_CONFIG_ASIC_VOLTAGE) setting_voltage = value; }
void nvs_config_set_float(int key, float value) { (void)key; frequency = value; }
void nvs_config_set_bool(int key, bool value) { if (key == NVS_CONFIG_OVERHEAT_MODE) overheat = value; }
bool VCORE_is_initialized(void) { return regulator_initialized; }
esp_err_t VCORE_set_voltage(GlobalState *g, float value) {
    (void)g; voltage_writes++;
    if ((value == 0 && fail_power_off) || (value > 0 && fail_power_on)) return -1;
    voltage = value; return ESP_OK;
}
int16_t VCORE_get_voltage_mv(GlobalState *g) { (void)g; return voltage * 1000; }
int16_t VCORE_get_voltage_min_mv(GlobalState *g) { (void)g; return 1000; }
esp_err_t VCORE_check_fault(GlobalState *g) { (void)g; faults_checked++; return ESP_OK; }
esp_err_t asic_hold_reset_low(void) { reset_low = true; return ESP_OK; }
void ASIC_set_frequency(GlobalState *g) { asic_writes++; g->POWER_MANAGEMENT_MODULE.actual_frequency = g->POWER_MANAGEMENT_MODULE.frequency_value; }
void ASIC_set_nonce_space(GlobalState *g) { (void)g; asic_writes++; }
uint8_t asic_initialize(GlobalState *g, int mode, uint32_t delay) {
    (void)mode; (void)delay; init_calls++; reset_low = false;
    g->ASIC_initalized = init_result > 0; return init_result;
}
float Power_get_input_voltage(GlobalState *g) {
    if (++loops > loop_limit) longjmp(done, 1);
    if (scenario == INITIAL_FAILURE && loops == 2 && g->ASIC_startup_pending) {
        POWER_MANAGEMENT_finish_asic_startup(g, false);
    }
    if (scenario == RESUME_FAILURE || scenario == USER_PAUSE || scenario == RESTORE_FAILURE) physical_off = loops == 1;
    if (scenario == USER_PAUSE) g->SYSTEM_MODULE.mining_paused = loops == 2;
    return 5000;
}
void Power_get_output(GlobalState *g, float *p, float *i) { (void)g; *p = 20; *i = 10000; }
float Power_get_vreg_temp(GlobalState *g) { (void)g; return 50; }
float Thermal_get_chip_temp(GlobalState *g) {
    return !g->ASIC_initalized ? -1 : scenario == THERMAL_FAILURE && loops == 1 ? 80 : 45;
}
float Thermal_get_chip_temp2(GlobalState *g) { (void)g; return -1; }
void difficulty_controller_update(GlobalState *g) { (void)g; }
void suffixString(double value, char *buf, size_t size, int digits) { (void)value; (void)digits; snprintf(buf, size, "test"); }
int uart_flush(int port) { assert(port == UART_NUM_1); return 0; }
bool bitair_mining_disabled(void) { return physical_off; }

static GlobalState setup(int selected) {
    scenario = selected; loops = init_calls = voltage_writes = asic_writes = faults_checked = 0;
    loop_limit = 4; init_result = 0; physical_off = reset_low = fail_power_off = fail_power_on = overheat = false;
    regulator_initialized = true; voltage = 1.15f; frequency = 600; setting_voltage = 1150;
    GlobalState g = {0};
    g.DEVICE_CONFIG.family.id = BITAIR; g.DEVICE_CONFIG.family.asic_count = 1;
    g.DEVICE_CONFIG.family.asic.small_core_count = 2040;
    g.ASIC_initalized = true;
    return g;
}
static void run(GlobalState *g) { if (!setjmp(done)) POWER_MANAGEMENT_task(g); }
static void assert_latched(GlobalState *g) {
    assert(!g->ASIC_initalized && !g->ASIC_startup_pending);
    assert(g->SYSTEM_MODULE.hardware_fault && reset_low);
    assert(strstr(g->SYSTEM_MODULE.hardware_fault_msg, "restart required"));
    assert(voltage == 0 && g->POWER_MANAGEMENT_MODULE.expected_hashrate == 0);
}
int main(void) {
    GlobalState g = setup(PENDING); g.ASIC_initalized = false; g.ASIC_startup_pending = true;
    run(&g); assert(g.ASIC_startup_pending && !voltage_writes && !asic_writes && !init_calls);
    POWER_MANAGEMENT_finish_asic_startup(&g, true); assert(!g.ASIC_startup_pending && !g.SYSTEM_MODULE.hardware_fault);

    g = setup(INITIAL_FAILURE); g.ASIC_initalized = false; g.ASIC_startup_pending = true;
    physical_off = true; run(&g); assert_latched(&g); assert(!init_calls && !asic_writes && voltage_writes == 1);

    g = setup(RESUME_FAILURE); run(&g); assert_latched(&g);
    assert(init_calls == 1 && voltage_writes == 3);

    g = setup(RESTORE_FAILURE); init_result = 1; fail_power_on = true; run(&g); assert_latched(&g);
    assert(!init_calls && voltage_writes == 3); // never probe ASIC after a failed core-voltage restore.

    g = setup(THERMAL_FAILURE); run(&g); assert_latched(&g);
    assert(init_calls == 1 && voltage_writes == 3 && overheat);

    g = setup(USER_PAUSE); init_result = 1; loop_limit = 2; run(&g);
    assert(!init_calls && !g.ASIC_initalized && voltage == 0); // physical ON must preserve API pause.
    g = setup(USER_PAUSE); init_result = 1; run(&g);
    assert(init_calls == 1 && g.ASIC_initalized && !g.SYSTEM_MODULE.hardware_fault && voltage > 1);

    g = setup(INITIAL_FAILURE); fail_power_off = true;
    POWER_MANAGEMENT_finish_asic_startup(&g, false);
    assert(reset_low && g.SYSTEM_MODULE.hardware_fault && !g.ASIC_startup_pending);
    g = setup(INITIAL_FAILURE); regulator_initialized = false;
    POWER_MANAGEMENT_finish_asic_startup(&g, false); assert(!voltage_writes && reset_low);
    g = setup(INITIAL_FAILURE); g.DEVICE_CONFIG.family.id = 0; g.ASIC_startup_pending = true;
    POWER_MANAGEMENT_finish_asic_startup(&g, false);
    assert(g.ASIC_initalized && g.ASIC_startup_pending && !voltage_writes && !g.SYSTEM_MODULE.hardware_fault);
    puts("Bitair startup/pause fault-injection tests passed");
    return 0;
}
"""


class BitairStartupTest(unittest.TestCase):
    def test_power_lifecycle_faults(self):
        root = Path(__file__).resolve().parents[2]
        headers = ["esp_log.h", "freertos/FreeRTOS.h", "freertos/task.h", "global_state.h",
                   "nvs_config.h", "vcore.h", "thermal.h", "power.h", "asic.h", "utils.h",
                   "asic_init.h", "asic_reset.h", "difficulty_controller.h", "driver/uart.h", "bitair.h"]
        with tempfile.TemporaryDirectory(prefix="bitair-startup-test-") as directory:
            temp = Path(directory)
            for name in headers:
                path = temp / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "test_support.h"\n')
            (temp / "test_support.h").write_text(SUPPORT)
            (temp / "production_power_management_task.h").symlink_to(root / "main/tasks/power_management_task.h")
            (temp / "production_power_management.c").symlink_to(root / "main/tasks/power_management_task.c")
            (temp / "test.c").write_text(TEST)
            binary = temp / "test-startup"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(temp),
                            str(temp / "test.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
