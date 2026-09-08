# Voltage-sensor diagnostic after thermal failure — 2026-09-07

User requested checking other A3/A4 sensors when their post-ramp temperature
conversions fail. Retain 1200 mV/half, every-ASIC 40 C gates, ten-second 300 MHz
dwell, 2-second high-frequency PLL steps, final 1.5-second settle and shutdown
on failed startup. No PLL/work/sensor setup change before the failure point.

New bounded failure-path diagnostic runs only for invalid temperatures on the
fresh pre-work read, without any credible >=70 C value or already tripped
startup guard. It snapshots voltage STATUS/DONE and VDD DOUT 13/14 for all four
ASICs, then triggers up to twelve voltage conversions to cover the configured
channels 4–15. Each conversion polls DONE independently on every ASIC, logs
readiness/status, and clears the latch between samples. This avoids the normal
voltage sweep's reliance on only A1's DONE. It does not reinitialize sensors.

Final raw values are marked fresh only after all twelve conversions completed
on that ASIC with successful status/read checks and valid decoded data. Before
values and incomplete sweeps remain explicitly non-fresh. Budget is 2500 ms,
reserving 400 ms for final snapshots, with a 100 ms maximum poll per round.
An in-flight bounded register read/snapshot may finish just after the budget;
slow-read host tests verify an upper bound of 2600 ms for the injected case.
A busy voltage-sensor lock skips the diagnostic rather than waiting. Existing
independent startup guard remains active; no work is sent after thermal failure.

Host tests cover all-healthy sweeps, A3/A4 conversion failure, register-read
failure, stale/fresh labeling, deadline, busy lock/chain rejection and exclusive
voltage triggering. Startup integration tests verify no job, diagnostic only
on pre-work invalid read, no delay on credible overtemperature, and shutdown.
ESP-IDF build passed. Firmware was flashed application-only after at least
two minutes of ASIC power off/fan 100%, with original fan settings restored.
No serial connection or scope is used. No automatic reset after the result.

## Result: both voltage and thermal conversions fail on A3/A4

Application SHA-256:
`1e1109960c93e51c948ec9d231f814b0359cbe3b4fde0addeab12cc56621af64`.
Fresh initial temperatures were **27.62 / 27.13 / 26.96 / 27.46 C**. Warm
300 MHz qualification passed at **1487.05 GH/s**. The normal post-ramp thermal
failure repeated on A3/A4 across all three attempts, with A1/A2 valid around
48.08 / 46.59 C on the final retry. No first 600 MHz work was sent.

The new voltage diagnostic ran **12 rounds in 1973 ms** and then shut down.

| ASIC | Fresh completed voltage conversions | Final DONE / STATUS | Voltage result |
| --- | --- | --- | --- |
| A1 | 12/12 | 1 / 0 | Fresh 1183.39 mV |
| A2 | 12/12 | 1 / 0 | Fresh 1183.80 mV |
| A3 | 0/12 | 0 / 3 | Raw13=0, raw14=0; invalid |
| A4 | 0/12 | 0 / 3 | Raw13=0, raw14=0; invalid |

A3/A4 returned successful status/data register responses throughout, but no
voltage conversion completed within any diagnostic poll window. Their invalid
-554.97 mV decoded values are artifacts of raw zero, not measured negative
supply voltages. The thermal failure is therefore accompanied by failure of
the on-chip voltage conversion path on the same two ASICs, while serial
register communication remains responsive.

Before triggering any new diagnostic voltage conversion, A1/A2 retained their
pre-ramp raw voltage data (decoded 801.99 / 802.29 mV); A3/A4 already had zero
raw13/raw14, although voltage STATUS initially read 0 on all ASICs. After each
trigger, A3/A4 STATUS read 3. The earlier 800 mV initialization read had reported
A3 **803.0 mV**, A4 **806.4 mV**, so their voltage data had become zero between
that initial read and the failure-path snapshot. That interval includes thermal
sensor setup, voltage ramp, PLL ramp and failed thermal conversions. It does
not isolate which operation changed the sensor state. The older initialization
voltage routine does not independently verify every ASIC's DONE, so its plausible
voltage data should not be given the stronger freshness guarantee of this new
diagnostic.

This evidence supports a broader A3/A4 on-chip sensor-state/conversion problem,
not a thermal-only result-decoding issue or total loss of communication. It
still does not establish direct high-voltage sensor sensitivity, shared-clock
failure, reset, or the responsible startup command. A next localization test
could snapshot these registers after sensor setup and after the voltage ramp,
before changing PLL frequency, to find where the data/state first changes.

Sampled peak half-stack voltage was 1213 mV; no scope was used. Post-monitor
inspection at uptime 264 seconds confirmed ASIC power off (**32 mV**), startup
failure with diagnostic epoch 0, zero shares, 1200 mV / 600 MHz saved settings,
and automatic fan output 99%. The diagnostic firmware remains installed; it
still shuts down after a failed pre-work thermal check. No retry was performed.

`result.json` records before/after raw data and freshness; `sensor-events.json`
contains all round-by-round evidence. No 600 MHz hashrate, active-core count or
steady efficiency measurement exists for this aborted startup.
