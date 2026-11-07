#ifndef BITAIR_INDICATOR_H_
#define BITAIR_INDICATOR_H_

#include "esp_err.h"

typedef struct GlobalState GlobalState;

esp_err_t bitair_indicator_init(GlobalState *state);

#endif
