#ifndef ASIC_H
#define ASIC_H

#include <esp_err.h>
#include "global_state.h"
#include "asic_common.h"
#include "asic_tuning.h"

uint8_t ASIC_init(GlobalState * GLOBAL_STATE);
task_result * ASIC_process_work(GlobalState * GLOBAL_STATE);
int ASIC_set_max_baud(GlobalState * GLOBAL_STATE);
void ASIC_send_work(GlobalState * GLOBAL_STATE, void * next_job);
void ASIC_set_version_mask(GlobalState * GLOBAL_STATE, uint32_t mask);
void ASIC_set_frequency(GlobalState * GLOBAL_STATE);
void ASIC_set_nonce_space(GlobalState * GLOBAL_STATE);
double ASIC_get_asic_job_frequency_ms(GlobalState * GLOBAL_STATE);
void ASIC_read_registers(GlobalState * GLOBAL_STATE);
bool ASIC_start_tuning(GlobalState * GLOBAL_STATE);
void ASIC_get_tuning_status(GlobalState * GLOBAL_STATE, asic_tuning_status_t *status);
bool ASIC_get_tuning_chip_result(GlobalState * GLOBAL_STATE, uint8_t chip_id, asic_tuning_chip_result_t *result);
const char *ASIC_tuning_state_name(asic_tuning_state_t state);

#endif // ASIC_H
