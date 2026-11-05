#!/usr/bin/env python3
"""Compile and exercise the actual Bitair capture module with host GPIO/time stubs."""

from pathlib import Path
import subprocess
import tempfile
import unittest


STUBS = {
    "esp_err.h": "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n",
    "esp_attr.h": "#define IRAM_ATTR\n",
    "esp_timer.h": "#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
    "freertos/FreeRTOS.h": """#include <stdint.h>
#include <stddef.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
""",
    "driver/gpio.h": """#include <stdint.h>
#include "esp_err.h"
#define GPIO_NUM_40 40
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_ANYEDGE 3
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
int gpio_get_level(int pin);
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_isr_handler_add(int pin, void (*handler)(void *), void *arg);
""",
}

TEST = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bitair.c"

static int64_t now;
static int level, config_error, isr_error, registrations;
static void (*isr)(void *);
int64_t esp_timer_get_time(void) { return now; }
int gpio_get_level(int pin) { assert(pin == 40); return level; }
esp_err_t gpio_config(const gpio_config_t *cfg) {
    assert(cfg->pin_bit_mask == (1ULL << 40));
    assert(cfg->mode == GPIO_MODE_INPUT && cfg->intr_type == GPIO_INTR_ANYEDGE);
    assert(!cfg->pull_up_en && !cfg->pull_down_en);
    return config_error;
}
esp_err_t gpio_isr_handler_add(int pin, void (*handler)(void *), void *arg) {
    assert(pin == 40 && arg == NULL);
    registrations++;
    if (!isr_error) isr = handler;
    return isr_error;
}
static void reset(int initial) {
    capture = (FanCapture){0};
    now = 0; level = initial; config_error = isr_error = registrations = 0; isr = NULL;
}
static void edge(int next_level, int64_t time) {
    now = time; level = next_level; assert(isr); isr(NULL);
}
static void expect(float speed, bool disabled) {
    assert(bitair_get_fan_percent() == speed);
    assert(bitair_mining_disabled() == disabled);
    const char *name = disabled ? (speed == 0 ? "off" : "unknown") :
                       (speed == 50 ? "silent" : speed == 75 ? "auto" : "turbo");
    assert(strcmp(bitair_get_mode_name(), name) == 0);
}
static void pwm(int high, int period) {
    reset(0); assert(bitair_init() == ESP_OK);
    edge(1, 100);
    for (int i = 0; i < 3; i++) {
        edge(0, 100 + i * period + high);
        edge(1, 100 + (i + 1) * period);
    }
}
static void cycle(int high, int period) {
    int64_t rise = now;
    edge(0, rise + high);
    edge(1, rise + period);
}
int main(void) {
    reset(0); expect(100, true);
    config_error = 23; assert(bitair_init() == 23 && !registrations);
    config_error = 0; isr_error = 24; assert(bitair_init() == 24); expect(100, true);
    isr_error = 0; assert(bitair_init() == ESP_OK);
    int count = registrations; assert(bitair_init() == ESP_OK && registrations == count);
    expect(100, true); now = 499999; expect(100, true); now++; expect(0, true);
    reset(1); assert(bitair_init() == ESP_OK); expect(100, true);
    now = 500000; expect(100, false);

    const int high[] = {24, 25, 65, 66, 85, 86};
    const float speed[] = {0, 50, 50, 75, 75, 100};
    for (unsigned i = 0; i < sizeof(high)/sizeof(high[0]); i++) {
        pwm(high[i], 100); expect(speed[i], high[i] < 25);
        edge(0, now + high[i]); expect(speed[i], high[i] < 25);
    }
    pwm(20, 40); expect(50, false); // 25 kHz remains supported.
    edge(0, now + 20);
    now += 250001; expect(100, true); // stale LOW capture cannot keep mining enabled.
    now += 250000; expect(0, true);
    pwm(20, 40); expect(50, false);
    now += 250001; expect(50, false); // constant HIGH begins the TURBO settling interval.
    now += 200000; expect(100, false); // PWM-to-100% must not briefly pause mining.
    now += 50000; expect(100, false);
    edge(0, now + 1); expect(100, true);
    now += 500000; expect(0, true);

    reset(0); assert(bitair_init() == ESP_OK); edge(1, 100);
    for (int i = 0; i < 2; i++) { edge(0, 150 + i * 100); edge(1, 200 + i * 100); }
    expect(100, true); // two complete cycles are insufficient.
    edge(0, 301); expect(100, true); // reject a 1 us glitch.
    edge(1, 400); expect(100, true);
    pwm(50, 100); expect(50, false);
    edge(0, now + 1); expect(100, true); // invalidate an established capture on noise.
    pwm(150000, 300000); expect(100, true); // unsupported slow waveform.
    pwm(500, 1000); expect(50, false);
    for (int i = 0; i < 10; i++) { cycle(900, 1000); expect(50, false); }
    for (int i = 0; i < 40; i++) { cycle(500, 1000); expect(50, false); }
    for (int i = 0; i < 100; i++) {
        cycle(i % 2 ? 900 : 100, 1000);
        expect(50, false); // timing jitter must not change the physical mode.
    }
    for (int i = 0; i < 260; i++) { cycle(900, 1000); bitair_get_fan_percent(); }
    expect(100, false); // a sustained new selection is applied.
    for (int i = 0; i < 280; i++) { cycle(100, 1000); bitair_get_fan_percent(); }
    expect(0, true); // off still pauses after the settling interval.
    edge(0, now + 1); expect(100, true); // invalid input still fails safe immediately.
    puts("Bitair capture tests passed");
    return 0;
}
"""


class BitairCaptureTest(unittest.TestCase):
    def test_capture(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory(prefix="bitair-test-") as directory:
            temp = Path(directory)
            for name, content in STUBS.items():
                path = temp / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            source = temp / "test.c"
            source.write_text(TEST)
            binary = temp / "test-bitair"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(temp), "-I", str(root / "main"),
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
