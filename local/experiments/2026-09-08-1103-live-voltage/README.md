# Stock 1103: live voltage reduction after 1200 mV startup

Date: 2026-09-08. Board 1103, MAC 28:84:85:BA:96:08, 192.168.30.9. No scope attached.

## Result

Starting and qualifying at 1200 mV, then lowering voltage while continuing pool mining, worked down to 1160 mV for five minutes. All 16 domains and the same 623 cores remained active throughout. The existing A1/D2 logical core 105 stayed inactive. PVT remained functional. At 1160 mV, throughput fell modestly; restoring 1200 mV recovered it without PLL changes, an identical-work reset, or PVT reinitialization.

| Setting per external half-stack | Hold | Counter hashrate | Reported power | Estimated efficiency |
|---|---:|---:|---:|---:|
| 1200 mV (baseline) | 122 s | 2.964 TH/s | 50.26 W | 16.96 J/TH |
| 1190 mV (voltage-1190) | 122 s | 2.966 TH/s | 49.51 W | 16.70 J/TH |
| 1180 mV (voltage-1180) | 121 s | 2.966 TH/s | 48.32 W | 16.29 J/TH |
| 1170 mV (voltage-1170) | 120 s | 2.960 TH/s | 47.25 W | 15.96 J/TH |
| 1160 mV (voltage-1160) | 305 s | 2.938 TH/s | 46.20 W | 15.73 J/TH |
| 1200 mV (restore-1200) | 121 s | 2.968 TH/s | 50.06 W | 16.87 J/TH |

Power is TPS546 output power plus the configured zero-watt Proto allowance, not wall power. These are short sequential holds with temperature changing between points, not randomized steady-temperature trials. The first 15 seconds of each phase are excluded from averages; only complete, fresh, distinct mining-counter epochs are used.

1160 mV reduced reported power by 8.08% and J/TH by 7.26%, with 0.88% lower total throughput. This did not yet reach 15 J/TH.

## Core and sensor detail

- 129 complete mining epochs across all six holds; no new zero-PASS cores or zero-throughput domains. 1160 mV includes 46 epochs and a minimum short-window domain rate of 92.96% ideal.
- 1190 and 1180 mV showed no core with mean throughput below 90% of its baseline. At 1170 mV, A2/D3 logical core 124 fell from 4.564 to 3.989 GH/s (87.4% of baseline). At 1160 mV it fell to 3.785 GH/s (82.9%), and A3/D0 core 0 fell from 4.769 to 4.202 GH/s (88.1%). These are indications of reduced margin despite the unchanged active-core count; a longer repeat is needed to establish the boundary.
- After returning to 1200 mV, no previously active core averaged below 90% of baseline. Aggregate recovery mean was 2967.56 GH/s.
- Logs contain 2156/2157/2157/2157 distinct fresh-valid thermal reads on A1/A2/A3/A4 during the comparison, with zero thermal retry/invalid lines and no voltage conversion error lines. Temperature and voltage telemetry changed on all four ASICs in every phase. Voltage API values alone are not proof of independently completed conversions; no extra diagnostic/PVT strobe was inserted during the sweep.
- Zero rejected shares during all holds; final device counters 64 accepted / 0 rejected. Short share counts are not used to estimate hashrate.
- Peak sampled ASIC temperature across holds: 54.68 C. At 1160 mV: at most 51.05 C after the excluded transition interval.

## Firmware and method

- Base checkpoint: `cef666d55805674210828250f49df88420f05519` on `1103x-support`.
- App-only OTA: `v2.14.0-54-gcef666d-dirty`, SHA256 `59f3e3eeb5211661aeb5036668051c4e10cb684e7da9c14999047b17908a5d81`.
- Startup code and MC3 driver are unchanged: 1160 mV/300 MHz warmup to 40 C, full supply cycle, unloaded ramp to 600 MHz at 1200 mV, PVT recovery, first work and qualification, pool mining.
- The new bench endpoint queues voltage requests to the power task: `POST /api/system/diagnostics/voltage?millivolts=1190` with an empty body. Accepts 1160..1200 mV in 10 mV increments, for a mining stock 1103 configured at 1200 mV / 600 MHz. Reductions are limited to 10 mV per applied step. `GET /api/system/diagnostics` reports `liveVoltageMv`, the last successfully applied command, not a voltage measurement.
- Renew within 90 seconds or the target returns to 1200 mV. Restoration also advances in 10 mV increments. Saved NVS voltage remains 1200 mV so subsequent startups retain the verified sequence. HTTP does not touch the regulator or ASIC buses. Pause/reinitialization clears the volatile request. Normal thermal/fault handling remains in place.
- Host monitoring renewed the lease every 25 seconds, sampled approximately every 5 seconds, and stopped lowering for persistent new zero cores, domain/aggregate collapse, temperature/PVT failure, unexpected reset/frequency/settings changes, or incorrect regulator voltage. No stop condition fired.
- After 1160 mV, the power task restored 1170/1180/1190/1200 mV over approximately two seconds. A final two-minute 1200 mV hold verified recovery. No runtime PLL, work-reset, or PVT-reinitialization command was added.
- Saved startup voltage, fan settings, and frequency were not changed. Fan stayed under existing automatic control (observed 99%).

## Interpretation and next comparison

This isolates steady mining from first-work startup: the earlier trial dropping to 1160 mV before the first 600 MHz job failed, whereas this trial dropping after healthy mining maintained all previously active cores. The 1160 mV throughput loss was reversible by voltage alone. This supports different startup and running voltage requirements, but does not establish a universal minimum or long-term stability.

1180 mV is the cleanest lower-voltage point in this run (about 4% better efficiency, no measurable total loss or individual core below 90% baseline). 1170 mV gives about 6% better efficiency with almost unchanged total rate, but core 124 deserves a longer observation. 1160 mV gives about 7% better efficiency with about 0.9% total loss. Repeat longer holds and startups before making an automatic persistent voltage reduction part of normal firmware.

## Validation and retained artifacts

- Firmware build and `git diff --check` passed.
- `test_live_voltage.py`: host-compiled current override functions; queued writes, step bounds, lease renewal/expiry, incremental restore, write-failure recovery, stop cancellation, and board/settings gating.
- Existing `test_pool_startup.py` passed, including the 1200 mV startup/PVT path and fault cases.
- `run_comparison.py` performed OTA and the complete monitored sequence; `analyze.py` recreates detailed per-core and PVT results.
- `manifest.json`, `results.json`, `analysis.json`, `pvt-summary.json`, sanitized snapshots, phase epochs, and `startup-events.txt` are retained. Raw logs, sample streams, and the exact binary are local ignored artifacts.

Final state: mining at 600 MHz / saved and applied 1200 mV; no monitoring process remains. The volatile bench endpoint remains in this experimental firmware. Production automatic undervolting has not been enabled.
