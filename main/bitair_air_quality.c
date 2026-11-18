#include "bitair_air_quality.h"
#include <string.h>
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SENSOR_UART UART_NUM_2
#define SENSOR_RX_GPIO 39
#define FRAME_START 0x2c
#define CONCENTRATION_HEADER 0xe4
#define CONCENTRATION_FRAME_SIZE 9
#define LEGACY_FRAME_SIZE 5
#define READING_MAX_AGE_US (10 * 1000 * 1000)

typedef struct {
    uint8_t bytes[CONCENTRATION_FRAME_SIZE];
    size_t used;
} SensorFrame;

static portMUX_TYPE reading_lock = portMUX_INITIALIZER_UNLOCKED;
static BitairAirQuality latest_reading;
static int64_t last_reading_us = -1;

static uint16_t read_be16(const uint8_t *bytes)
{
    return ((uint16_t)bytes[0] << 8) | bytes[1];
}

static void seek_frame_start(SensorFrame *frame, size_t from)
{
    const uint8_t *next = memchr(frame->bytes + from, FRAME_START, frame->used - from);
    if (next == NULL) {
        frame->used = 0;
    } else {
        frame->used -= (size_t)(next - frame->bytes);
        memmove(frame->bytes, next, frame->used);
    }
}

static bool frame_push(SensorFrame *frame, uint8_t byte, BitairAirQuality *reading)
{
    if (frame->used == 0 && byte != FRAME_START) return false;
    frame->bytes[frame->used++] = byte;

    while (frame->used >= 2) {
        const uint8_t *bytes = frame->bytes;
        if (bytes[1] == CONCENTRATION_HEADER) {
            if (frame->used < CONCENTRATION_FRAME_SIZE) return false;
            uint8_t checksum = 0;
            for (size_t i = 0; i < CONCENTRATION_FRAME_SIZE - 1; i++) {
                checksum += bytes[i];
            }
            if (checksum == bytes[8]) {
                *reading = (BitairAirQuality) {
                    .has_concentrations = true,
                    .tvoc_ug_m3 = read_be16(&bytes[2]),
                    .formaldehyde_ug_m3 = read_be16(&bytes[4]),
                    .estimated_co2_ppm = read_be16(&bytes[6]),
                };
                frame->used = 0;
                return true;
            }
        } else {
            if (frame->used < LEGACY_FRAME_SIZE) return false;
            if (bytes[3] == 0x03 && bytes[4] == 0xff) {
                *reading = (BitairAirQuality) { .raw_level = read_be16(&bytes[1]) };
                seek_frame_start(frame, LEGACY_FRAME_SIZE);
                return true;
            }
        }

        // A damaged or truncated packet may already contain the start of the next one.
        seek_frame_start(frame, 1);
    }
    return false;
}

static void sensor_task(void *arg)
{
    (void)arg;
    SensorFrame frame = {0};
    uint8_t buffer[64];

    while (true) {
        int count = uart_read_bytes(SENSOR_UART, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
        if (count <= 0) {
            frame.used = 0;
            if (count < 0) vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        for (int i = 0; i < count; i++) {
            BitairAirQuality reading;
            if (frame_push(&frame, buffer[i], &reading)) {
                int64_t now = esp_timer_get_time();
                portENTER_CRITICAL(&reading_lock);
                latest_reading = reading;
                last_reading_us = now;
                portEXIT_CRITICAL(&reading_lock);
            }
        }
    }
}

bool bitair_air_quality_get(BitairAirQuality *reading)
{
    portENTER_CRITICAL(&reading_lock);
    int64_t timestamp = last_reading_us;
    *reading = latest_reading;
    portEXIT_CRITICAL(&reading_lock);
    return timestamp >= 0 && esp_timer_get_time() - timestamp < READING_MAX_AGE_US;
}

esp_err_t bitair_air_quality_init(void)
{
    if (uart_is_driver_installed(SENSOR_UART)) return ESP_ERR_INVALID_STATE;

    const uart_config_t config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(SENSOR_UART, &config);
    if (err != ESP_OK) return err;

    err = uart_driver_install(SENSOR_UART, 512, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;

    err = uart_set_pin(SENSOR_UART, UART_PIN_NO_CHANGE, SENSOR_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err == ESP_OK && xTaskCreate(sensor_task, "bitair_air", 3072, NULL, 2, NULL) == pdPASS) {
        return ESP_OK;
    }

    uart_driver_delete(SENSOR_UART);
    return err != ESP_OK ? err : ESP_ERR_NO_MEM;
}
