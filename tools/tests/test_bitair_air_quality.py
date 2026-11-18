"""Exercise the Bitair UART reader with the vendor's example and damaged streams."""

from pathlib import Path
import subprocess
import tempfile
import unittest


STUBS = {
    "esp_err.h": """#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
""",
    "esp_timer.h": "#include <stdint.h>\nint64_t esp_timer_get_time(void);\n",
    "freertos/FreeRTOS.h": """#include <stddef.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
""",
    "freertos/task.h": """void vTaskDelay(int ticks);
int xTaskCreate(void (*task)(void *), const char *name, int stack, void *arg, int priority, void *handle);
""",
    "driver/uart.h": """#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
enum { UART_NUM_2 = 2, UART_PIN_NO_CHANGE = -1, UART_DATA_8_BITS = 3,
       UART_PARITY_DISABLE = 0, UART_STOP_BITS_1 = 1, UART_HW_FLOWCTRL_DISABLE = 0,
       UART_SCLK_DEFAULT = 0 };
typedef struct { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; } uart_config_t;
bool uart_is_driver_installed(int port);
int uart_read_bytes(int port, uint8_t *buf, size_t len, int ticks);
esp_err_t uart_param_config(int port, const uart_config_t *config);
esp_err_t uart_driver_install(int port, int rx, int tx, int queue_size, void *queue, int flags);
esp_err_t uart_driver_delete(int port);
esp_err_t uart_set_pin(int port, int tx, int rx, int rts, int cts);
""",
}

HARNESS = r"""
#include <assert.h>
#include <setjmp.h>
#include "bitair_air_quality.c"

static int64_t now_us;
static jmp_buf done;
static const uint8_t *chunks[4];
static size_t lengths[4], count, cursor;
static bool installed;

int64_t esp_timer_get_time(void) { return now_us; }
void vTaskDelay(int ticks) { (void)ticks; }
bool uart_is_driver_installed(int port) { assert(port == 2); return installed; }
int uart_read_bytes(int port, uint8_t *buf, size_t len, int ticks)
{
    assert(port == 2 && ticks == 100);
    if (cursor == count) longjmp(done, 1);
    size_t size = lengths[cursor];
    assert(size <= len);
    if (size) memcpy(buf, chunks[cursor], size);
    cursor++;
    return (int)size;
}
esp_err_t uart_param_config(int port, const uart_config_t *c)
{
    assert(port == 2 && c->baud_rate == 9600 && c->data_bits == UART_DATA_8_BITS);
    assert(c->parity == UART_PARITY_DISABLE && c->stop_bits == UART_STOP_BITS_1);
    assert(c->flow_ctrl == UART_HW_FLOWCTRL_DISABLE);
    return ESP_OK;
}
esp_err_t uart_set_pin(int port, int tx, int rx, int rts, int cts)
{
    assert(port == 2 && tx == -1 && rx == 39 && rts == -1 && cts == -1);
    return ESP_OK;
}
esp_err_t uart_driver_install(int port, int rx, int tx, int size, void *queue, int flags)
{
    assert(port == 2 && rx > 128 && tx == 0 && size == 0 && !queue && flags == 0);
    installed = true;
    return ESP_OK;
}
esp_err_t uart_driver_delete(int port) { assert(port == 2); installed = false; return ESP_OK; }
int xTaskCreate(void (*task)(void *), const char *name, int stack, void *arg, int priority, void *handle)
{
    assert(task == sensor_task && name && stack && priority && !arg && !handle);
    return pdPASS;
}

static void run(void)
{
    cursor = 0;
    if (!setjmp(done)) sensor_task(NULL);
}

int main(void)
{
    // Dovelet's example reports 10 µg/m³ TVOC, 3 µg/m³ HCHO and 360 ppm estimated CO2.
    static const uint8_t example[] = {0x2c, 0xe4, 0, 10, 0, 3, 1, 0x68, 0x86};
    static const uint8_t corrupt[] = {0x2c, 0xe4, 0, 10, 0, 3, 1, 0x68, 0x87};
    static const uint8_t truncated[] = {0x2c, 0xe4, 0};
    static const uint8_t zero_tvoc[] = {0x2c, 0xe4, 0, 0, 0, 0x2c, 2, 0x30, 0x6e};
    static const uint8_t legacy[] = {0x2c, 0, 0, 3, 0xff};
    BitairAirQuality r;

    assert(bitair_air_quality_init() == ESP_OK);
    assert(bitair_air_quality_init() == ESP_ERR_INVALID_STATE);
    assert(!bitair_air_quality_get(&r));

    chunks[0] = corrupt; lengths[0] = sizeof(corrupt); count = 1;
    run();
    assert(!bitair_air_quality_get(&r));

    chunks[0] = example; lengths[0] = 4;
    chunks[1] = example + 4; lengths[1] = 5; count = 2;
    run();
    assert(bitair_air_quality_get(&r) && r.has_concentrations);
    assert(r.tvoc_ug_m3 == 10 && r.formaldehyde_ug_m3 == 3 && r.estimated_co2_ppm == 360);

    now_us = 10000000;
    assert(!bitair_air_quality_get(&r));
    chunks[0] = truncated; lengths[0] = sizeof(truncated);
    chunks[1] = zero_tvoc; lengths[1] = sizeof(zero_tvoc);
    run();
    assert(bitair_air_quality_get(&r) && r.has_concentrations);
    assert(r.tvoc_ug_m3 == 0 && r.formaldehyde_ug_m3 == 44 && r.estimated_co2_ppm == 560);

    chunks[0] = truncated; lengths[0] = sizeof(truncated);
    lengths[1] = 0;
    chunks[2] = legacy; lengths[2] = sizeof(legacy); count = 3;
    run();
    assert(bitair_air_quality_get(&r) && !r.has_concentrations && r.raw_level == 0);
    assert(r.estimated_co2_ppm == 0);
}
"""


class BitairAirQualityTest(unittest.TestCase):
    def test_packets_and_expiry(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            for name, content in STUBS.items():
                path = folder / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            source = folder / "test.c"
            source.write_text(HARNESS)
            executable = folder / "test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(folder),
                 "-I", str(root / "main"), str(source), "-o", str(executable)],
                check=True,
            )
            subprocess.run([str(executable)], check=True)
