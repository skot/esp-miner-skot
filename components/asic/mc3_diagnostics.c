#include "mc3_diagnostics.h"
#include <math.h>
#include <pthread.h>
#include <string.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static mc3_diagnostics_t snapshot;
// Running first-work counters are sampled sequentially. Subtract each core's
// initial reading so initial work delivery and bank-read skew cancel out.
static struct {
    bool valid;
    int64_t measured_us;
    uint16_t pass[ASIC_TUNING_MAX_CORES];
    uint16_t fail[ASIC_TUNING_MAX_CORES];
} baseline[ASIC_TUNING_MAX_CHIPS];

void mc3_diagnostics_reset(void)
{
    pthread_mutex_lock(&lock);
    memset(&snapshot, 0, sizeof(snapshot));
    memset(baseline, 0, sizeof(baseline));
    pthread_mutex_unlock(&lock);
}

void mc3_diagnostics_finish_startup(bool passed)
{
    pthread_mutex_lock(&lock);
    snapshot.startup_result = passed ? 1 : -1;
    pthread_mutex_unlock(&lock);
}

void mc3_diagnostics_record(uint32_t epoch, bool mining, bool running, uint8_t chip_count,
    uint8_t chip_id, uint8_t leading_zeros, float frequency, double seconds,
    int64_t measured_us, const uint32_t *pass, const uint32_t *fail)
{
    if (chip_count > ASIC_TUNING_MAX_CHIPS || chip_id >= chip_count) return;
    pthread_mutex_lock(&lock);
    if (snapshot.epoch != epoch) {
        // Never combine chips from different counter windows.
        memset(snapshot.chips, 0, sizeof(snapshot.chips));
        memset(baseline, 0, sizeof(baseline));
    }
    snapshot.epoch = epoch;
    snapshot.mining = mining;
    snapshot.chip_count = chip_count;
    snapshot.leading_zeros = leading_zeros;
    mc3_diagnostic_chip_t *chip = &snapshot.chips[chip_id];
    memset(chip, 0, sizeof(*chip));
    chip->frequency_mhz = isfinite(frequency) ? frequency : 0;
    chip->measured_us = measured_us;
    chip->valid = pass && fail && isfinite(seconds) && seconds > 0 &&
        isfinite(frequency) && frequency > 0 && leading_zeros < 63;
    if (chip->valid) {
        chip->window_seconds = seconds;
        if (running && !baseline[chip_id].valid) {
            // This first sample is only a baseline, not a rate measurement.
            baseline[chip_id].valid = true;
            baseline[chip_id].measured_us = measured_us;
            for (unsigned core = 0; core < ASIC_TUNING_MAX_CORES; core++) {
                baseline[chip_id].pass[core] = pass[core];
                baseline[chip_id].fail[core] = fail[core];
            }
            chip->valid = false;
            chip->window_seconds = 0;
        } else if (running) {
            chip->window_seconds = (measured_us - baseline[chip_id].measured_us) / 1000000.0;
            chip->valid = chip->window_seconds >= 1.0;
            for (unsigned core = 0; core < ASIC_TUNING_MAX_CORES; core++) {
                // A reset or wrap cannot be interpreted as a valid delta.
                if (pass[core] < baseline[chip_id].pass[core] ||
                    fail[core] < baseline[chip_id].fail[core]) {
                    chip->valid = false;
                    break;
                }
                chip->core_pass[core] = pass[core] - baseline[chip_id].pass[core];
                chip->core_fail[core] = fail[core] - baseline[chip_id].fail[core];
            }
        } else {
            memcpy(chip->core_pass, pass, sizeof(chip->core_pass));
            memcpy(chip->core_fail, fail, sizeof(chip->core_fail));
        }
    }
    pthread_mutex_unlock(&lock);
}

void mc3_diagnostics_copy(mc3_diagnostics_t *out)
{
    pthread_mutex_lock(&lock);
    *out = snapshot;
    pthread_mutex_unlock(&lock);
}
