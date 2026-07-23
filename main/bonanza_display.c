#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bonanza_display.h"
#include "global_state.h"
#include "nvs_config.h"

#define BONANZA_I2C_ADDRESS          0x3C
#define BONANZA_I2C_SPEED_HZ         100000U
// Dedicated bonanzaDisplay FPC bus; the miner PMBus remains on GPIO47/48.
#define BONANZA_I2C_PORT             I2C_NUM_1
#define BONANZA_I2C_SDA_GPIO         GPIO_NUM_7
#define BONANZA_I2C_SCL_GPIO         GPIO_NUM_6
#define BONANZA_PROTOCOL_VERSION     1
#define BONANZA_REGISTER_FILE_SIZE   128

#define BONANZA_REG_PROTOCOL_VERSION 0x00
#define BONANZA_REG_DEVICE_FAMILY    0x10
#define BONANZA_REG_DEVICE_MODEL     0x20
#define BONANZA_REG_DEVICE_NAME      0x28
#define BONANZA_REG_IP_ADDRESS       0x38
#define BONANZA_REG_BEST_SHARE       0x48
#define BONANZA_REG_HASHRATE_GHS     0x60
#define BONANZA_REG_TEMPERATURE_C    0x64
#define BONANZA_REG_POWER_W          0x68
#define BONANZA_REG_FREQUENCY_MHZ    0x6C
#define BONANZA_REG_FAN_PERCENT      0x70
#define BONANZA_REG_LAST_BYTE        (BONANZA_REG_FAN_PERCENT + sizeof(uint32_t) - 1)

#define BONANZA_DEVICE_FAMILY_SIZE   16
#define BONANZA_DEVICE_MODEL_SIZE    8
#define BONANZA_DEVICE_NAME_SIZE     16
#define BONANZA_IP_ADDRESS_SIZE      16
#define BONANZA_BEST_SHARE_SIZE      16

#define BONANZA_PACKET_SIZE          (1 + BONANZA_REG_LAST_BYTE - BONANZA_REG_DEVICE_FAMILY + 1)
#define BONANZA_IO_TIMEOUT_MS        250
#define BONANZA_REFRESH_MS           1000
#define BONANZA_RECONNECT_MS         5000

static const char * TAG = "bonanza_display";

static i2c_master_bus_handle_t bonanza_bus_handle;
static i2c_master_dev_handle_t bonanza_device_handle;
static char configured_device_name[BONANZA_DEVICE_NAME_SIZE] = "bitaxe";

_Static_assert(BONANZA_PACKET_SIZE == 101, "Unexpected bonanzaDisplay packet size");

static size_t packet_index(uint8_t reg)
{
    return 1 + reg - BONANZA_REG_DEVICE_FAMILY;
}

static void packet_write_string(uint8_t * packet, uint8_t reg, size_t field_size, const char * value, bool uppercase)
{
    if (value == NULL) {
        return;
    }

    size_t offset = packet_index(reg);
    size_t length = strnlen(value, field_size - 1);
    for (size_t i = 0; i < length; i++) {
        unsigned char character = (unsigned char) value[i];
        packet[offset + i] = uppercase ? (uint8_t) toupper(character) : character;
    }
}

static void packet_write_u32(uint8_t * packet, uint8_t reg, uint32_t value)
{
    size_t offset = packet_index(reg);
    packet[offset] = (uint8_t) value;
    packet[offset + 1] = (uint8_t) (value >> 8);
    packet[offset + 2] = (uint8_t) (value >> 16);
    packet[offset + 3] = (uint8_t) (value >> 24);
}

static uint32_t metric_to_u32(float value)
{
    if (!isfinite(value) || value <= 0.0f) {
        return 0;
    }
    if (value >= (float) UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t) (value + 0.5f);
}

static uint32_t fan_percent_to_u32(float value)
{
    uint32_t percent = metric_to_u32(value);
    return percent > 100 ? 100 : percent;
}

static void build_metrics_packet(const GlobalState * global_state, uint8_t packet[BONANZA_PACKET_SIZE])
{
    const SystemModule * system = &global_state->SYSTEM_MODULE;
    const PowerManagementModule * power = &global_state->POWER_MANAGEMENT_MODULE;

    memset(packet, 0, BONANZA_PACKET_SIZE);
    packet[0] = BONANZA_REG_DEVICE_FAMILY;

    const char * device_name = system->mdns_hostname[0] != '\0'
        ? system->mdns_hostname
        : configured_device_name;

    packet_write_string(packet, BONANZA_REG_DEVICE_FAMILY, BONANZA_DEVICE_FAMILY_SIZE,
                        global_state->DEVICE_CONFIG.family.name, true);
    packet_write_string(packet, BONANZA_REG_DEVICE_MODEL, BONANZA_DEVICE_MODEL_SIZE,
                        global_state->DEVICE_CONFIG.board_version, false);
    packet_write_string(packet, BONANZA_REG_DEVICE_NAME, BONANZA_DEVICE_NAME_SIZE,
                        device_name, false);
    packet_write_string(packet, BONANZA_REG_IP_ADDRESS, BONANZA_IP_ADDRESS_SIZE,
                        system->ip_addr_str, false);
    packet_write_string(packet, BONANZA_REG_BEST_SHARE, BONANZA_BEST_SHARE_SIZE,
                        system->best_diff_string, false);

    float temperature = power->chip_temp_avg;
    if (power->chip_temp2_avg > temperature) {
        temperature = power->chip_temp2_avg;
    }

    packet_write_u32(packet, BONANZA_REG_HASHRATE_GHS, metric_to_u32(system->current_hashrate));
    packet_write_u32(packet, BONANZA_REG_TEMPERATURE_C, metric_to_u32(temperature));
    packet_write_u32(packet, BONANZA_REG_POWER_W, metric_to_u32(power->power));
    packet_write_u32(packet, BONANZA_REG_FREQUENCY_MHZ, metric_to_u32(power->actual_frequency));
    packet_write_u32(packet, BONANZA_REG_FAN_PERCENT, fan_percent_to_u32(power->fan_perc));
}

