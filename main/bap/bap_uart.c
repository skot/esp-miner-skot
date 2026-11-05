/**
 * @file bap_uart.c
 * @brief BAP UART communication layer
 * 
 * Handles UART initialization, message sending/receiving, and communication tasks.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_err.h"
#include "device_config.h"
#include "global_state.h"
#include "bap_uart.h"
#include "bap_protocol.h"
#include "bap.h"

#define BAP_UART_NUM UART_NUM_2
#define BAP_BUF_SIZE 1024
#define UART_SEND_QUEUE_SIZE 10
#define UART_SEND_QUEUE_ITEM_SIZE sizeof(bap_message_t)
#define UART_SEND_TIMEOUT_MS 1000
static QueueHandle_t uart_event_queue;

static const char *TAG = "BAP_UART";

static TaskHandle_t uart_send_task_handle = NULL;
static TaskHandle_t uart_receive_task_handle = NULL;

static void uart_receive_task(void *pvParameters);
static void uart_send_task(void *pvParameters);

extern void BAP_parse_message(const char *message);

void BAP_send_message(bap_command_t cmd, const char *parameter, const char *value) {
    char message[BAP_MAX_MESSAGE_LEN + 1];
    size_t len = BAP_format_message(message, sizeof(message), BAP_command_to_string(cmd), parameter, value);
    if (!len) return;

    if (bap_uart_send_mutex != NULL && xSemaphoreTake(bap_uart_send_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uart_write_bytes(BAP_UART_NUM, message, len);
        xSemaphoreGive(bap_uart_send_mutex);
        
        //ESP_LOGI(TAG, "Sent: %s", message);
    } else {
        ESP_LOGW(TAG, "Failed to take UART mutex for immediate send, message dropped");
    }
}

void BAP_send_message_with_queue(bap_command_t cmd, const char *parameter, const char *value) {
    char message[BAP_MAX_MESSAGE_LEN + 1];
    size_t len = BAP_format_message(message, sizeof(message), BAP_command_to_string(cmd), parameter, value);
    if (!len || !bap_uart_send_queue) return;
    bap_message_t msg;

    msg.message = malloc(len + 1);
    if (msg.message == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for message");
        return;
    }
    
    strncpy(msg.message, message, len);
    msg.message[len] = '\0';
    msg.length = len;

    if (xQueueSend(bap_uart_send_queue, &msg, UART_SEND_TIMEOUT_MS / portTICK_PERIOD_MS) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to queue message, dropping");
        free(msg.message);
    }
}

void BAP_send_init_message(GlobalState *state) {
    const char *init_message = "BAP UART Interface Initialized\r\n";
    esp_err_t ret = uart_write_bytes(BAP_UART_NUM, init_message, strlen(init_message));
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to send init message: %d", ret);
    } else {
        //ESP_LOGI(TAG, "Init message sent: %s", init_message);
    }
}

void BAP_send_ap_message(GlobalState *state) {
    BAP_send_message(BAP_CMD_CMD, "mode", "ap_mode");
}


static void uart_receive_task(void *pvParameters) {
    uint8_t *data = (uint8_t *) malloc(BAP_BUF_SIZE);
    if (!data) {
        ESP_LOGE(TAG, "Failed to allocate memory for UART receive buffer");
        vTaskDelete(NULL);
        return;
    }

    bap_stream_t stream = {0};
    uart_event_t event;
    while (1) {
        if (xQueueReceive(uart_event_queue, &event, portMAX_DELAY) != pdTRUE) continue;
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
            event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR || event.type == UART_BREAK) {
            stream.length = 0;
            uart_flush_input(BAP_UART_NUM);
            xQueueReset(uart_event_queue);
            continue;
        }
        if (event.type != UART_DATA) continue;
        size_t remaining = event.size;
        while (remaining) {
            int len = uart_read_bytes(BAP_UART_NUM, data, remaining < BAP_BUF_SIZE ? remaining : BAP_BUF_SIZE, 0);
            if (len <= 0) {
                stream.length = 0;
                break;
            }
            remaining -= len;
            for (int i = 0; i < len; ++i) {
                if (BAP_stream_feed(&stream, data[i])) BAP_parse_message(stream.message);
            }
        }
    }

    free(data);
    vTaskDelete(NULL);
}

static void uart_send_task(void *pvParameters) {
    bap_message_t msg;
    while (1) {
        if (xQueueReceive(bap_uart_send_queue, &msg, portMAX_DELAY) == pdTRUE) {
            
            if (bap_uart_send_mutex != NULL && xSemaphoreTake(bap_uart_send_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                int bytes_sent = uart_write_bytes(BAP_UART_NUM, msg.message, msg.length);
                if (bytes_sent < 0 || (size_t)bytes_sent != msg.length) {
                    ESP_LOGW(TAG, "UART send failed or partial: %d of %zu bytes", bytes_sent, msg.length);
                }
                xSemaphoreGive(bap_uart_send_mutex);
            } else {
                ESP_LOGW(TAG, "Failed to take UART send mutex, dropping message");
            }

            if (msg.message) {
                free(msg.message);
                msg.message = NULL;
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

esp_err_t BAP_start_uart_receive_task(void) {
    BaseType_t result = xTaskCreateWithCaps(
        uart_receive_task,
        "uart_receive_ta",
        8192,
        NULL,
        4,
        &uart_receive_task_handle,
        MALLOC_CAP_SPIRAM
    );

    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t BAP_uart_init(void) {
    //ESP_LOGI(TAG, "Initializing BAP UART interface");
    
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    
    esp_err_t ret = uart_param_config(BAP_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART parameters: %d", ret);
        return ret;
    }
    
    gpio_num_t bap_tx = bap_global_state->DEVICE_CONFIG.pins.bap->tx;
    gpio_num_t bap_rx = bap_global_state->DEVICE_CONFIG.pins.bap->rx;

    ret = uart_set_pin(BAP_UART_NUM, bap_tx, bap_rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %d", ret);
        return ret;
    }
    
    ret = uart_driver_install(BAP_UART_NUM, BAP_BUF_SIZE, BAP_BUF_SIZE, 20, &uart_event_queue, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %d", ret);
        return ret;
    }
    
    
    BaseType_t task_result = xTaskCreateWithCaps(
        uart_send_task,
        "uart_send_task",
        8192,
        NULL,
        4,
        &uart_send_task_handle,
        MALLOC_CAP_SPIRAM
    );
    
    if (task_result != pdPASS) {
        uart_driver_delete(BAP_UART_NUM);
        ESP_LOGE(TAG, "Failed to create uart_send_task");
        return ESP_ERR_NO_MEM;
    }
    
    //ESP_LOGI(TAG, "UART send task created successfully");
    return ESP_OK;
}