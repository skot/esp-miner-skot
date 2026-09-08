# Stock 1103: 40 C / 1180 mV, restored 2-second PLL steps — 2026-09-07

Requested isolating comparison against `../2026-09-07-1103-1180mv-40c-faster`.
Only the 300–575 MHz PLL settling cadence changes: **2000 ms per 25 MHz step**
instead of 1500 ms. The 600 MHz final settle remains 1500 ms, and steps below
300 remain 500 ms. Both temperature gates remain **every ASIC >=40 C**, with
the existing ten-second 300 MHz warmup dwell. Voltage remains **1180 mV per
board half-stack** during startup and subsequent mining.

Supply cycle, first work, thermal checks/retries, fault handling and voltage
telemetry are unchanged. Startup host tests pass and verify the restored exact
PLL cadence. The previous faster-ramp source and test are archived with
`.before` suffixes; prior experiment firmware remains available separately.

## Preparation

The board was already powered off by the prior failed startup. Confirmed
regulator output 32 mV and enabled manual fan 100% through the API. This run
needs no temporary unsupported voltage setting or pause/resume: voltage stays
1180 mV. Wait at least two minutes at confirmed 100% fan before restoring the
original automatic fan settings and application-only OTA. Initial cached ASIC
temperatures are stale; fresh post-boot temperatures establish actual cooling.
No serial connection, scope or automatic recovery loop is used.

The host monitor records passive API snapshots and logs. A successful startup
is followed by five minutes of pool observation. Separate domain activity from
individual core activity; the known A1 D2 logical core 105 was zero PASS in
previous successful runs. Efficiency uses reported TPS546 output power; the
Proto power allowance is 0 W, so this is not wall-power efficiency.

## Result: restoring the slower steps did not recover full hashrate

ESP-IDF build and startup host tests passed. The flashed application SHA-256
is `bc9c109a05086870d650282c142d5633736af56c94df804fa3f6b9d82f3074b0`.
Fresh initial temperatures were **27.62 / 26.96 / 26.80 / 27.29 C**, about
1–2 C below the preceding faster test. Initial 300 MHz qualification passed
at **1482.88 GH/s**. After the ten-second dwell, temperatures were **42.30 /
41.32 / 41.64 / 42.14 C**, close to that preceding run, and warm qualification
passed at **1484.98 GH/s**.

The 300-to-600 MHz PLL updates took **24.444 seconds**, confirming restoration
of the slower cadence (18.451 seconds in the faster test). Before first work,
valid fresh temperatures were **47.58 / 45.77 / 47.09 / 47.09 C**. First work
began at uptime **184.463 seconds**; this was later than the faster test's
164.557 seconds, with the difference including longer warmup from the cooler
starting state as well as the six-second ramp change.

600 MHz qualification failed at **2595.75 GH/s**. **A1 D0 and A2 D2** each
had all 39 cores at zero PASS. There were **14 active domains and 545/624
active cores**, including the known A1 D2 logical core 105 at zero PASS.
ASIC rates were **553.43 / 556.04 / 742.48 / 743.80 GH/s**. The inactive domains
changed from the faster test's A2 D0 / A3 D2; the overall lost throughput was
similar. Temperature conversions stayed valid through qualification, ending
at **49.40 / 47.91 / 49.23 / 49.07 C**.

During first-work observation, sampled board halves were **1159 / 1205 mV**
at uptime 188 s and **1157 / 1207 mV** at 194 s; reported regulator total was
2364 mV in both samples. The sampled peak was therefore 1207 mV, with two
readings above 1200 mV. Voltage telemetry remained report-only; the shutdown
was caused by failed throughput qualification. These measurements cannot
establish whether the voltage imbalance preceded or followed core inactivity,
and there was no scope to resolve fast transients.

The firmware shut ASIC power off. Post-monitor HTTP inspection at uptime
229 seconds confirmed **34 mV**, startup result `failed`, retained diagnostic
epoch 11, and no pool mining or shares. No retry was performed. The installed
firmware retains **40 C / 2-second steps / 1180 mV**, with ASIC power off.
There is no five-minute hold or sustained J/TH result. The pool-only summaries
contain zero complete mining epochs; use `result.json` / `failure.json` for
startup domain activity.

## Comparison at 1180 mV

| Warmup minimum | 300–575 MHz step | Pre-work temperature range | Qualification | Inactive domains |
| --- | --- | --- | --- | --- |
| 45 C | 2 s | 48.57–50.06 C | 2962.43 GH/s PASS | None |
| 40 C | 1.5 s | 45.27–46.76 C | 2593.80 GH/s FAIL | A2 D0, A3 D2 |
| 40 C | 2 s | 45.77–47.58 C | 2595.75 GH/s FAIL | A1 D0, A2 D2 |

Restoring two-second steps was insufficient to rescue this 40 C startup.
Together these boots favor retaining more warmup over attributing the prior
failure solely to faster PLL updates. Each condition has only one observed
boot here, initial temperatures differ, and earlier hotter failures exist;
this does not establish a deterministic temperature threshold or exclude any
cadence effect. Higher startup voltage followed by lower mining voltage is
still untested.