static esp_err_t probe_display(void)
{
    const uint8_t register_address = BONANZA_REG_PROTOCOL_VERSION;
    uint8_t identity[3] = {0};
    esp_err_t error = i2c_master_transmit_receive(
        bonanza_device_handle,
        &register_address,
        sizeof(register_address),
        identity,
        sizeof(identity),
        BONANZA_IO_TIMEOUT_MS);

    if (error != ESP_OK) {
        return error;
    }
    if (identity[0] != BONANZA_PROTOCOL_VERSION ||
        identity[1] != BONANZA_I2C_ADDRESS ||
        identity[2] != BONANZA_REGISTER_FILE_SIZE) {
        ESP_LOGW(TAG, "Unsupported device at 0x%02X: protocol=%u address=0x%02X registers=%u",
                 BONANZA_I2C_ADDRESS, identity[0], identity[1], identity[2]);
        return ESP_ERR_INVALID_VERSION;
    }

    return ESP_OK;
}

static esp_err_t publish_metrics(const GlobalState * global_state)
{
    uint8_t packet[BONANZA_PACKET_SIZE];
    build_metrics_packet(global_state, packet);
    return i2c_master_transmit(
        bonanza_device_handle,
        packet,
        sizeof(packet),
        BONANZA_IO_TIMEOUT_MS);
}

static void bonanza_display_task(void * pvParameters)
{
    GlobalState * global_state = (GlobalState *) pvParameters;
    bool connected = false;
    bool absence_logged = false;

    while (true) {
        if (!connected) {
            esp_err_t error = probe_display();
            if (error != ESP_OK) {
                if (!absence_logged) {
                    ESP_LOGW(TAG, "bonanzaDisplay unavailable; mining will continue and the display will be retried");
                    absence_logged = true;
                }
                vTaskDelay(pdMS_TO_TICKS(BONANZA_RECONNECT_MS));
                continue;
            }

            connected = true;
            absence_logged = false;
            ESP_LOGI(TAG, "Connected to bonanzaDisplay protocol v%d at 0x%02X", BONANZA_PROTOCOL_VERSION, BONANZA_I2C_ADDRESS);
        }

        esp_err_t error = publish_metrics(global_state);
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "bonanzaDisplay disconnected: %s", esp_err_to_name(error));
            connected = false;
            vTaskDelay(pdMS_TO_TICKS(BONANZA_RECONNECT_MS));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(BONANZA_REFRESH_MS));
    }
}

esp_err_t bonanza_display_init(void * pvParameters)
{
    char * hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
    if (hostname != NULL) {
        size_t length = strnlen(hostname, sizeof(configured_device_name) - 1);
        memcpy(configured_device_name, hostname, length);
        configured_device_name[length] = '\0';
        free(hostname);
    }

    const i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BONANZA_I2C_PORT,
        .scl_io_num = BONANZA_I2C_SCL_GPIO,
        .sda_io_num = BONANZA_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &bonanza_bus_handle),
                        TAG, "Failed to initialize bonanzaDisplay I2C bus");

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BONANZA_I2C_ADDRESS,
        .scl_speed_hz = BONANZA_I2C_SPEED_HZ,
    };
    esp_err_t error = i2c_master_bus_add_device(
        bonanza_bus_handle, &device_config, &bonanza_device_handle);
    if (error != ESP_OK) {
        i2c_del_master_bus(bonanza_bus_handle);
        bonanza_bus_handle = NULL;
        ESP_RETURN_ON_ERROR(error, TAG, "Failed to add bonanzaDisplay I2C device");
    }

    ESP_LOGI(TAG, "bonanzaDisplay I2C bus initialized: SDA=GPIO%d SCL=GPIO%d speed=%u Hz",
             BONANZA_I2C_SDA_GPIO, BONANZA_I2C_SCL_GPIO, BONANZA_I2C_SPEED_HZ);

    if (xTaskCreate(bonanza_display_task, "bonanza display", 4096, pvParameters, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create bonanzaDisplay task");
        i2c_master_bus_rm_device(bonanza_device_handle);
        bonanza_device_handle = NULL;
        i2c_del_master_bus(bonanza_bus_handle);
        bonanza_bus_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
