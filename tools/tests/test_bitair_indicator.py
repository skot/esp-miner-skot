#!/usr/bin/env python3
"""Exercise the production indicator with host LEDC, scheduler and mode stubs."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

STUBS = {
    "esp_err.h": """#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NO_MEM 2
static inline const char *esp_err_to_name(esp_err_t err) { (void)err; return "test"; }
""",
    "global_state.h": """#pragma once
#include <stdbool.h>
#define BITAIR 11
struct GlobalState {
    struct { struct { int id; } family; } DEVICE_CONFIG;
    struct { bool is_connected, hardware_fault, mining_paused, pools_unavailable; } SYSTEM_MODULE;
};
""",
    "esp_log.h": """#pragma once
void test_log(const char *, const char *, ...);
#define ESP_LOGW(...) test_log(__VA_ARGS__)
""",
    "connect.h": "#include <stdbool.h>\nbool wifi_is_connected(void);\n",
    "esp_timer.h": "#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
    "freertos/FreeRTOS.h": """#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef unsigned TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
""",
    "freertos/task.h": """#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t *, unsigned);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
""",
    "driver/ledc.h": """#pragma once
#include <stdint.h>
#include "esp_err.h"
#define LEDC_LOW_SPEED_MODE 0
#define LEDC_CHANNEL_0 0
#define LEDC_TIMER_0 0
#define LEDC_TIMER_10_BIT 10
#define LEDC_AUTO_CLK 0
typedef struct { int speed_mode, duty_resolution, timer_num, freq_hz, clk_cfg; } ledc_timer_config_t;
typedef struct { int gpio_num, speed_mode, channel, timer_sel; uint32_t duty; int hpoint;
    struct { unsigned output_invert:1; } flags;
} ledc_channel_config_t;
esp_err_t ledc_timer_config(const ledc_timer_config_t *);
esp_err_t ledc_channel_config(const ledc_channel_config_t *);
esp_err_t ledc_set_duty(int, int, uint32_t);
esp_err_t ledc_update_duty(int, int);
esp_err_t ledc_stop(int, int, int);
""",
}

TEST = r"""
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "bitair_indicator.c"

static int timer_calls, channel_calls, task_calls, stops, writes, updates, logs;
static int timer_error, channel_error, write_error, update_error;
static bool task_error, disabled;
static const char *physical_mode = "silent";
static int64_t now_ms;
static int frames, frame_limit;
static bool change_mode, connected;
static int wifi_checks;
static uint32_t applied_duty, pending_duty;
static jmp_buf finished;

