#ifndef BITAIR_H_
#define BITAIR_H_

#include <stdbool.h>
#include "esp_err.h"

// Call after installing the shared GPIO ISR service.
esp_err_t bitair_init(void);
// Physical fan request in percent; invalid input requests full airflow.
float bitair_get_fan_percent(void);
bool bitair_mining_disabled(void);
const char *bitair_get_mode_name(void);

#endif
