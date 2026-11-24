#ifndef ASIC_H
#define ASIC_H

#include <esp_err.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "asic_job.h"

typedef struct GlobalState GlobalState;
typedef struct task_result task_result;

typedef struct {
    uint64_t time_us;
    float hashrate;
} asic_domain_measurement_t;

uint8_t ASIC_init(GlobalState * GLOBAL_STATE);
/* Returns driver-owned storage valid until the next call, or NULL when no
 * usable response is available. Non-register results include the matched job
 * snapshot, independent of subsequent active-slot reuse. */
task_result * ASIC_process_work(GlobalState * GLOBAL_STATE);
int ASIC_set_max_baud(GlobalState * GLOBAL_STATE);
/* The caller retains ownership and may release the job after this call. */
void ASIC_send_job(GlobalState *state, const asic_job_t *job);
// Configure hardware version rolling; software-midstate ASICs use each job's version_mask.
void ASIC_set_version_mask(GlobalState * GLOBAL_STATE, uint32_t mask);
float ASIC_get_min_frequency(GlobalState * GLOBAL_STATE);
void ASIC_set_frequency(GlobalState * GLOBAL_STATE);
void ASIC_set_nonce_space(GlobalState * GLOBAL_STATE);
double ASIC_get_asic_job_frequency_ms(GlobalState * GLOBAL_STATE);
void ASIC_read_registers(GlobalState * GLOBAL_STATE);
float ASIC_get_temperature(GlobalState *state);
esp_err_t ASIC_get_asic_measurement(GlobalState * GLOBAL_STATE, uint8_t asic_nr,
                                    asic_domain_measurement_t * measurement);
esp_err_t ASIC_get_domain_measurement(GlobalState * GLOBAL_STATE, uint8_t asic_nr,
                                      uint8_t domain_nr, asic_domain_measurement_t * measurement);

#define ASIC_DETAIL_MAX_DOMAINS 4

/* Per-chip readings for accessories. A domain here is a part of the chip with
 * its own rail and clock (on BZM, an engine stack and its PLL); it is not the
 * same as a hash-counting domain. Values a driver cannot supply are false. */
typedef struct {
    bool has_temperature;
    float temperature_c;
    uint8_t domain_count;
    bool has_voltage;
    float domain_voltage_mv[ASIC_DETAIL_MAX_DOMAINS];
    bool has_frequency;
    float domain_frequency_mhz[ASIC_DETAIL_MAX_DOMAINS];
} asic_chip_detail_t;

/* Fill one entry per chip, up to max_chips, and return the number filled:
 * the family's chip count, or 0 when the driver has no per-chip readings.
 * Never blocks on the ASIC driver, so it is safe from accessory tasks. */
size_t ASIC_get_chip_details(GlobalState * GLOBAL_STATE, asic_chip_detail_t * details, size_t max_chips);
void ASIC_set_difficulty(GlobalState * GLOBAL_STATE, uint8_t difficulty_power);
void ASIC_clear_work(GlobalState * GLOBAL_STATE);
void ASIC_init_job_slots(GlobalState * GLOBAL_STATE);

#endif // ASIC_H
