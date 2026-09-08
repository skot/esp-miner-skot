# Stock 1103: 40 C / 2-second steps at 1200 mV — 2026-09-07

Requested voltage comparison against `../2026-09-07-1103-1180mv-40c-2s`.
Both temperature gates remain every ASIC >=40 C, with a ten-second dwell at
300 MHz. High-frequency 25 MHz steps retain 2000 ms settling from 300 through
575 MHz, and final 600 MHz settling stays 1500 ms. The voltage setting changes
from **1180 to 1200 mV per board half-stack**, throughout startup and mining.
No subsequent undervolt is implemented in this test.

Raised the supported startup ceiling to 1200 mV and updated status strings.
Host tests cover the full 40-step ramp from 800 to 1200 mV, exact PLL cadence,
pre-work cold rejection at 1200 mV, and rejection of 1201 mV before startup.
TPS546 configuration is unchanged: requested total 2.40 V equals its existing
VOUT_MAX. Sampled domain voltages remain report-only; no voltage trip was
removed. Other thermal, hardware-fault and throughput checks remain unchanged.

## Preparation

The previous failed startup left ASIC power off. Confirmed 32 mV regulator
output and manual fan 100% through the API. Cool at confirmed fan 100% for at
least two minutes, then restore original automatic fan control and set saved
voltage to 1200 mV immediately before application-only OTA. The installed old
firmware rejects this new setting before applying ASIC power; the new firmware
allows it. No serial connection, scope, automatic restart or recovery loop.
Fresh post-boot temperatures establish cooling; powered-off cached temperatures
are stale. `preparation.json` includes unchanged MC3 and vcore source hashes.

Observe qualification at 600 MHz, then five minutes of pool mining if it passes.
Count domains separately from cores: previous successes have had a persistent
zero-PASS A1 D2 logical core 105. Estimated efficiency uses TPS546 output power
(the Proto power offset is 0 W), not wall-power measurements.

## Result: temperature conversion failed before first work

ESP-IDF build and startup host tests passed. The flashed application SHA-256
is `f77ac103893b7213393299e0b77398c3fd55cd970f5ce91888296c0bc2f96f9a`.
Fresh starting temperatures were **26.64 / 25.98 / 25.98 / 26.47 C**. The
initial 300 MHz qualification passed at **1484.50 GH/s**; after the 40 C dwell,
warm qualification passed at **1489.57 GH/s**, with temperatures **42.47 /
41.48 / 41.81 / 41.97 C**.

The reinitialized ASICs completed the unloaded ramp to 600 MHz at uptime
**187.441 seconds**. The fresh pre-work conversion then failed on **A3 and A4
in all three attempts**: `done=0`, `status=3`, `raw=0`, data/status reads
successful, and decoded invalid temperature -288.90 C. A1/A2 conversions were
valid (47.75 / 46.26 C on attempt 1, 48.08 / 46.59 C on attempt 3).
The stored A3/A4 dashboard temperatures remained the older 300 MHz readings;
they do not establish actual post-ramp temperature.

No 600 MHz first work was sent and no 600 MHz throughput qualification was
performed. The diagnostic cache had epoch 0 with no current first-work counts,
so this cannot be classified as a domain/core throughput failure or success.
The conversion failure matches the symptom from the first, hotter 1180 mV
attempt documented in `../2026-09-07-1103-1180mv/README.md`; that historical
attempt later passed after a separate identical restart. This run has not been
retried, and the recurrence does not isolate its cause.

Firmware shut ASIC power off at uptime **190.864 seconds**. A later HTTP check
at uptime 258 seconds confirmed regulator output **32 mV**, startup `failed`,
zero shares and no pool mining. Settings and installed firmware remain
**40 C / 2-second steps / 1200 mV**, with ASIC power off. There is no sustained
mining or J/TH result, and no conclusion yet about whether 1200 mV prevents
40 C domain dropouts under work.

The sampled peak half-stack voltage was **1213 mV** during the unloaded ramp
at reported 400 MHz (uptime 171 s), paired with 1190 mV on the other half and
2403 mV total. Twenty-five individual sampled half-stack readings exceeded
1200 mV across the capture. Voltage telemetry remained report-only; the
shutdown was caused by invalid thermal conversions. No scope was used, so
these samples do not resolve fast transients. MC3 and vcore source hashes
remain unchanged from preparation.

`startup-events.json` records the bounded thermal retries. `result.json`
distinguishes missing 600 MHz measurements from zero throughput. The pool-only
summaries contain no complete mining epochs and should not be interpreted as
all domains healthy.
