#include "bitair.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define BITAIR_FAN_INPUT_GPIO GPIO_NUM_40
#define SIGNAL_SETTLE_US 500000
#define PWM_STALE_US 250000
#define MIN_PERIOD_US 8
#define MAX_PERIOD_US 100000
#define MIN_PULSE_US 2
#define REQUIRED_CYCLES 3
#define MODE_SETTLE_US 200000
#define AVERAGE_SHIFT 4

typedef struct {
    int64_t last_edge;
    int64_t last_rise;
    int64_t last_cycle;
    uint32_t high_us;
    uint32_t pulse_high_us;
    uint32_t period_us;
    uint8_t valid_cycles;
    uint8_t mode;
    uint8_t pending_mode;
    int64_t pending_since;
    int level;
    bool initialized;
} FanCapture;

typedef enum { MODE_UNKNOWN, MODE_OFF, MODE_SILENT, MODE_AUTO, MODE_TURBO } FanMode;

static portMUX_TYPE capture_lock = portMUX_INITIALIZER_UNLOCKED;
static FanCapture capture;

static void IRAM_ATTR fan_edge(void *arg)
{
    (void)arg;
    int level = gpio_get_level(BITAIR_FAN_INPUT_GPIO);
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&capture_lock);
    if (!capture.initialized || level == capture.level) {
        portEXIT_CRITICAL_ISR(&capture_lock);
        return;
    }
    capture.level = level;
    capture.last_edge = now;
    if (level) {
        int64_t period = now - capture.last_rise;
        if (capture.last_rise >= 0 && period >= MIN_PERIOD_US && period <= MAX_PERIOD_US &&
            capture.pulse_high_us >= MIN_PULSE_US && capture.pulse_high_us + MIN_PULSE_US <= period) {
            if (capture.valid_cycles == 0) {
                capture.period_us = (uint32_t)period << AVERAGE_SHIFT;
                capture.high_us = capture.pulse_high_us << AVERAGE_SHIFT;
            } else {
                capture.period_us -= capture.period_us >> AVERAGE_SHIFT;
                capture.period_us += (uint32_t)period;
                capture.high_us -= capture.high_us >> AVERAGE_SHIFT;
                capture.high_us += capture.pulse_high_us;
            }
            capture.last_cycle = now;
            if (capture.valid_cycles < REQUIRED_CYCLES) capture.valid_cycles++;
        } else {
            capture.valid_cycles = 0;
        }
        capture.last_rise = now;
        capture.pulse_high_us = 0;
    } else {
        int64_t high = now - capture.last_rise;
        if (capture.last_rise >= 0 && high >= MIN_PULSE_US && high <= MAX_PERIOD_US) {
            capture.pulse_high_us = (uint32_t)high;
        } else {
            capture.pulse_high_us = 0;
            capture.valid_cycles = 0;
        }
    }
    portEXIT_CRITICAL_ISR(&capture_lock);
}

esp_err_t bitair_init(void)
{
    if (capture.initialized) return ESP_OK;
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << BITAIR_FAN_INPUT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) return err;
    portENTER_CRITICAL(&capture_lock);
    capture = (FanCapture) {
        .last_edge = esp_timer_get_time(),
        .last_rise = -1,
        .level = gpio_get_level(BITAIR_FAN_INPUT_GPIO),
    };
    portEXIT_CRITICAL(&capture_lock);
    err = gpio_isr_handler_add(BITAIR_FAN_INPUT_GPIO, fan_edge, NULL);
    if (err != ESP_OK) return err;
    portENTER_CRITICAL(&capture_lock);
    capture.initialized = true;
    portEXIT_CRITICAL(&capture_lock);
    return ESP_OK;
}

static FanMode get_raw_mode(const FanCapture *sample, int64_t now)
{
    if (!sample->initialized) return MODE_UNKNOWN;
    if (now - sample->last_edge >= SIGNAL_SETTLE_US) {
        return sample->level ? MODE_TURBO : MODE_OFF;
    }
    if (sample->valid_cycles >= REQUIRED_CYCLES && sample->level && now - sample->last_edge >= PWM_STALE_US) {
        return MODE_TURBO;
    }
    if (sample->valid_cycles < REQUIRED_CYCLES || now - sample->last_cycle > PWM_STALE_US) {
        return MODE_UNKNOWN;
    }
    uint32_t duty = sample->high_us * 100;
    if (duty < sample->period_us * 25) return MODE_OFF;
    if (duty <= sample->period_us * 65) return MODE_SILENT;
    if (duty <= sample->period_us * 85) return MODE_AUTO;
    return MODE_TURBO;
}

static FanMode get_mode(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&capture_lock);
    FanMode mode = get_raw_mode(&capture, now);
    if (mode == MODE_UNKNOWN || capture.mode == MODE_UNKNOWN || mode == capture.mode) {
        capture.mode = mode;
        capture.pending_mode = mode;
        capture.pending_since = now;
    } else if (capture.pending_mode != mode) {
        capture.pending_mode = mode;
        capture.pending_since = now;
    } else if (now - capture.pending_since >= MODE_SETTLE_US) {
        capture.mode = mode;
    }
    mode = (FanMode)capture.mode;
    portEXIT_CRITICAL(&capture_lock);
    return mode;
}

float bitair_get_fan_percent(void)
{
    switch (get_mode()) {
        case MODE_OFF: return 0;
        case MODE_SILENT: return 37.5f;
        case MODE_AUTO: return 56.25f;
        case MODE_TURBO: return 75;
        default: return 100;
    }
}

bool bitair_mining_disabled(void)
{
    FanMode mode = get_mode();
    return mode == MODE_OFF || mode == MODE_UNKNOWN;
}

const char *bitair_get_mode_name(void)
{
    switch (get_mode()) {
        case MODE_OFF: return "off";
        case MODE_SILENT: return "silent";
        case MODE_AUTO: return "auto";
        case MODE_TURBO: return "turbo";
        default: return "unknown";
    }
}
