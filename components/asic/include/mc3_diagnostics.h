#ifndef MC3_DIAGNOSTICS_H
#define MC3_DIAGNOSTICS_H

#include "asic_tuning.h"

typedef struct {
    bool valid;
    float frequency_mhz;
    double window_seconds;
    int64_t measured_us;
    uint32_t core_pass[ASIC_TUNING_MAX_CORES];
    uint32_t core_fail[ASIC_TUNING_MAX_CORES];
} mc3_diagnostic_chip_t;

typedef struct {
    uint32_t epoch;
    bool mining;
    // 0: not yet qualified, 1: passed, -1: failed. Retained until next initialization.
    int startup_result;
    uint8_t chip_count;
    uint8_t leading_zeros;
    mc3_diagnostic_chip_t chips[ASIC_TUNING_MAX_CHIPS];
} mc3_diagnostics_t;

// Memory-only operations. None of these functions access ASIC registers.
void mc3_diagnostics_reset(void);
void mc3_diagnostics_finish_startup(bool passed);
void mc3_diagnostics_record(uint32_t epoch, bool mining, bool running, uint8_t chip_count,
    uint8_t chip_id, uint8_t leading_zeros, float frequency, double seconds,
    int64_t measured_us, const uint32_t *pass, const uint32_t *fail);
void mc3_diagnostics_copy(mc3_diagnostics_t *out);
#endif
