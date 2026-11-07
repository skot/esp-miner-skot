#include "bitair_indicator.h"
#include "bitair.h"
#include "global_state.h"
#include "connect.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define INDICATOR_GPIO 41
#define INDICATOR_MAX_DUTY 1023
#define INDICATOR_INTERVAL_MS 20

typedef enum { LED_OFF, LED_SILENT, LED_AUTO, LED_TURBO, LED_BREATHE, LED_FAILURE } LedMode;

typedef struct {
    LedMode mode;
    int brightness;
    bool rising;
    int64_t last_step_ms;
    int64_t mode_since_ms;
} Indicator;

static const char *TAG = "bitair_indicator";
static TaskHandle_t indicator_task_handle;

static LedMode select_mode(const GlobalState *state, bool wifi_connected)
{
    if (state->SYSTEM_MODULE.hardware_fault) return LED_FAILURE;
    if (!wifi_connected) return LED_BREATHE;
    if (state->SYSTEM_MODULE.mining_paused || bitair_mining_disabled()) return LED_OFF;
    const char *mode = bitair_get_mode_name();
    if (strcmp(mode, "silent") == 0) return LED_SILENT;
    if (strcmp(mode, "auto") == 0) return LED_AUTO;
    if (strcmp(mode, "turbo") == 0) return LED_TURBO;
    return LED_OFF;
}

static uint32_t indicator_duty(Indicator *indicator, LedMode mode, int64_t now_ms)
{
    if (indicator->mode != mode) {
        *indicator = (Indicator) {
            .mode = mode,
            .brightness = 5,
            .rising = true,
            .last_step_ms = now_ms - INDICATOR_INTERVAL_MS,
            .mode_since_ms = now_ms,
        };
    }
    switch (mode) {
        case LED_SILENT: return 0x1ff;
        case LED_AUTO: return 0x2ff;
        case LED_TURBO: return INDICATOR_MAX_DUTY;
        case LED_FAILURE: {
            int64_t phase = (now_ms - indicator->mode_since_ms) % 2000;
            return phase < 600 && (phase / 100) % 2 == 0 ? INDICATOR_MAX_DUTY : 0;
        }
        case LED_BREATHE:
            if (now_ms - indicator->last_step_ms >= INDICATOR_INTERVAL_MS) {
                indicator->last_step_ms = now_ms;
                if (indicator->rising) {
                    indicator->brightness += 3;
                    if (indicator->brightness >= 100) {
                        indicator->brightness = 100;
                        indicator->rising = false;
                    }
                } else if (indicator->brightness < 9) {
                    indicator->brightness = 5;
                    indicator->rising = true;
                } else {
                    indicator->brightness -= 3;
                }
            }
            return indicator->brightness * INDICATOR_MAX_DUTY / 100;
        default: return 0;
    }
}

static void indicator_task(void *arg)
{
    GlobalState *state = arg;
    Indicator indicator = { .mode = LED_OFF };
    int last_duty = -1;
    esp_err_t last_error = ESP_OK;
    bool wifi_connected = false;
    int64_t next_wifi_check_ms = 0;
    TickType_t wake_time = xTaskGetTickCount();
    while (true) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms >= next_wifi_check_ms) {
            wifi_connected = wifi_is_connected();
            next_wifi_check_ms = now_ms + 100;
        }
        uint32_t duty = indicator_duty(&indicator, select_mode(state, wifi_connected), now_ms);
        if ((int)duty != last_duty) {
            esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
            if (err == ESP_OK) err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            if (err == ESP_OK) {
                last_duty = duty;
            } else if (err != last_error) {
                ESP_LOGW(TAG, "Failed to update front LED: %s", esp_err_to_name(err));
            }
            last_error = err;
        }
        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(INDICATOR_INTERVAL_MS));
    }
}

esp_err_t bitair_indicator_init(GlobalState *state)
{
    if (state == NULL) return ESP_ERR_INVALID_ARG;
    if (state->DEVICE_CONFIG.family.id != BITAIR || indicator_task_handle != NULL) return ESP_OK;
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;
    const ledc_channel_config_t channel = {
        .gpio_num = INDICATOR_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) return err;
    if (xTaskCreate(indicator_task, "bitair_indicator", 3072, state, 2, &indicator_task_handle) != pdPASS) {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
