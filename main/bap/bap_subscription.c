/**
 * @file bap_subscription.c
 * @brief BAP subscription management
 * 
 * Handles parameter subscriptions, periodic updates, and subscription timeouts.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "global_state.h"
#include "bap_subscription.h"
#include "bap_protocol.h"
#include "bap_uart.h"
#include "bap.h"
#include "connect.h"
#include "nvs_config.h"
#include "system.h"
#include "asic.h"
#include <math.h>

static const char *TAG = "BAP_SUBSCRIPTION";

static bap_subscription_t subscriptions[BAP_PARAM_UNKNOWN] = {0};
static TaskHandle_t subscription_task_handle = NULL;

// Per-chip lists for accessories: up to four chips of up to four domains.
#define BAP_MAX_CHIPS 4
#define BAP_CHIP_LIST_LEN 96 // 16 values of up to five digits, separators and NUL

typedef struct {
    char hashrate[32];
    char frequency[32];
    char chip_temp[32];
    char vr_temp[32];
    char power[32];
    char voltage[32];
    char current[32];
    char shares[64];
    char fan_speed[32];
    char best_difficulty[32];
    char block_height[32];
    char wifi_ssid[64];
    char wifi_password[64];
    char wifi_rssi[32];
    char wifi_ip[32];
    char found_block[16];
    char network_target[65];
    char best_share_hash[65];
    char last_share_hash[65];
    char asic_temp[BAP_CHIP_LIST_LEN];
    char asic_voltage[BAP_CHIP_LIST_LEN];
    char asic_frequency[BAP_CHIP_LIST_LEN];
    char asic_hashrate[BAP_CHIP_LIST_LEN];
    char domain_voltage[BAP_CHIP_LIST_LEN];
    char domain_frequency[BAP_CHIP_LIST_LEN];
    char domain_hashrate[BAP_CHIP_LIST_LEN];
} bap_last_values_t;

typedef struct {
    bool hashrate;
    bool frequency;
    bool chip_temp;
    bool vr_temp;
    bool power;
    bool voltage;
    bool current;
    bool shares;
    bool fan_speed;
    bool best_difficulty;
    bool block_height;
    bool wifi_ssid;
    bool wifi_password;
    bool wifi_rssi;
    bool wifi_ip;
    bool found_block;
    bool network_target;
    bool best_share_hash;
    bool last_share_hash;
    bool asic_temp;
    bool asic_voltage;
    bool asic_frequency;
    bool asic_hashrate;
    bool domain_voltage;
    bool domain_frequency;
    bool domain_hashrate;
} bap_last_values_valid_t;

static bap_last_values_t last_values = {0};
static bap_last_values_valid_t last_values_valid = {0};

static void subscription_update_task(void *pvParameters);
static void BAP_send_if_changed(const char *parameter, const char *value, char *last, size_t last_size, bool *is_valid);

// "v,v,v" with one whole number per chip, or "a/b,a/b" with one per domain.
// Returns false, leaving nothing to send, if any value is missing or negative.
static bool format_chip_table(char *out, size_t size, const float values[][ASIC_DETAIL_MAX_DOMAINS],
                              size_t chips, size_t domains)
{
    size_t used = 0;
    out[0] = '\0';
    if (chips == 0 || domains == 0) return false;
    for (size_t chip = 0; chip < chips; ++chip) {
        for (size_t domain = 0; domain < domains; ++domain) {
            float value = values[chip][domain];
            if (!isfinite(value) || value < 0) return false;
            const char *separator = chip == 0 && domain == 0 ? "" : domain == 0 ? "," : "/";
            int n = snprintf(out + used, size - used, "%s%.0f", separator, value);
            if (n < 0 || (size_t)n >= size - used) return false;
            used += (size_t)n;
        }
    }
    return true;
}

static void send_chip_table(const char *parameter, const float values[][ASIC_DETAIL_MAX_DOMAINS],
                            size_t chips, size_t domains, char *last, bool *is_valid)
{
    char text[BAP_CHIP_LIST_LEN];
    if (format_chip_table(text, sizeof(text), values, chips, domains)) {
        BAP_send_if_changed(parameter, text, last, BAP_CHIP_LIST_LEN, is_valid);
    }
}

// The per-chip and per-domain lists behind bonanzaDisplay's ASIC page. Each
// list is sent only when every chip has a value for it.
static void send_chip_details(GlobalState *state)
{
    size_t chips = state->DEVICE_CONFIG.family.asic_count;
    if (chips == 0 || chips > BAP_MAX_CHIPS) return;

    asic_chip_detail_t details[BAP_MAX_CHIPS];
    size_t detailed = ASIC_get_chip_details(state, details, BAP_MAX_CHIPS);
    size_t domains = detailed == chips ? details[0].domain_count : 0;
    bool all_temperature = domains > 0, all_voltage = domains > 0, all_frequency = domains > 0;
    for (size_t chip = 0; chip < detailed; ++chip) {
        if (details[chip].domain_count != domains) domains = 0;
        all_temperature &= details[chip].has_temperature;
        all_voltage &= details[chip].has_voltage;
        all_frequency &= details[chip].has_frequency;
    }
    if (domains == 0 || domains > ASIC_DETAIL_MAX_DOMAINS) {
        all_temperature = all_voltage = all_frequency = false;
        domains = 0;
    }

    float table[BAP_MAX_CHIPS][ASIC_DETAIL_MAX_DOMAINS];
    if (all_temperature) {
        for (size_t chip = 0; chip < chips; ++chip) table[chip][0] = details[chip].temperature_c;
        send_chip_table("asicTemp", table, chips, 1, last_values.asic_temp, &last_values_valid.asic_temp);
    }
    if (all_voltage) {
        // The domains of a chip are in series, so the chip's voltage is their sum.
        for (size_t chip = 0; chip < chips; ++chip) {
            table[chip][0] = 0;
            for (size_t domain = 0; domain < domains; ++domain) table[chip][0] += details[chip].domain_voltage_mv[domain];
        }
        send_chip_table("asicVoltage", table, chips, 1, last_values.asic_voltage, &last_values_valid.asic_voltage);
        for (size_t chip = 0; chip < chips; ++chip) memcpy(table[chip], details[chip].domain_voltage_mv, sizeof(table[chip]));
        send_chip_table("domainVoltage", table, chips, domains, last_values.domain_voltage, &last_values_valid.domain_voltage);
    }
    if (all_frequency) {
        // A chip's frequency is the mean of its domain clocks.
        for (size_t chip = 0; chip < chips; ++chip) {
            table[chip][0] = 0;
            for (size_t domain = 0; domain < domains; ++domain) table[chip][0] += details[chip].domain_frequency_mhz[domain];
            table[chip][0] /= (float)domains;
        }
        send_chip_table("asicFrequency", table, chips, 1, last_values.asic_frequency, &last_values_valid.asic_frequency);
        for (size_t chip = 0; chip < chips; ++chip) memcpy(table[chip], details[chip].domain_frequency_mhz, sizeof(table[chip]));
        send_chip_table("domainFrequency", table, chips, domains, last_values.domain_frequency, &last_values_valid.domain_frequency);
    }

    bool all_hashrate = true;
    for (size_t chip = 0; chip < chips && all_hashrate; ++chip) {
        asic_domain_measurement_t measurement = {0};
        all_hashrate = ASIC_get_asic_measurement(state, (uint8_t)chip, &measurement) == ESP_OK;
        table[chip][0] = measurement.hashrate;
    }
    if (all_hashrate) {
        send_chip_table("asicHashrate", table, chips, 1, last_values.asic_hashrate, &last_values_valid.asic_hashrate);
    }

    // Hash-counting domains are only sent when they cannot be confused with
    // the rail/clock domains above: BZM counts per chip, so it sends none.
    size_t hash_domains = state->DEVICE_CONFIG.family.asic.hash_domains;
    if (hash_domains > 1 && hash_domains <= ASIC_DETAIL_MAX_DOMAINS && (domains == 0 || domains == hash_domains)) {
        bool all_domain_hashrate = true;
        for (size_t chip = 0; chip < chips && all_domain_hashrate; ++chip) {
            for (size_t domain = 0; domain < hash_domains && all_domain_hashrate; ++domain) {
                asic_domain_measurement_t measurement = {0};
                all_domain_hashrate = ASIC_get_domain_measurement(state, (uint8_t)chip, (uint8_t)domain, &measurement) == ESP_OK;
                table[chip][domain] = measurement.hashrate;
            }
        }
        if (all_domain_hashrate) {
            send_chip_table("domainHashrate", table, chips, hash_domains, last_values.domain_hashrate, &last_values_valid.domain_hashrate);
        }
    }
}

static void BAP_send_if_changed(const char *parameter, const char *value, char *last, size_t last_size, bool *is_valid) {
    const char *safe_value = value ? value : "";

    if (*is_valid && strcmp(last, safe_value) == 0) {
        return;
    }

    snprintf(last, last_size, "%s", safe_value);
    *is_valid = true;
    BAP_send_message_with_queue(BAP_CMD_RES, parameter, safe_value);
}

esp_err_t BAP_subscription_init(void) {
    //ESP_LOGI(TAG, "Initializing BAP subscription management");
    
    memset(subscriptions, 0, sizeof(subscriptions));
    memset(&last_values, 0, sizeof(last_values));
    memset(&last_values_valid, 0, sizeof(last_values_valid));
    
    //ESP_LOGI(TAG, "BAP subscription management initialized");
    return ESP_OK;
}

void BAP_subscription_handle_subscribe(const char *parameter, const char *value) {
    //ESP_LOGI(TAG, "Handling subscription request for parameter: %s", parameter);
    
    if (!parameter) {
        ESP_LOGE(TAG, "Invalid subscription parameter");
        return;
    }

    bap_parameter_t param = BAP_parameter_from_string(parameter);
    //ESP_LOGI(TAG, "Parameter ID: %d (from string: %s)", param, parameter);
    
    if (param == BAP_PARAM_UNKNOWN) {
        ESP_LOGE(TAG, "Unknown subscription parameter: %s", parameter);
        return;
    }

    // Take the mutex to protect the subscriptions array
    if (bap_subscription_mutex != NULL && xSemaphoreTake(bap_subscription_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint32_t current_time = esp_timer_get_time() / 1000;
        
        subscriptions[param].active = true;
        subscriptions[param].last_subscribe = current_time;
        subscriptions[param].last_response = 0;
        subscriptions[param].update_interval_ms = 3000;

        switch (param) {
            case BAP_PARAM_HASHRATE:
                last_values_valid.hashrate = false;
                break;
            case BAP_PARAM_FREQUENCY:
                last_values_valid.frequency = false;
                break;
            case BAP_PARAM_TEMPERATURE:
                last_values_valid.chip_temp = false;
                last_values_valid.vr_temp = false;
                last_values_valid.asic_temp = false;
                last_values_valid.asic_voltage = false;
                last_values_valid.asic_frequency = false;
                last_values_valid.asic_hashrate = false;
                last_values_valid.domain_voltage = false;
                last_values_valid.domain_frequency = false;
                last_values_valid.domain_hashrate = false;
                break;
            case BAP_PARAM_POWER:
                last_values_valid.power = false;
                break;
            case BAP_PARAM_VOLTAGE:
                last_values_valid.voltage = false;
                break;
            case BAP_PARAM_CURRENT:
                last_values_valid.current = false;
                break;
            case BAP_PARAM_SHARES:
                last_values_valid.shares = false;
                break;
            case BAP_PARAM_FAN_SPEED:
                last_values_valid.fan_speed = false;
                break;
            case BAP_PARAM_BEST_DIFFICULTY:
                last_values_valid.best_difficulty = false;
                last_values_valid.best_share_hash = false;
                last_values_valid.last_share_hash = false;
                break;
            case BAP_PARAM_BLOCK_HEIGHT:
                last_values_valid.block_height = false;
                last_values_valid.network_target = false;
                break;
            case BAP_PARAM_WIFI:
                last_values_valid.wifi_ssid = false;
                last_values_valid.wifi_password = false;
                last_values_valid.wifi_rssi = false;
                last_values_valid.wifi_ip = false;
                break;
            case BAP_PARAM_FOUND_BLOCK:
                last_values_valid.found_block = false;
                break;
            default:
                break;
        }

        if (value) {
            int interval = atoi(value);
            if (interval > 0) {
                subscriptions[param].update_interval_ms = interval;
            }
        }

        ESP_LOGI(TAG, "Subscription activated for %s with interval %lu ms",
                 BAP_parameter_to_string(param), subscriptions[param].update_interval_ms);
        
        // Optionally, for debugging purposes, log the current subscription status
        //ESP_LOGI(TAG, "Current subscription status:");
        //for (int i = 0; i < BAP_PARAM_UNKNOWN; i++) {
        //    ESP_LOGI(TAG, "  %s: active=%d, interval=%lu ms",
        //             BAP_parameter_to_string((bap_parameter_t)i), subscriptions[i].active,
        //             subscriptions[i].update_interval_ms);
        //}

        BAP_send_message(BAP_CMD_ACK, parameter, "subscribed");

        xSemaphoreGive(bap_subscription_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take subscription mutex");
    }
}

void BAP_subscription_handle_unsubscribe(const char *parameter, const char *value) {
    //ESP_LOGI(TAG, "Handling unsubscription request for parameter: %s", parameter);
    
    if (!parameter) {
        ESP_LOGE(TAG, "Invalid unsubscription parameter");
        return;
    }

    bap_parameter_t param = BAP_parameter_from_string(parameter);
    if (param == BAP_PARAM_UNKNOWN) {
        ESP_LOGE(TAG, "Unknown unsubscription parameter: %s", parameter);
        return;
    }

    if (bap_subscription_mutex != NULL && xSemaphoreTake(bap_subscription_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        subscriptions[param].active = false;
        
        //ESP_LOGI(TAG, "Subscription deactivated for %s", BAP_parameter_to_string(param));

        BAP_send_message(BAP_CMD_ACK, parameter, "unsubscribed");

        xSemaphoreGive(bap_subscription_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take subscription mutex");
    }
}

void BAP_send_subscription_update(GlobalState *state) {
    if (!state) {
        ESP_LOGE(TAG, "Invalid global state");
        return;
    }

    uint32_t current_time = esp_timer_get_time() / 1000;

    if (bap_subscription_mutex != NULL && xSemaphoreTake(bap_subscription_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        const uint32_t SUBSCRIPTION_TIMEOUT_MS = 5 * 60 * 1000;

        for (int i = 0; i < BAP_PARAM_UNKNOWN; i++) {
            // Check for subscription timeout (5 minutes without refresh)
            if (subscriptions[i].active &&
                (current_time - subscriptions[i].last_subscribe > SUBSCRIPTION_TIMEOUT_MS)) {
                ESP_LOGW(TAG, "Subscription for %s timed out after 5 minutes, deactivating",
                         BAP_parameter_to_string((bap_parameter_t)i));
                subscriptions[i].active = false;
                BAP_send_message_with_queue(BAP_CMD_STA, BAP_parameter_to_string((bap_parameter_t)i), "subscription_timeout");
                continue;
            }
            
            if (subscriptions[i].active &&
                (current_time - subscriptions[i].last_response >= subscriptions[i].update_interval_ms)) {
                
                //ESP_LOGI(TAG, "Sending update for %s", BAP_parameter_to_string((bap_parameter_t)i));
                
                subscriptions[i].last_response = current_time;
                
                switch (i) {   
                    case BAP_PARAM_HASHRATE:
                        {
                            char hashrate_str[32];
                            snprintf(hashrate_str, sizeof(hashrate_str), "%.2f", state->SYSTEM_MODULE.current_hashrate);
                            BAP_send_if_changed("hashrate", hashrate_str, last_values.hashrate, sizeof(last_values.hashrate), &last_values_valid.hashrate);
                        }
                        break;
                        
                    case BAP_PARAM_FREQUENCY:
                        {
                            char frequency_str[32];
                            snprintf(frequency_str, sizeof(frequency_str), "%.2f", state->POWER_MANAGEMENT_MODULE.actual_frequency);
                            BAP_send_if_changed("frequency", frequency_str, last_values.frequency, sizeof(last_values.frequency), &last_values_valid.frequency);
                        }
                        break;

                    case BAP_PARAM_TEMPERATURE:
                        {
                            char temp_str[32];
                            snprintf(temp_str, sizeof(temp_str), "%f", state->POWER_MANAGEMENT_MODULE.chip_temp_avg);
                            BAP_send_if_changed("chipTemp", temp_str, last_values.chip_temp, sizeof(last_values.chip_temp), &last_values_valid.chip_temp);
                            
                            snprintf(temp_str, sizeof(temp_str), "%f", state->POWER_MANAGEMENT_MODULE.vr_temp);
                            BAP_send_if_changed("vrTemp", temp_str, last_values.vr_temp, sizeof(last_values.vr_temp), &last_values_valid.vr_temp);

                            send_chip_details(state);
                        }
                        break;
                        
                    case BAP_PARAM_POWER:
                        {
                            char power_str[32];
                            snprintf(power_str, sizeof(power_str), "%.2f", state->POWER_MANAGEMENT_MODULE.power);
                            BAP_send_if_changed("power", power_str, last_values.power, sizeof(last_values.power), &last_values_valid.power);
                        }
                        break;
                        
                    case BAP_PARAM_VOLTAGE:
                        {
                            char voltage_str[32];
                            snprintf(voltage_str, sizeof(voltage_str), "%.2f", state->POWER_MANAGEMENT_MODULE.voltage);
                            BAP_send_if_changed("voltage", voltage_str, last_values.voltage, sizeof(last_values.voltage), &last_values_valid.voltage);
                        }
                        break;
                        
                    case BAP_PARAM_CURRENT:
                        {
                            char current_str[32];
                            snprintf(current_str, sizeof(current_str), "%.2f", state->POWER_MANAGEMENT_MODULE.current);
                            BAP_send_if_changed("current", current_str, last_values.current, sizeof(last_values.current), &last_values_valid.current);
                        }
                        break;
                        
                    case BAP_PARAM_SHARES:
                        {
                            char shares_ar_str[64];
                            snprintf(shares_ar_str, sizeof(shares_ar_str), "%" PRIu64 "/%" PRIu64, state->SYSTEM_MODULE.shares_accepted, state->SYSTEM_MODULE.shares_rejected);
                            BAP_send_if_changed("shares", shares_ar_str, last_values.shares, sizeof(last_values.shares), &last_values_valid.shares);
                            
                        }
                        break;

                    case BAP_PARAM_FAN_SPEED:
                        {
                            char fan_speed_str[32];
                            snprintf(fan_speed_str, sizeof(fan_speed_str), "%d", state->POWER_MANAGEMENT_MODULE.fan_rpm);
                            BAP_send_if_changed("fan_speed", fan_speed_str, last_values.fan_speed, sizeof(last_values.fan_speed), &last_values_valid.fan_speed);
                        }
                        break;
                    
                    case BAP_PARAM_BEST_DIFFICULTY:
                        {
                            char best_diff_str[32];
                            snprintf(best_diff_str, sizeof(best_diff_str), "%s", state->SYSTEM_MODULE.best_diff_string);
                            BAP_send_if_changed("best_difficulty", best_diff_str, last_values.best_difficulty, sizeof(last_values.best_difficulty), &last_values_valid.best_difficulty);

                            // 64 hex digits each; unknown values are not sent.
                            char network_target[65], best_share[65], last_share[65];
                            SYSTEM_get_display_hashes(state, network_target, best_share, last_share);
                            if (best_share[0]) {
                                BAP_send_if_changed("bestShareHash", best_share, last_values.best_share_hash, sizeof(last_values.best_share_hash), &last_values_valid.best_share_hash);
                            }
                            if (last_share[0]) {
                                BAP_send_if_changed("lastShareHash", last_share, last_values.last_share_hash, sizeof(last_values.last_share_hash), &last_values_valid.last_share_hash);
                            }
                        }
                        break;

                    case BAP_PARAM_BLOCK_HEIGHT:
                        {
                            char block_height_str[32];
                            snprintf(block_height_str, sizeof(block_height_str), "%d", state->block_height);
                            BAP_send_if_changed("block_height", block_height_str, last_values.block_height, sizeof(last_values.block_height), &last_values_valid.block_height);

                            // The network target changes with the job's nBits, so it rides with block height.
                            char network_target[65], best_share[65], last_share[65];
                            SYSTEM_get_display_hashes(state, network_target, best_share, last_share);
                            if (network_target[0]) {
                                BAP_send_if_changed("networkTarget", network_target, last_values.network_target, sizeof(last_values.network_target), &last_values_valid.network_target);
                            }
                        }
                        break;

                    case BAP_PARAM_WIFI:
                        {
                            char wifi_status_str[256];
                            char rssi_str[32];
                            char ip_str[32];
                            snprintf(wifi_status_str, sizeof(wifi_status_str), "%s", state->SYSTEM_MODULE.wifi_status);
                            
                            int8_t current_rssi = -128; // no connection
                            if (state->SYSTEM_MODULE.is_connected) {
                                get_wifi_current_rssi(&current_rssi);
                            }
                            snprintf(rssi_str, sizeof(rssi_str), "%d", current_rssi);
                            
                            snprintf(ip_str, sizeof(ip_str), "%s", state->SYSTEM_MODULE.ip_addr_str);
                            char *wifi_pass = nvs_config_get_string(NVS_CONFIG_WIFI_PASS);
                            if (!wifi_pass) {
                                wifi_pass = strdup("");
                            }
                            BAP_send_if_changed("wifi_ssid", state->SYSTEM_MODULE.ssid, last_values.wifi_ssid, sizeof(last_values.wifi_ssid), &last_values_valid.wifi_ssid);
                            BAP_send_if_changed("wifi_password", wifi_pass, last_values.wifi_password, sizeof(last_values.wifi_password), &last_values_valid.wifi_password);
                            BAP_send_if_changed("wifi_rssi", rssi_str, last_values.wifi_rssi, sizeof(last_values.wifi_rssi), &last_values_valid.wifi_rssi);
                            BAP_send_if_changed("wifi_ip", ip_str, last_values.wifi_ip, sizeof(last_values.wifi_ip), &last_values_valid.wifi_ip);
                            free(wifi_pass);
                        }
                        break;

                    case BAP_PARAM_FOUND_BLOCK:
                        {
                            char found_block_str[16];
                            snprintf(found_block_str, sizeof(found_block_str), "%d", state->SYSTEM_MODULE.block_found);
                            BAP_send_if_changed("found_block", found_block_str, last_values.found_block, sizeof(last_values.found_block), &last_values_valid.found_block);
                        }
                        break;

                    default:
                        break;
                }
            }
        }
        
        xSemaphoreGive(bap_subscription_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take subscription mutex");
    }
}

static void subscription_update_task(void *pvParameters) {
    GlobalState *state = (GlobalState *)pvParameters;
    
    while (1) {
        BAP_send_subscription_update(state);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    vTaskDelete(NULL);
}

static void mode_management_task(void *pvParameters) {
    GlobalState *state = (GlobalState *)pvParameters;
    bool was_connected = false;
    bool subscription_task_started = false;
    // will go into log dev mode
    //ESP_LOGI(TAG, "BAP mode management task started");
    
    while (1) {
        bool is_connected = state->SYSTEM_MODULE.is_connected;
        
        // Check for mode transitions
        if (!was_connected && !is_connected) {
            // AP mode - send periodic AP messages
            BAP_send_ap_message(state);
            vTaskDelay(pdMS_TO_TICKS(5000));
        } else if (!was_connected && is_connected) {
            // Transition from AP to connected mode
            //ESP_LOGI(TAG, "WiFi connected - switching to normal BAP mode");
            
            // Start subscription task for connected mode
            if (!subscription_task_started) {
                esp_err_t ret = BAP_start_subscription_task(state);
                if (ret == ESP_OK) {
                    subscription_task_started = true;
                    //ESP_LOGI(TAG, "Subscription task started for connected mode");
                } else {
                    ESP_LOGE(TAG, "Failed to start subscription task");
                }
            }
            
            was_connected = true;
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (was_connected && is_connected) {
            // Normal connected mode - subscription task handles updates
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (was_connected && !is_connected) {
            // Transition from connected to AP mode (connection lost)
            //ESP_LOGI(TAG, "WiFi disconnected - switching to AP mode");
            was_connected = false;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    
    vTaskDelete(NULL);
}

esp_err_t BAP_start_mode_management_task(GlobalState *state) {
    if (!state) {
        ESP_LOGE(TAG, "Invalid global state");
        return ESP_ERR_INVALID_ARG;
    }
    
    xTaskCreateWithCaps(
        mode_management_task,
        "bap_mode_mgmt",
        8192,
        state,
        4,
        NULL,
        MALLOC_CAP_SPIRAM
    );

    //ESP_LOGI(TAG, "BAP mode management task started");
    return ESP_OK;
}

esp_err_t BAP_start_subscription_task(GlobalState *state) {
    if (!state) {
        ESP_LOGE(TAG, "Invalid global state");
        return ESP_ERR_INVALID_ARG;
    }
    
    xTaskCreateWithCaps(
        subscription_update_task,
        "subscription_up",
        8192,
        state,
        4,
        &subscription_task_handle,
        MALLOC_CAP_SPIRAM
    );

    //ESP_LOGI(TAG, "Subscription update task started");
    return ESP_OK;
}
