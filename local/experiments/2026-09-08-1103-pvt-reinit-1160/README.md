# PVT-only reinitialization at 1160 mV, PLLs fixed at 600 MHz — 2026-09-08

User requested extending the 1200-to-1160 mV sensor test with PVT-only
reinitialization. Retain the prior three startup conditions: 40 C minimum with
ten-second warmup dwell at 300 MHz, 1200 mV/half, 2-second high-frequency steps
and final 1.5-second settle. No first 600 MHz work is sent.

Capture three sets of temperature and per-ASIC voltage-conversion readings:
(1) 1200 mV after the unloaded ramp, (2) after dropping to 1160 mV and confirming
fresh regulator voltage following a 1-second settle, (3) after reinitializing
only the voltage and thermal sensor blocks at 1160 mV, then waiting 100 ms.
The third stage reuses the existing sensor setup commands, reads STATUS/CLOCK
for both blocks and PLL0_EN on each ASIC, then performs bounded fresh thermal
and voltage conversions. It does not call MC3_init, reset ASICs, change PLLs,
cycle supply, or send work. It reapplies the sensor clocks, which are separate
registers from ASIC PLL configuration. All four ASICs are included as controls.

Sensor setup has the existing three bounded 500 ms A1 status waits. Per-ASIC
readback and subsequent independent conversion checks establish actual recovery;
returning from setup alone is not proof. Both sensor locks use try-lock and
skip if busy. Thermal/voltage reads retain bounded retry/sweep behavior.
Startup guards remain active and credible hot readings abort the experiment.
The test always shuts ASIC power off afterward, regardless of sensor recovery.

Tests passed for exact nine sensor-only writes (no PLL/work writes), all-chip
readback, bounded setup waits, busy/chain rejection, three-stage startup order,
zero jobs, setup failure handling and final shutdown. Existing voltage sweep
freshness tests also pass. ESP-IDF build passed. Cool with ASIC power off and
fan confirmed 100% for two minutes, restore automatic fan settings, then flash
application-only OTA. No serial or scope is used.

## Result: all four PVT sensor paths recovered after sensor-only setup

Flashed application SHA-256:
`c741f5037e783c37e88d2d3244fc58d58a1252d484145fb85c026ec7d7877817`.
Fresh starting temperatures were **26.64 / 25.81 / 25.81 / 26.31 C**. Warm
300 MHz qualification passed at **1488.68 GH/s**. The diagnostic began after
the unloaded 600 MHz ramp at uptime 190.549 s.

| Stage | A1/A2 temperatures | A3/A4 temperatures | A3/A4 voltage conversions |
| --- | --- | --- | --- |
| 1200 mV baseline | 48.08 / 46.43 C | Invalid after retries | 0/12 each; DONE=0, STATUS=3, raw zero |
| 1160 mV, before PVT setup | 47.58 / 46.10 C | Invalid after retries | 0/12 each; DONE=0, STATUS=3, raw zero |
| 1160 mV, after PVT setup | 47.58 / 46.10 C | **47.09 / 46.76 C, valid** | **12/12 each; DONE=1, STATUS=0** |

The 1160 mV command was issued at 194.670 s and fresh regulator total was
**2323 mV** after one-second settling. Lowering voltage alone again left A3/A4
invalid. PVT-only reinitialization ran from **199.591 to 199.732 s**. Readback
then showed both sensor STATUS registers zero and both sensor CLOCK registers
`0x01010000` on every ASIC. PLL0_EN was **0x00000007** on all four, including
lock bit 0x4. No PLL writes, ASIC reset or supply cycle occurred in this stage.

After 100 ms settling, **all four temperatures were valid on the first thermal
attempt**. All four then completed **12/12 fresh voltage conversions**. Final
fresh VDD readings were **1141.64 / 1142.35 / 1166.02 / 1171.81 mV** for A1–A4.
These are on-chip measured voltages, not the 1160 mV half-stack setpoint. The
final sweep took **980 ms**, compared with 2108 ms at 1200 mV and 1963 ms at
1160 mV before setup. `result.json` contains all twelve temperature and voltage
readings; `sensor-events.json` retains raw status/clock and conversion evidence.

This demonstrates recovery of the PVT conversion paths at **1160 mV / fixed
600 MHz after PVT-only reinitialization**. It rules out the observed bad sensor
state requiring a full ASIC reset in this test. It does not identify which
sensor setup write was necessary, establish that voltage reduction was required
for reinitialization to work, or prove the cause of the original failure.
Sensor recovery also does not establish that all mining cores/domains work:
no first 600 MHz job, hashrate qualification or pool mining was performed.

The experiment completed and deliberately shut ASIC power off. A final HTTP
check at uptime 259 s confirmed **34 mV**, no mining, zero shares and diagnostic
epoch 0. Saved NVS remains **1200 mV / 600 MHz**. The temporary three-stage
firmware remains installed and stops after the sensor comparison on every
1200 mV / 600 MHz startup, even when PVT recovers. AxeOS `startup failed` is
therefore the deliberate diagnostic stop, not a claim that PVT recovery failed.
No subsequent restart was issued. No scope was used.
