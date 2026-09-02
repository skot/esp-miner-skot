#ifndef ADC_H_
#define ADC_H_

#include <stdint.h>
#include "esp_err.h"

void ADC_init(void);
uint16_t ADC_get_vcore(void);
esp_err_t ADC_get_domain_midpoint_mv(uint16_t *voltage_mv);

#endif /* ADC_H_ */
