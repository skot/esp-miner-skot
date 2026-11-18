#ifndef BITAIR_AIR_QUALITY_H_
#define BITAIR_AIR_QUALITY_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool has_concentrations;
    uint16_t raw_level;
    uint16_t tvoc_ug_m3;
    uint16_t formaldehyde_ug_m3;
    uint16_t estimated_co2_ppm;
} BitairAirQuality;

esp_err_t bitair_air_quality_init(void);
bool bitair_air_quality_get(BitairAirQuality *reading);

#endif
