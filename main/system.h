#ifndef SYSTEM_H_
#define SYSTEM_H_

#include <stdbool.h>
#include "esp_err.h"
#include "miner_job.h"
#include "stratum_api.h"

typedef struct GlobalState GlobalState;
typedef struct SystemModule SystemModule;

void SYSTEM_check_firmware_migration(void);
void SYSTEM_reset_custom_www(void);
void SYSTEM_init_system(GlobalState * GLOBAL_STATE);
void SYSTEM_init_versions(GlobalState * GLOBAL_STATE);
void SYSTEM_init_partitions(GlobalState * GLOBAL_STATE);
esp_err_t SYSTEM_init_peripherals(GlobalState * GLOBAL_STATE);

void SYSTEM_notify_submitted_share(GlobalState * GLOBAL_STATE);
void SYSTEM_notify_accepted_share(GlobalState * GLOBAL_STATE);
void SYSTEM_notify_rejected_share(GlobalState * GLOBAL_STATE, char * error_msg);
// hash is the nonce's double-SHA256 (little-endian); is_share means it met the pool target.
void SYSTEM_notify_found_nonce(GlobalState * GLOBAL_STATE, const uint8_t hash[32], double diff, bool is_share, bool is_block);

// Copy the network target, best share hash and last share hash as 64 hex digits,
// most significant first. A value that is not known yet is returned as "".
void SYSTEM_get_display_hashes(GlobalState * GLOBAL_STATE, char network_target[65], char best_share[65], char last_share[65]);
void SYSTEM_notify_new_ntime(GlobalState * GLOBAL_STATE, uint32_t ntime);
void SYSTEM_record_response_time(GlobalState * GLOBAL_STATE, float response_time_ms, uint16_t batch_count);

// Reset decoded coinbase UI fields (scriptsig, coinbase values, outputs, block signals).
// Note: block_height is intentionally NOT reset here; it is preserved as the "last known good"
// network height so the UI, screen, and BAP do not flicker or lose context on transient disconnects.
void SYSTEM_reset_coinbase_ui_state(GlobalState * GLOBAL_STATE);
void SYSTEM_decode_and_apply_coinbase(GlobalState * GLOBAL_STATE, const miner_job_t * job);

// Reset pool session stats, share counts, pending shares, latency, difficulty,
// job queue, and coinbase UI state on disconnect, reconnect, or failover.
void SYSTEM_reset_pool_session(GlobalState * GLOBAL_STATE);

void SYSTEM_noinit_update(SystemModule * SYSTEM_MODULE, float interval_hashrate_ghs);
uint64_t SYSTEM_noinit_get_total_uptime_seconds();
double SYSTEM_noinit_get_total_hashes();
double SYSTEM_noinit_get_total_log2_work();
void SYSTEM_load_pool_from_nvs(GlobalState * GLOBAL_STATE, int i);
void SYSTEM_reload_pool_config(GlobalState * GLOBAL_STATE);

#endif /* SYSTEM_H_ */
