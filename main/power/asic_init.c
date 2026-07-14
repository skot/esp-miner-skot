#include "asic_init.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "asic.h"
#include "asic_common.h"
#include "serial.h"
#include "asic_reset.h"
#include "device_config.h"
#include "driver/gpio.h"

#define GPIO_PROTO_VDDIO_5V_EN GPIO_NUM_21
#define PROTO_VDDIO_STABILIZATION_MS 100

static const char *TAG = "asic_init";

static esp_err_t enable_proto_vddio_5v(const GlobalState *GLOBAL_STATE)
{
    if (GLOBAL_STATE->DEVICE_CONFIG.family.id != PROTO) {
        return ESP_OK;
    }

    gpio_config_t vddio_5v_conf = {
        .pin_bit_mask = (1ULL << GPIO_PROTO_VDDIO_5V_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&vddio_5v_conf), TAG, "Failed to configure Proto VDDIO 5V enable GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_PROTO_VDDIO_5V_EN, 1), TAG, "Failed to enable Proto VDDIO 5V");
    ESP_LOGI(TAG, "Proto VDDIO 5V enabled on GPIO%d", GPIO_PROTO_VDDIO_5V_EN);

    return ESP_OK;
}

uint8_t asic_initialize(GlobalState *GLOBAL_STATE, asic_init_mode_t mode, uint32_t stabilization_delay_ms)
{
    const char *mode_str = (mode == ASIC_INIT_COLD_BOOT) ? "cold boot" : "recovery";
    ESP_LOGI(TAG, "Starting ASIC initialization (%s mode)", mode_str);

    if (asic_hold_reset_low() != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "ASIC reset hold failed";
        ESP_LOGE(TAG, "Failed to hold ASIC reset low");
        return 0;
    }

    // Check actual UART state for safety
    bool uart_initialized = SERIAL_is_initialized();
    
    // Verify mode matches actual state
    if (mode == ASIC_INIT_COLD_BOOT && uart_initialized) {
        ESP_LOGW(TAG, "Cold boot mode but UART already initialized - will reset baud only");
    } else if (mode == ASIC_INIT_RECOVERY && !uart_initialized) {
        ESP_LOGW(TAG, "Recovery mode but UART not initialized - will do full init");
    }
    
    // Use actual state for decision, not just mode
    if (!uart_initialized) {
        // Fresh boot - full UART initialization
        ESP_LOGI(TAG, "Performing full UART initialization");
        SERIAL_init();
    } else {
        // Live recovery - ASIC was reset, UART needs baud reset to 115200
        // This preserves the running system and avoids reboot
        ESP_LOGI(TAG, "UART already initialized, resetting baud to %d", UART_FREQ);
        SERIAL_set_baud(UART_FREQ);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    if (enable_proto_vddio_5v(GLOBAL_STATE) != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "Proto VDDIO 5V enable failed";
        return 0;
    }

    if (GLOBAL_STATE->DEVICE_CONFIG.family.id == PROTO) {
        ESP_LOGI(TAG, "Waiting %u ms for Proto VDDIO 5V to stabilize", PROTO_VDDIO_STABILIZATION_MS);
        vTaskDelay(pdMS_TO_TICKS(PROTO_VDDIO_STABILIZATION_MS));
    }

    if (asic_reset() != ESP_OK) {
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = "ASIC reset failed";
        ESP_LOGE(TAG, "ASIC reset failed!");
        return 0;
    }

    ESP_LOGI(TAG, "Detecting ASIC chips...");
    clear_asic_chain_error();
    uint8_t chip_count = ASIC_init(GLOBAL_STATE);
    
    if (chip_count == 0) {
        const char *chain_error = get_asic_chain_error();
        ESP_LOGE(TAG, "ASIC initialization failed - chip chain detection failed");
        GLOBAL_STATE->SYSTEM_MODULE.asic_status = chain_error != NULL ? chain_error : "ASIC chain detection failed";
        return 0;
    }

    ESP_LOGI(TAG, "Setting max baud rate and clearing buffers");
    SERIAL_set_baud(ASIC_set_max_baud(GLOBAL_STATE));
    SERIAL_clear_buffer();

    GLOBAL_STATE->ASIC_initalized = true;
    
    if (stabilization_delay_ms > 0) {
        ESP_LOGI(TAG, "Waiting %u ms for tasks to stabilize...", stabilization_delay_ms);
        vTaskDelay(stabilization_delay_ms / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "ASIC initialized successfully with %d chip(s) (%s mode)", chip_count, mode_str);
    return chip_count;
}
