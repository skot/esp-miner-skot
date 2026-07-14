#ifndef ASIC_TUNING_H
#define ASIC_TUNING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ASIC_TUNING_MAX_CHIPS 8
#define ASIC_TUNING_MAX_CORES 156
#define ASIC_TUNING_MESSAGE_LENGTH 96

typedef enum {
    ASIC_TUNING_UNSUPPORTED,
    ASIC_TUNING_IDLE,
    ASIC_TUNING_QUEUED,
    ASIC_TUNING_MEASURING,
    ASIC_TUNING_READING,
    ASIC_TUNING_COMPLETE,
    ASIC_TUNING_ERROR,
} asic_tuning_state_t;

typedef struct {
    bool supported;
    bool validated;
    asic_tuning_state_t state;
    uint8_t progress_percent;
    uint8_t chip_count;
    uint16_t core_count;
    uint8_t leading_zeros;
    uint32_t scan_id;
    double runtime_seconds;
    char message[ASIC_TUNING_MESSAGE_LENGTH];
} asic_tuning_status_t;

typedef struct {
    uint8_t chip_id;
    bool validated;
    uint32_t global_pass;
    uint32_t global_fail;
    uint64_t core_pass_sum;
    double global_hashrate_ghs;
    uint32_t core_pass_counts[ASIC_TUNING_MAX_CORES];
} asic_tuning_chip_result_t;

#endif // ASIC_TUNING_H
