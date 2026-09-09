#ifndef POWER_MANAGEMENT_TASK_H_
#define POWER_MANAGEMENT_TASK_H_

#include <stdint.h>
#include <stdbool.h>

#define MAX_ASIC_TEMPS 8
#define MAX_VOLTAGE_DOMAINS 4

typedef struct
{
    float fan_perc;
    uint16_t fan_rpm;
    uint16_t fan2_rpm;
    float chip_temp_avg;
    float chip_temp2_avg;
    float asic_temps[MAX_ASIC_TEMPS];
    uint8_t asic_temp_count;
    float asic_voltages[MAX_ASIC_TEMPS];
    uint8_t asic_voltage_count;
    float domain_voltages[MAX_VOLTAGE_DOMAINS];
    uint8_t domain_voltage_count;
    float vr_temp;
    float voltage;
    float frequency_value;
    float actual_frequency;    
    float expected_hashrate;
    float power;
    float current;
    float core_voltage;
    float regulator_voltage;
    uint16_t applied_core_voltage_mv; // Last successful command, not a measurement.
    int64_t initialized_us;
} PowerManagementModule;

void POWER_MANAGEMENT_init_frequency(void * pvParameters);

void POWER_MANAGEMENT_task(void * pvParameters);

#endif
