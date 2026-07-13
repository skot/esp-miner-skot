#include "ESP32Fan.h"

#include <math.h>

#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#define FAN_PWM_GPIO 8
#define FAN_TACH_GPIO 9
#define FAN_PWM_FREQUENCY_HZ 25000
#define FAN_PWM_MAX_DUTY 255
#define FAN_TACH_SAMPLE_US 1000000
#define FAN_TACH_PULSES_PER_REVOLUTION 2

static const char * TAG = "esp32_fan";

static pcnt_unit_handle_t tach_unit;
static int64_t tach_sample_start_us;
static uint16_t cached_rpm;
static bool initialized;

esp_err_t ESP32Fan_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = FAN_PWM_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG, "Failed to configure fan PWM timer");

    ledc_channel_config_t channel_config = {
        .gpio_num = FAN_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG, "Failed to configure fan PWM output");

    pcnt_unit_config_t unit_config = {
        .low_limit = -1,
        .high_limit = 32767,
    };
    ESP_RETURN_ON_ERROR(pcnt_new_unit(&unit_config, &tach_unit), TAG, "Failed to create fan tach counter");

    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(tach_unit, &filter_config), TAG, "Failed to configure fan tach filter");

    pcnt_chan_config_t tach_config = {
        .edge_gpio_num = FAN_TACH_GPIO,
        .level_gpio_num = -1,
    };
    pcnt_channel_handle_t tach_channel;
    ESP_RETURN_ON_ERROR(pcnt_new_channel(tach_unit, &tach_config, &tach_channel), TAG, "Failed to create fan tach channel");
    ESP_RETURN_ON_ERROR(
        pcnt_channel_set_edge_action(tach_channel, PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE),
        TAG,
        "Failed to configure fan tach edges");
    ESP_RETURN_ON_ERROR(pcnt_unit_enable(tach_unit), TAG, "Failed to enable fan tach counter");
    ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(tach_unit), TAG, "Failed to clear fan tach counter");
    ESP_RETURN_ON_ERROR(pcnt_unit_start(tach_unit), TAG, "Failed to start fan tach counter");

    tach_sample_start_us = esp_timer_get_time();
    initialized = true;
    ESP_LOGI(TAG, "Fan PWM GPIO%d at %d Hz; tach GPIO%d", FAN_PWM_GPIO, FAN_PWM_FREQUENCY_HZ, FAN_TACH_GPIO);
    return ESP_OK;
}

esp_err_t ESP32Fan_set_speed(float percent)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!isfinite(percent)) {
        return ESP_ERR_INVALID_ARG;
    }

    percent = fminf(fmaxf(percent, 0.0f), 1.0f);
    uint32_t duty = FAN_PWM_MAX_DUTY - (uint32_t)lroundf(percent * FAN_PWM_MAX_DUTY);
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty), TAG, "Failed to set fan PWM duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0), TAG, "Failed to update fan PWM duty");
    return ESP_OK;
}

uint16_t ESP32Fan_get_speed(void)
{
    if (!initialized) {
        return 0;
    }

    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_us = now_us - tach_sample_start_us;
    if (elapsed_us < FAN_TACH_SAMPLE_US) {
        return cached_rpm;
    }

    int pulse_count;
    esp_err_t err = pcnt_unit_get_count(tach_unit, &pulse_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read fan tach counter: %s", esp_err_to_name(err));
        return cached_rpm;
    }
    err = pcnt_unit_clear_count(tach_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear fan tach counter: %s", esp_err_to_name(err));
        return cached_rpm;
    }

    uint64_t rpm = ((uint64_t)pulse_count * 60000000ULL) /
                   ((uint64_t)elapsed_us * FAN_TACH_PULSES_PER_REVOLUTION);
    cached_rpm = rpm > UINT16_MAX ? UINT16_MAX : (uint16_t)rpm;
    tach_sample_start_us = now_us;
    return cached_rpm;
}
