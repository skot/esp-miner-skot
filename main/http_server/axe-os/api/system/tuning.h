#ifndef TUNING_API_H
#define TUNING_API_H

#include <esp_http_server.h>

#include "global_state.h"

void tuning_api_init(GlobalState *global_state);
esp_err_t GET_system_tuning(httpd_req_t *req);
esp_err_t POST_system_tuning(httpd_req_t *req);

#endif // TUNING_API_H
