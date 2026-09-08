# PVT reinitialization at 1200 mV, fixed 600 MHz - 2026-09-08

Compare PVT before/after the exact existing sensor-only setup without lowering
voltage. Keep the preceding full-test startup conditions: saved 1160 mV,
300 MHz warmup until every ASIC >=40 C for ten seconds, full Vcore/VDDIO cycle,
800 mV/100 MHz initialization, raise to 1200 mV and ramp unloaded to 600 MHz
with two-second high-frequency steps and 1.5-second final settling.

Wait another second, require fresh TPS total 2340-2460 mV, then collect bounded
thermal and twelve-round voltage diagnostics on all four ASICs. Reapply the
same nine PVT-only writes, read both sensor STATUS/CLOCK and PLL0_EN on all
four, wait 100 ms, and repeat fresh thermal/voltage diagnostics. No voltage,
PLL, work or ASIC-reset commands occur between stages. Existing A1-only setup
waits remain to keep the intervention identical to the successful 1160 mV
sensor experiment; per-chip readback and independent conversions determine
actual recovery. Setup return alone is not proof.

Keep thermal/fault/cancellation guards. This temporary path always powers ASICs
off after the diagnostic; AxeOS startup-failed status is expected even if PVT
recovers. No pool mining or 600 MHz hashrate measurement is intended.

Host tests passed for fixed voltage/PLL/no work, before/after order, hot/readback/
setup/PLL/chain/pause failures, shutdown and unaffected other operating points.
The exact PVT-write and independently bounded voltage-conversion tests passed.

Cool with ASICs off and fan100 for at least two minutes, restore prior automatic
fan control, then application-only OTA. No serial or scope. ESP-IDF 5.5.1 build and `git diff --check` passed. Application OTA succeeded.

## Result: sensor recovery at 1200 mV without a voltage drop

Application SHA256:
`876ea6466b559eaa36799e8afc7e63a1f3c449f8cad686a6bc67d3411b8bbfc5`.
MC3 source remained byte-identical to the earlier 1160 mV recovery experiment.

Initial fresh temperatures were **27.79 / 27.13 / 27.13 / 27.79 C**.
The final warm 300 MHz qualification passed at **1486.45 GH/s**.
The 600 MHz diagnostic began at uptime 187.399 s, with fresh TPS546 total
**2403 mV** after the extra one-second settle. No voltage command was issued
between baseline, sensor reinitialization and final readings.

| ASIC | Before temp (C) | After temp (C) | Before fresh voltage conversions | After fresh voltage conversions | After VDD (mV) |
|---|---:|---:|---:|---:|---:|
| A1 | 48.24 | 48.57 | 12/12 | 12/12 | 1183.90 |
| A2 | 46.59 | 46.92 | 12/12 | 12/12 | 1184.61 |
| A3 | Invalid | 47.75 | 0/12 | 12/12 | 1201.68 |
| A4 | Invalid | 47.75 | 0/12 | 12/12 | 1207.67 |

Before setup, A3/A4 voltage DONE=0, STATUS=3, raw13/raw14=0. After setup,
all four voltage and thermal STATUS readbacks were zero, both sensor CLOCK
readbacks were 0x01010000, and PLL0_EN was 0x7. Setup took **178 ms**; after
100 ms settling, all four temperatures were valid. All four then completed
12/12 fresh voltage conversions, DONE=1 and STATUS=0, in a 1158 ms sweep.
These VDD readings are chip measurements, not PVT VDDIO12 supply measurements.

This demonstrates that **lowering core voltage is not required for PVT
recovery in this observed state**. The sensors can operate at the 1200 mV
setpoint after reapplying their setup while keeping PLLs at 600 MHz. It argues
against a simple steady 1200 mV cutoff and supports investigating sensor setup
or state disturbed during startup/ramp. It does not identify the necessary
write, distinguish a supply/clock transient from an initialization race, or
prove long-term/repeated reliability at this voltage. The baseline sweeps also
add elapsed settling time, so this is not a randomized elapsed-time control.

No first 600 MHz work was sent, so the result does not establish mining-core
health at 1200 mV or successful operation after a later drop to 1160 mV.
The comparison completed at 194.128 s and deliberately powered ASICs off.
Final HTTP state at uptime223 s: **34 mV regulator output**, no mining/shares,
saved1160 mV / 600 MHz, automatic fan restored to99%. The temporary firmware
remains installed: this 1103/1160/600 setting runs the boosted diagnostic and
stops, even when sensors recover. AxeOS `startup failed` is the deliberate stop.
No additional reset or scope measurement was performed.
