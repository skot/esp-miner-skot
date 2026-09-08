# Stock 1103 startup at 1180 mV per half-stack — 2026-09-07

Requested comparison: retain the hot 300 MHz warmup, supply cycle, unloaded
600 MHz ramp and current first-work sequence, while increasing the requested
board half-stack voltage from the preceding test's 1160 mV to 1180 mV.
Nominal full-stack regulator output is consequently 2360 mV. These are board
half-stack/chip VDD values, not individual internal D0–D3 domain voltages.

At connection the saved setting was actually 1200 mV, with ASIC power off and
startup rejected by the firmware's 1160 mV maximum setting. The last observed
mining-startup failure was at 1160 mV (A1 D0 and A2 D2, 2594.87 GH/s). The API
setting was explicitly changed to 1180 mV for this run.

The only production source change is raising the supported startup setting
ceiling from 1160 to 1180 mV and updating the two corresponding status messages.
Sampled board-domain voltages were already report-only; no 1.20 V domain
shutdown was active in this path. No thermal, regulator-fault, valid-rail,
throughput or domain-collapse check was removed. The TPS profile, warmup and
launch-temperature thresholds, PLL cadence and work sequence are unchanged.

Host startup tests pass, including the full 1180 mV / 38-step voltage ramp,
1181 and 1200 mV setting rejection, and existing fault/temperature/throughput
paths. ESP-IDF 5.5.1 build passed. Application-only network OTA triggered one
reboot; installed web assets and other NVS settings were retained. No serial
connection or scope was used. Firmware version text is unchanged from the
previous uncommitted build, so `manifest.json` identifies the exact new binary.

`before.json` / `configured.json` record whitelisted settings; `manifest.json`
records the uploaded image hash. Monitoring captures read-only API counters,
telemetry and HTTP logs. Raw logs, samples and binaries remain ignored local
artifacts. HTTP logs may include older boots; the summarizer uses the unique
1180 mV startup marker rather than treating the whole log as one session.

The ASICs started cooler than in the preceding 1160 mV failure. Both runs use
the same launch-temperature criterion, but this is not a perfectly matched
thermal-history comparison. One outcome cannot establish voltage as the sole
cause or establish startup repeatability.

## Results

**The identical retry passed at 600 MHz / 2964.04 GH/s and continued mining.**
Its 301.18-second pool hold averaged **2961.38 GH/s** across 45 complete cached
counter epochs, range **2946.46–2976.56 GH/s**. All 16 domains remained active;
the minimum individual domain window was 95.17% of ideal. The known single
zero-PASS A1 D2 core remained. The final state had **43 accepted / 3 rejected**
shares and ASIC temperatures **53.36 / 52.04 / 52.70 / 52.37 C**.

The three rejects were `Difficulty too low` at boot 489.989, 490.068 and
490.107 seconds, immediately after the pool changed difficulty from 1000 to
16384 at 489.768 seconds. No domain dropout accompanied them. The timing is
consistent with a transition issue but does not prove its cause. Accepted
shares were subsequently logged at 536.342, 536.365 and 544.836 seconds.

The first attempt did **not** test 600 MHz throughput: A3 and A4 failed all three
fresh temperature-conversion retries after the PLL ramp, before the first job.
Both reported done=0, status=3, raw=0 and an invalid -288.90 C decoded value.
A1/A2 temperatures were valid. Firmware powered the ASICs off normally; this
was not an overvoltage trip. Warm 300 MHz qualification had passed at
1487.65 GH/s. Sampled half-stack voltage peaked at 1197 mV on this attempt.

One identical software restart followed, with no settings or firmware changes.
It passed warm 300 MHz qualification at 1487.78 GH/s, then all temperature
reads and the first-work qualification at 600 MHz. Initial temperatures were
32.24 / 31.58 / 31.42 / 31.91 C. Before first 600 MHz work they were
**54.51 / 53.36 / 54.02 / 53.69 C**: within 0.33 C of the preceding 1160 mV
failure, despite the different starting temperatures and thermal history.
The previously missing A1 D0 and A2 D2 were active.

On the retry, sampled half-stack voltages peaked at **1188 mV**; the final
reading was **1180 / 1184 mV**, total **2364 mV**. Sampled mining temperature
peaked at **55.01 C**. These are polled telemetry values, not scope captures;
unobserved fast transients cannot be characterized from these samples.

After monitoring ended, HTTP confirmed continuous uptime and **2967.86 GH/s**
at 600 MHz / 1180 mV, still mining. The setting was left at 1180 mV and no
additional restart was issued. This successful counter result supports further
1180 mV repeatability testing, but the first attempt's conversion failure means
this is not a demonstrated reliable startup fix or proof of voltage causality.

## Measurement and artifact notes

`retry-01/domain-summary.json` reports hashrate from complete valid counter
windows. The raw API mean in `retry-01/result.json` includes one zero before the
first live hashrate window was published (49 samples); it is not used for the
reported counter-window mean. `retry-01/pool-epochs.json` preserves the 45
complete windows. Pool shares are a separate measure; this short hold does not
establish long-term pool-side hashrate.

The first attempt's outcome and thermal failure are saved at the experiment
root. The successful retry has its own state, logs, summaries and on-device
card screenshot under `retry-01/`. Images use actual device data. The matching
application image is archived locally with a hash in `manifest.json`; source
changes remain uncommitted, preserving fallback commit `591dfde`.
