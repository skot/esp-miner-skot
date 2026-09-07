#include "tuning.h"

#include "asic.h"
#include "mc3_diagnostics.h"
#include "esp_timer.h"
#include <stdlib.h>
#include "cJSON.h"
#include "http_server.h"

static GlobalState *GLOBAL_STATE;
static int tuning_prebuffer_len = 4096;

extern esp_err_t is_network_allowed(httpd_req_t *req);
extern esp_err_t set_cors_headers(httpd_req_t *req);

void tuning_api_init(GlobalState *global_state)
{
    GLOBAL_STATE = global_state;
}

static cJSON *tuning_status_json(void)
{
    asic_tuning_status_t status;
    ASIC_get_tuning_status(GLOBAL_STATE, &status);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "supported", status.supported);
    cJSON_AddBoolToObject(root, "validated", status.validated);
    cJSON_AddStringToObject(root, "state", ASIC_tuning_state_name(status.state));
    cJSON_AddNumberToObject(root, "progress", status.progress_percent);
    cJSON_AddNumberToObject(root, "scanId", status.scan_id);
    cJSON_AddNumberToObject(root, "chipCount", status.chip_count);
    cJSON_AddNumberToObject(root, "coreCount", status.core_count);
    cJSON_AddNumberToObject(root, "leadingZeros", status.leading_zeros);
    cJSON_AddNumberToObject(root, "runtimeSeconds", status.runtime_seconds);
    cJSON_AddStringToObject(root, "message", status.message);

    cJSON *chips = cJSON_AddArrayToObject(root, "chips");
    if (status.state != ASIC_TUNING_COMPLETE) {
        return root;
    }

    for (uint8_t chip_id = 0; chip_id < status.chip_count; chip_id++) {
        asic_tuning_chip_result_t result;
        if (!ASIC_get_tuning_chip_result(GLOBAL_STATE, chip_id, &result)) {
            continue;
        }

        cJSON *chip = cJSON_CreateObject();
        cJSON_AddNumberToObject(chip, "chipId", result.chip_id);
        cJSON_AddBoolToObject(chip, "validated", result.validated);
        cJSON_AddNumberToObject(chip, "globalPass", result.global_pass);
        cJSON_AddNumberToObject(chip, "globalFail", result.global_fail);
        cJSON_AddNumberToObject(chip, "corePassSum", (double)result.core_pass_sum);
        cJSON_AddNumberToObject(chip, "globalHashrate", result.global_hashrate_ghs);
        cJSON_AddNumberToObject(chip, "matchPercent", result.global_pass > 0
            ? ((double)result.core_pass_sum * 100.0) / result.global_pass
            : 0.0);

        cJSON *pass_counts = cJSON_AddArrayToObject(chip, "corePassCounts");
        for (uint16_t core_id = 0; core_id < status.core_count; core_id++) {
            cJSON_AddItemToArray(pass_counts, cJSON_CreateNumber(result.core_pass_counts[core_id]));
        }
        cJSON_AddItemToArray(chips, chip);
    }

    return root;
}

static esp_err_t send_tuning_status(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    if (set_cors_headers(req) != ESP_OK) {
        return httpd_resp_send_500(req);
    }

    cJSON *root = tuning_status_json();
    if (root == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not allocate tuning response");
    }

    esp_err_t result = HTTP_send_json(req, root, &tuning_prebuffer_len);
    cJSON_Delete(root);
    return result;
}

esp_err_t GET_system_tuning(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    return send_tuning_status(req);
}

esp_err_t POST_system_tuning(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    if (!GLOBAL_STATE->ASIC_initalized) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "ASIC is not ready");
    }

    ASIC_start_tuning(GLOBAL_STATE);
    return send_tuning_status(req);
}

// Unlike the tuning endpoint, this only copies measurements already collected
// by startup or the normal logger task. HTTP never touches the ASIC bus.
esp_err_t GET_system_diagnostics(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }
    if (set_cors_headers(req) != ESP_OK) return httpd_resp_send_500(req);
    mc3_diagnostics_t *snapshot = malloc(sizeof(*snapshot));
    if (!snapshot) return httpd_resp_send_500(req);
    mc3_diagnostics_copy(snapshot);
    cJSON *root = cJSON_CreateObject();
    if (!root) {free(snapshot); return httpd_resp_send_500(req);}
    bool supported = GLOBAL_STATE->DEVICE_CONFIG.family.asic.id == MC3;
    cJSON_AddNumberToObject(root, "supported", supported ? 1 : 0);
    cJSON_AddNumberToObject(root, "mining", GLOBAL_STATE->ASIC_initalized &&
        !GLOBAL_STATE->SYSTEM_MODULE.mining_paused ? 1 : 0);
    cJSON_AddStringToObject(root, "source", snapshot->mining ? "mining" : "startup");
    cJSON_AddStringToObject(root, "startupResult", snapshot->startup_result > 0 ? "passed" :
        snapshot->startup_result < 0 ? "failed" : "pending");
    cJSON_AddStringToObject(root, "status", GLOBAL_STATE->SYSTEM_MODULE.asic_status ?
        GLOBAL_STATE->SYSTEM_MODULE.asic_status : "Waiting for ASICs");
    cJSON_AddNumberToObject(root, "epoch", snapshot->epoch);
    cJSON_AddNumberToObject(root, "leadingZeros", snapshot->leading_zeros);
    cJSON *chips = cJSON_AddArrayToObject(root, "chips");
    int64_t now = esp_timer_get_time();
    for (uint8_t c = 0; supported && c < snapshot->chip_count; c++) {
        const mc3_diagnostic_chip_t *data = &snapshot->chips[c];
        cJSON *chip = cJSON_CreateObject();
        cJSON_AddItemToArray(chips, chip);
        cJSON_AddNumberToObject(chip, "chipId", c);
        cJSON_AddNumberToObject(chip, "valid", data->valid ? 1 : 0);
        cJSON_AddNumberToObject(chip, "frequency", data->frequency_mhz);
        cJSON_AddNumberToObject(chip, "windowSeconds", data->window_seconds);
        cJSON_AddNumberToObject(chip, "ageSeconds", data->measured_us > 0 ?
            (double)(now - data->measured_us) / 1000000.0 : 0);
        cJSON *passes = cJSON_AddArrayToObject(chip, "corePassCounts");
        cJSON *fails = cJSON_AddArrayToObject(chip, "coreFailCounts");
        // Missing/failed reads are empty arrays, never 156 false zeroes.
        if (data->valid) {
            for (unsigned core = 0; core < ASIC_TUNING_MAX_CORES; core++) {
                cJSON_AddItemToArray(passes, cJSON_CreateNumber(data->core_pass[core]));
                cJSON_AddItemToArray(fails, cJSON_CreateNumber(data->core_fail[core]));
            }
        }
    }
    free(snapshot);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    static int prebuffer_len = 8192;
    esp_err_t result = HTTP_send_json(req, root, &prebuffer_len);
    cJSON_Delete(root);
    return result;
}
