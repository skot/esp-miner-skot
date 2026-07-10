#ifndef MC3_H_
#define MC3_H_

#include <stdint.h>
#include <stddef.h>

#include "asic_common.h"
#include "mining.h"

#define MC3_SERIALTX_DEBUG false
#define MC3_SERIALRX_DEBUG false
#define MC3_DEBUG_WORK false

uint8_t MC3_init(void * GLOBAL_STATE);
void MC3_send_work(void * GLOBAL_STATE, bm_job * next_bm_job);
void MC3_set_version_mask(uint32_t version_mask);
int MC3_set_max_baud(void);
int MC3_set_default_baud(void);
float MC3_send_hash_frequency(float frequency);
task_result * MC3_process_work(void * GLOBAL_STATE);
void MC3_read_registers(void * GLOBAL_STATE);
void MC3_set_nonce_space(double nonce_percent, float frequency, uint16_t asic_count, uint16_t cores);
uint8_t MC3_read_temperatures(float *temps, size_t max_temps);
uint8_t MC3_read_vdd_voltages(float *voltages_mv, size_t max_voltages);

#endif /* MC3_H_ */
