/**
 * @file bap_protocol.c
 * @brief BAP (Bitaxe accessory protocol) core protocol utilities
 * 
 * Contains protocol-level functions for message formatting, checksums,
 * and string conversions between enums and strings.
 */

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "bap_protocol.h"

static const char *TAG = "BAP_PROTOCOL";

static const char *parameter_strings[] = {
    "systemInfo",
    "hashrate",
    "temperature",
    "power",
    "voltage",
    "current",
    "shares",
    "frequency",
    "asic_voltage",
    "ssid",
    "password",
    "fan_speed",
    "auto_fan",
    "best_difficulty",
    "block_height",
    "wifi",
    "found_block",
    "show_new_block",
    "unknown"
};

static const char *command_strings[] = {
    "REQ", "RES", "SUB", "UNSUB", "SET", "ACK", "ERR", "CMD", "STA", "LOG"
};

bap_parameter_t BAP_parameter_from_string(const char *param_str) {
    for (int i = 0; i < BAP_PARAM_UNKNOWN; ++i) {
        if (strcmp(param_str, parameter_strings[i]) == 0) {
            return (bap_parameter_t)i;
        }
    }
    return BAP_PARAM_UNKNOWN;
}

const char* BAP_parameter_to_string(bap_parameter_t param) {
    if (param >= 0 && param < BAP_PARAM_UNKNOWN) {
        return parameter_strings[param];
    }
    return "unknown";
}

bap_command_t BAP_command_from_string(const char *cmd_str) {
    for (int i = 0; i < BAP_CMD_UNKNOWN; ++i) {
        if (strcmp(cmd_str, command_strings[i]) == 0) {
            return (bap_command_t)i;
        }
    }
    return BAP_CMD_UNKNOWN;
}

const char* BAP_command_to_string(bap_command_t cmd) {
    if (cmd >= 0 && cmd < BAP_CMD_UNKNOWN) {
        return command_strings[cmd];
    }
    return "UNK";
}

uint8_t BAP_calculate_checksum(const char *sentence_body) {
    uint8_t checksum = 0;
    //ESP_LOGI(TAG, "Calculating checksum for: %s", sentence_body);
    
    while (*sentence_body) {
        ESP_LOGD(TAG, "XOR: 0x%02X ^ 0x%02X = 0x%02X",
                checksum, (uint8_t)(*sentence_body),
                checksum ^ (uint8_t)(*sentence_body));
        checksum ^= (uint8_t)(*sentence_body++);
    }
    
    //ESP_LOGI(TAG, "Final checksum: 0x%02X", checksum);
    return checksum;
}

static bool valid_text(const char *text, bool token) {
    if (!text || (token && !*text)) return false;
    for (; *text; ++text) {
        if ((unsigned char)*text < 0x20 || (unsigned char)*text > 0x7e ||
            *text == '$' || *text == '*' || (token && (*text == ',' || *text == ' '))) return false;
    }
    return true;
}

size_t BAP_format_message(char *out, size_t capacity, const char *command,
                          const char *parameter, const char *value) {
    if (!out || !capacity || !valid_text(command, true) || !valid_text(parameter, true) ||
        (value && !valid_text(value, false))) return 0;
    char body[BAP_MAX_MESSAGE_LEN];
    int n = value ? snprintf(body, sizeof(body), "BAP,%s,%s,%s", command, parameter, value)
                  : snprintf(body, sizeof(body), "BAP,%s,%s", command, parameter);
    if (n < 0 || (size_t)n + 6 > BAP_MAX_MESSAGE_LEN || (size_t)n + 6 >= capacity) return 0;
    n = snprintf(out, capacity, "$%s*%02X\r\n", body, BAP_calculate_checksum(body));
    return n > 0 && (size_t)n < capacity ? (size_t)n : 0;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool BAP_decode_sentence(const char *message, char *body, size_t capacity) {
    if (!message || !body) return false;
    size_t len = strnlen(message, BAP_MAX_MESSAGE_LEN + 1);
    if (!len || len > BAP_MAX_MESSAGE_LEN || message[0] != '$') return false;
    if (message[len - 1] == '\n') {
        --len;
        if (len && message[len - 1] == '\r') --len;
    }
    const char *star = strchr(message, '*');
    if (!star || (size_t)(star - message) + 3 != len) return false;
    size_t body_len = star - message - 1;
    int hi = hex_digit(star[1]), lo = hex_digit(star[2]);
    if (hi < 0 || lo < 0 || body_len >= capacity) return false;
    for (size_t i = 1; i <= body_len; ++i) {
        if ((unsigned char)message[i] < 0x20 || (unsigned char)message[i] > 0x7e || message[i] == '$') return false;
    }
    memcpy(body, message + 1, body_len);
    body[body_len] = '\0';
    return BAP_calculate_checksum(body) == (uint8_t)((hi << 4) | lo);
}

bool BAP_stream_feed(bap_stream_t *stream, uint8_t byte) {
    if (byte == '$') {
        stream->message[0] = '$';
        stream->length = 1;
    } else if (stream->length) {
        if (byte == '\n') {
            stream->message[stream->length++] = '\n';
            stream->message[stream->length] = '\0';
            stream->length = 0;
            return true;
        }
        if (stream->length >= BAP_MAX_MESSAGE_LEN - 1 ||
            stream->message[stream->length - 1] == '\r' ||
            (byte != '\r' && (byte < 0x20 || byte > 0x7e))) {
            stream->length = 0;
        } else {
            stream->message[stream->length++] = (char)byte;
        }
    }
    return false;
}

bool BAP_parse_interval(const char *value, uint32_t *interval_ms) {
    if (!value || !*value || strlen(value) > 8) return false;
    uint32_t interval = 0;
    for (; *value; ++value) {
        if (*value < '0' || *value > '9') return false;
        interval = interval * 10 + (unsigned)(*value - '0');
        if (interval > 60000) return false;
    }
    if (interval < 100) return false;
    *interval_ms = interval;
    return true;
}
