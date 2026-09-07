# Slower unloaded PLL ramp on stock 1103x

User requested slowing the 300–600 MHz ramp to check whether startup improves.
Comparison changes the dwell after each 25 MHz PLL update from 300 through
575 MHz to **2000 ms**. Below 300 MHz the dwell remains 500 ms. Final target
settling remains **1500 ms** before the temperature check and first job.
The ramp deadline increases from 20 to 45 seconds to accommodate those steps.

Unchanged: all four ASICs must reach 40 C for ten seconds while hashing at
300 MHz; 1160 mV/half; full warm supply cycle; broadcast PLL updates; no work
or temperature conversions during the unloaded ramp; passive first-work
qualification and domain failure guard; cached AxeOS diagnostics. Fan override
is refreshed on each step. This longer unloaded interval can change the ASIC
temperatures at first work, so temperatures and actual step timings are recorded.

Baseline: `../2026-09-06-1103-dashboard/ramp-summary.json` (fast ramp, final
qualification 2044.51 GH/s, zero-PASS domains A1 D0/D2, A2 D0/D2, A3 D2).

Validation: ESP-IDF build, startup state-machine tests with the longer step
assertions, and first-work register-write regression tests passed. One app-only
flash preserves NVS and the installed AxeOS UI. No scope is connected.

## Result

The actual 300–600 MHz interval increased from **7907 to 24380 ms**. Final
600 MHz PLL-to-first-work interval stayed essentially identical (**1591 vs
1592 ms**). Qualification remained **FAIL: 2044.74 GH/s**, compared with the
baseline **2044.51 GH/s**. Per-ASIC rates were 373.42, 371.42, 557.52, and
742.38 GH/s. A1 D0/D2, A2 D0/D2, and A3 D2 still had 39/39 zero-PASS cores.

Before first work, temperatures were **47.25 / 45.11 / 46.59 / 46.76 C**,
versus baseline 46.26 / 44.45 / 45.60 / 45.44 C. Thus this longer unloaded
ramp left the ASICs 0.66–1.32 C warmer rather than colder. The slightly different
warmup exit temperatures are also recorded in `ramp-summary.json`.

This single slower-ramp trial did not improve startup. It does not establish
that ramp timing never matters, nor does it isolate the remaining clock/work
or electrical cause. Firmware powered the ASICs off on failed qualification.
The slower 2-second cadence remains installed for manual testing; warmup stays
at 40 C and final settling at 1.5 seconds. No extra work reset/recovery was tried.
