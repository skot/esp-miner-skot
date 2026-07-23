#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "input.h"

#define GPIO_BUTTON_BOOT CONFIG_GPIO_BUTTON_BOOT

#define BUTTON_POLL_MS            10
#define BUTTON_DEBOUNCE_MS        30
#define LONG_PRESS_DURATION_MS  2000

static const char * TAG = "input";

static void (*button_short_clicked_fn)(void) = NULL;
static void (*button_long_pressed_fn)(void) = NULL;
static TaskHandle_t button_task_handle;

static bool button_is_pressed(void)
{
    return gpio_get_level(GPIO_BUTTON_BOOT) == 0;
}

static void button_task(void * pvParameters)
{
    (void) pvParameters;

    bool raw_pressed = button_is_pressed();
    bool stable_pressed = raw_pressed;
    bool long_press_fired = false;
    TickType_t raw_changed_at = xTaskGetTickCount();
    TickType_t pressed_at = raw_changed_at;
    TickType_t task_wake_time = raw_changed_at;

    while (true) {
        TickType_t now = xTaskGetTickCount();
        bool new_raw_pressed = button_is_pressed();

        if (new_raw_pressed != raw_pressed) {
            raw_pressed = new_raw_pressed;
            raw_changed_at = now;
        }

        if (raw_pressed != stable_pressed &&
            now - raw_changed_at >= pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
            stable_pressed = raw_pressed;

            if (stable_pressed) {
                pressed_at = now;
                long_press_fired = false;
            } else if (!long_press_fired && button_short_clicked_fn != NULL) {
                ESP_LOGI(TAG, "Short button click detected");
                button_short_clicked_fn();
            }
        }

        if (stable_pressed && !long_press_fired &&
            now - pressed_at >= pdMS_TO_TICKS(LONG_PRESS_DURATION_MS)) {
            long_press_fired = true;
            if (button_long_pressed_fn != NULL) {
                ESP_LOGI(TAG, "Long button press detected");
                button_long_pressed_fn();
            }
        }

        vTaskDelayUntil(&task_wake_time, pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

esp_err_t input_init(void (*button_short_clicked_cb)(void), void (*button_long_pressed_cb)(void))
{
    if (button_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Install button driver");

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_BUTTON_BOOT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Error configuring boot button");

    button_short_clicked_fn = button_short_clicked_cb;
    button_long_pressed_fn = button_long_pressed_cb;

    if (xTaskCreate(button_task, "button input", 4096, NULL, 5, &button_task_handle) != pdPASS) {
        button_task_handle = NULL;
        ESP_LOGE(TAG, "Failed to create button input task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
