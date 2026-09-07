#include "mc3_diagnostics.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    mc3_diagnostics_t s;
    uint32_t pass[156], fail[156];
    for (unsigned i = 0; i < 156; i++) {pass[i] = i; fail[i] = 2;}
    mc3_diagnostics_reset();
    mc3_diagnostics_copy(&s);
    assert(s.chip_count == 0 && s.startup_result == 0);
    mc3_diagnostics_record(1, false, false, 4, 0, 24, 600, 10, 12000000, pass, fail);
    mc3_diagnostics_finish_startup(false);
    mc3_diagnostics_copy(&s);
    assert(s.startup_result == -1 && s.chips[0].valid && !s.chips[1].valid);
    assert(s.chips[0].core_pass[155] == 155 && s.chips[0].core_fail[78] == 2);
    assert(s.chips[0].window_seconds == 10 && s.chips[0].measured_us == 12000000);
    // Failed startup remains readable without the ASICs being powered.
    mc3_diagnostics_copy(&s);
    assert(s.startup_result == -1 && s.chips[0].core_pass[155] == 155);
    // Zero is a valid observation; a failed read is explicitly unavailable.
    pass[0] = 0;
    mc3_diagnostics_record(1, false, false, 4, 1, 24, 600, 10, 12000000, pass, fail);
    mc3_diagnostics_record(1, false, false, 4, 2, 24, 600, 10, 12000000, NULL, NULL);
    mc3_diagnostics_copy(&s);
    assert(s.chips[1].valid && s.chips[1].core_pass[0] == 0 && !s.chips[2].valid);
    // A new window cannot mix a new A1 with old A2/A3/A4 values.
    mc3_diagnostics_record(2, true, false, 4, 0, 24, 600, 1, 15000000, pass, fail);
    mc3_diagnostics_copy(&s);
    assert(s.epoch == 2 && s.mining && s.chips[0].valid && !s.chips[1].valid);
    mc3_diagnostics_record(2, true, false, 4, 0, 24, 600, NAN, 15000000, pass, fail);
    mc3_diagnostics_copy(&s);
    assert(!s.chips[0].valid && s.chips[0].window_seconds == 0);
    mc3_diagnostics_record(3, true, false, 9, 8, 24, 600, 1, 15000000, pass, fail);
    mc3_diagnostics_copy(&s);
    assert(s.epoch == 2);
    // First-work baseline has deliberately biased initial counters. Only
    // growth over the subsequent ten seconds contributes to the displayed rate.
    mc3_diagnostics_record(4, false, true, 4, 0, 24, 600, .1, 100000, pass, fail);
    mc3_diagnostics_copy(&s);
    assert(!s.chips[0].valid);
    for (unsigned i = 0; i < 156; i++) {pass[i] += i >= 78 && i < 117 ? 0 : 2800; fail[i] += 5;}
    mc3_diagnostics_record(4, false, true, 4, 0, 24, 600, 10.1, 10100000, pass, fail);
    mc3_diagnostics_copy(&s);
    assert(s.chips[0].valid && s.chips[0].window_seconds == 10);
    assert(s.chips[0].core_pass[0] == 2800 && s.chips[0].core_fail[0] == 5);
    for (unsigned i = 78; i < 117; i++) assert(s.chips[0].core_pass[i] == 0);
    pass[0] = 0; pass[1] = 0; // A decrease/reset must not underflow into a huge rate.
    mc3_diagnostics_record(4, false, true, 4, 0, 24, 600, 11.1, 11100000, pass, fail);
    mc3_diagnostics_copy(&s); assert(!s.chips[0].valid);
    mc3_diagnostics_reset();
    mc3_diagnostics_copy(&s);
    assert(s.chip_count == 0 && s.startup_result == 0);
    puts("PASS: counter cache, failed-startup retention, unavailable vs zero, epoch isolation, reset");
}
