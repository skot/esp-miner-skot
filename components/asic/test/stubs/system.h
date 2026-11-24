#ifndef TEST_STUB_SYSTEM_H
#define TEST_STUB_SYSTEM_H

#include "global_state.h"
#include "miner_job.h"

void SYSTEM_decode_and_apply_coinbase(GlobalState *state,
                                      const miner_job_t *job);
void SYSTEM_notify_found_nonce(GlobalState *state, const uint8_t hash[32],
                               double difficulty, bool is_share,
                               bool is_block);

#endif /* TEST_STUB_SYSTEM_H */