bool wifi_is_connected(void) { wifi_checks++; return connected; }
bool bitair_mining_disabled(void) { return disabled; }
const char *bitair_get_mode_name(void) { return physical_mode; }
void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; logs++; }
int64_t esp_timer_get_time(void) { return now_ms * 1000; }
TickType_t xTaskGetTickCount(void) { return now_ms; }
void vTaskDelayUntil(TickType_t *wake, unsigned interval) {
    assert(interval == 20); *wake += interval; now_ms = *wake;
    if (++frames == frame_limit) longjmp(finished, 1);
    if (change_mode) {
        connected = true;
        physical_mode = "auto";
    }
}
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack, void *arg,
                unsigned priority, TaskHandle_t *handle) {
    assert(task == indicator_task && strcmp(name, "bitair_indicator") == 0);
    assert(stack >= 3072 && priority == 2 && arg != NULL);
    task_calls++;
    if (task_error) return 0;
    *handle = (void *)1; return pdPASS;
}
esp_err_t ledc_timer_config(const ledc_timer_config_t *c) {
    timer_calls++;
    assert(c->speed_mode == 0 && c->timer_num == 0 && c->duty_resolution == 10);
    assert(c->freq_hz == 5000 && c->clk_cfg == LEDC_AUTO_CLK);
    return timer_error;
}
esp_err_t ledc_channel_config(const ledc_channel_config_t *c) {
    channel_calls++;
    assert(c->gpio_num == 41 && c->speed_mode == 0 && c->channel == 0 && c->timer_sel == 0);
    assert(c->duty == 0 && c->hpoint == 0 && !c->flags.output_invert);
    return channel_error;
}
esp_err_t ledc_set_duty(int mode, int channel, uint32_t duty) {
    assert(mode == 0 && channel == 0 && duty <= 1023);
    writes++; pending_duty = duty; return write_error;
}
esp_err_t ledc_update_duty(int mode, int channel) {
    assert(mode == 0 && channel == 0); updates++;
    if (!update_error) applied_duty = pending_duty;
    return update_error;
}
esp_err_t ledc_stop(int mode, int channel, int idle) {
    assert(mode == 0 && channel == 0 && idle == 0); stops++; return ESP_OK;
}
static void run(GlobalState *state, int count) {
    frames = 0; frame_limit = count;
    if (!setjmp(finished)) indicator_task(state);
}
int main(void) {
    GlobalState state = {0};
    assert(bitair_indicator_init(NULL) == ESP_ERR_INVALID_ARG);
    assert(bitair_indicator_init(&state) == ESP_OK && !timer_calls && !task_calls);
    state.DEVICE_CONFIG.family.id = BITAIR;
    timer_error = 21;
    assert(bitair_indicator_init(&state) == 21 && !channel_calls && !task_calls);
    timer_error = 0; channel_error = 22;
    assert(bitair_indicator_init(&state) == 22 && !task_calls);
    channel_error = 0; task_error = true;
    assert(bitair_indicator_init(&state) == ESP_ERR_NO_MEM && stops == 1);
    task_error = false;
    assert(bitair_indicator_init(&state) == ESP_OK && task_calls == 2);
    int configured = channel_calls;
    assert(bitair_indicator_init(&state) == ESP_OK && channel_calls == configured && task_calls == 2);

    assert(select_mode(&state, connected) == LED_BREATHE);
    connected = true;
    const char *names[] = {"off", "silent", "auto", "turbo", "unknown"};
    const LedMode modes[] = {LED_OFF, LED_SILENT, LED_AUTO, LED_TURBO, LED_OFF};
    const uint32_t duties[] = {0, 511, 767, 1023, 0};
    Indicator led = {0};
    for (int i = 0; i < 5; i++) {
        physical_mode = names[i];
        assert(select_mode(&state, connected) == modes[i]);
        assert(indicator_duty(&led, modes[i], 100) == duties[i]);
    }
    physical_mode = "turbo"; disabled = true;
    assert(select_mode(&state, connected) == LED_OFF); disabled = false;
    state.SYSTEM_MODULE.mining_paused = true;
    assert(select_mode(&state, connected) == LED_OFF); state.SYSTEM_MODULE.mining_paused = false;
    state.SYSTEM_MODULE.pools_unavailable = true;
    assert(select_mode(&state, connected) == LED_TURBO); // Stock indicates purifier mode, not pool activity.
    connected = false;
    assert(select_mode(&state, connected) == LED_BREATHE);
    state.SYSTEM_MODULE.hardware_fault = true;
    assert(select_mode(&state, connected) == LED_FAILURE); state.SYSTEM_MODULE.hardware_fault = false;

    assert(indicator_duty(&led, LED_BREATHE, 100) == 81); // Stock ramp begins 5 -> 8 percent.
    assert(indicator_duty(&led, LED_BREATHE, 119) == 81);
    assert(indicator_duty(&led, LED_BREATHE, 120) == 112);
    for (int i = 2; i < 32; i++) indicator_duty(&led, LED_BREATHE, 100 + i * 20);
    assert(led.brightness == 100 && !led.rising);
    assert(indicator_duty(&led, LED_BREATHE, 740) == 992); // 97% on descent.
    for (int i = 33; i < 64; i++) indicator_duty(&led, LED_BREATHE, 100 + i * 20);
    assert(led.brightness == 5 && led.rising);
    assert(indicator_duty(&led, LED_BREATHE, 100000) == 81); // No jump after scheduler delay.
    indicator_duty(&led, LED_SILENT, 100001);
    assert(indicator_duty(&led, LED_BREATHE, 100002) == 81); // Reset on reconnect/disconnect.

    const int times[] = {0, 99, 100, 199, 200, 300, 400, 500, 600, 1999, 2000};
    const uint32_t flashes[] = {1023, 1023, 0, 0, 1023, 0, 1023, 0, 0, 0, 1023};
    for (unsigned i = 0; i < sizeof(times)/sizeof(times[0]); i++) {
        assert(indicator_duty(&led, LED_FAILURE, 110000 + times[i]) == flashes[i]);
    }
    assert(indicator_duty(&led, LED_OFF, 113000) == 0);

    writes = updates = logs = 0; now_ms = 0; change_mode = true;
    wifi_checks = 0;
    run(&state, 7);
    assert(wifi_checks == 2);
    assert(writes == 6 && updates == 6 && applied_duty == 767); // Breathing -> connected Auto.
    change_mode = false; writes = updates = logs = 0; write_error = 23;
    run(&state, 3);
    assert(writes == 3 && !updates && logs == 1); // Retry without a log storm.
    write_error = 0; update_error = 24; writes = updates = logs = 0;
    run(&state, 3);
    assert(writes == 3 && updates == 3 && logs == 1);
    update_error = 0; writes = updates = 0;
    run(&state, 3);
    assert(writes == 1 && updates == 1 && applied_duty == 767);
    puts("Bitair front LED tests passed");
    return 0;
}
"""


class BitairIndicatorTest(unittest.TestCase):
    def test_stock_patterns_and_driver(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory(prefix="bitair-led-test-") as directory:
            temp = Path(directory)
            for name, content in STUBS.items():
                path = temp / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            shutil.copyfile(root / "main/bitair_indicator.c", temp / "bitair_indicator.c")
            (temp / "test.c").write_text(TEST)
            binary = temp / "test-led"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(temp), "-I", str(root / "main"),
                            str(temp / "test.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
