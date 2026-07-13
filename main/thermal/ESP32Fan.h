#ifndef ESP32_FAN_H
#define ESP32_FAN_H

#include <stdint.h>

#include "esp_err.h"

esp_err_t ESP32Fan_init(void);
esp_err_t ESP32Fan_set_speed(float percent);
uint16_t ESP32Fan_get_speed(void);

#endif // ESP32_FAN_H
