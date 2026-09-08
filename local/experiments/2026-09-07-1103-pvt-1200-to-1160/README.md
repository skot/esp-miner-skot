# PVT recovery after dropping 1200 to 1160 mV — 2026-09-07

User requested ramping at 1200 mV and lowering to 1160 mV to test whether PVT
readings recover. Keep 40 C minimum/ten-second warmup dwell at 300 MHz, then
full supply cycle and unloaded 2-second-step PLL ramp to 600 MHz at 1200 mV.
After final settling, this temporary experiment runs for every 1200 mV / 600 MHz
startup, whether or not the baseline thermal read fails:

1. Read temperatures and perform the existing bounded per-ASIC voltage sweep
   at 1200 mV. A credible hot value or tripped guard aborts immediately.
2. Command 1160 mV per board half, leave PLLs at 600 MHz, and settle 1000 ms.
3. Read fresh regulator output and require 2270–2370 mV total to confirm the
   drop, then retry thermal conversions and the same voltage sweep.
4. Shut ASIC power off regardless of recovery; do not send the first job.

No ASIC reset, sensor setup or PLL update occurs between the paired readings.
The 1160 mV command is applied to hardware only; saved NVS remains 1200 mV.
No claim about core activity or efficient mining can be made from this no-work
sensor test. AxeOS reports startup failed because this diagnostic deliberately
returns through the shutdown path; use PVT_DROP logs to distinguish completed
diagnostic from a failure to perform the experiment.

The existing voltage sweep has a 2500 ms budget and checks each ASIC's freshness
independently. Thermal reads retain three bounded attempts. Independent startup
guards remain active. Host tests verify order, unchanged PLL, zero jobs, both
sensor passes, voltage-command failure/pause handling and shutdown even when
readings are valid. MC3 voltage diagnostic tests also pass. ESP-IDF build passed.

Start from confirmed ASIC power off and fan 100% for two minutes, restore
original automatic fan settings, and flash application-only OTA. No serial,
scope, sensor reinitialization or automatic subsequent restart is used.

## Result: lowering voltage alone did not recover the sensors

Flashed application SHA-256:
`e65d583823d0f60d6ba75bf0c9f6b7269b7efdd12c4ad2f4657022e9d0117f23`.
Fresh starting temperatures were **25.81 / 25.15 / 24.99 / 25.48 C**. Warm
300 MHz qualification passed at **1494.68 GH/s**. After the unloaded 600 MHz
ramp, the paired test began at uptime 196.534 s.

| Setting | A1 / A2 temperature | A3 / A4 temperature | A1 / A2 fresh voltage | A3 / A4 voltage conversions |
| --- | --- | --- | --- | --- |
| 1200 mV | 47.91 / 46.26 C | Invalid after three retries | 1184.51 / 1184.82 mV | 0/12 each |
| 1160 mV | 47.42 / 45.77 C | Invalid after three retries | 1140.11 / 1140.72 mV | 0/12 each |

At both settings A3/A4 voltage registers remained readable but reported
`DONE=0`, `STATUS=3`, raw13/raw14 zero. A1/A2 completed 12/12 voltage conversions
at both settings. Thermal status/data responses likewise remained readable;
A3/A4 did not produce valid fresh temperatures. Negative decoded numbers from
zero data are invalid measurements, not physical negative temperature/voltage.

The 1160 mV command was issued at uptime **200.429 s**. After 1000 ms settling,
the fresh TPS546 total was **2323 mV** at 201.444 s, confirming the lower supply.
The second thermal pass ended at 203.431 s; final voltage snapshots ended at
205.488 s. Thus no recovery was observed over roughly five seconds following
the drop, including the conversion retries and sweep. Voltage sweeps took
1950 ms before and 2057 ms afterward. PLLs remained at 600 MHz throughout;
no ASIC reset, sensor initialization, additional PLL write or first work was
sent during the comparison.

The failure therefore persists after returning to 1160 mV in this tested
state. This argues against an immediately reversible voltage-only reading
error. It does not prove permanent damage, identify a shared clock/reset fault,
or establish that a longer wait or explicit PVT reinitialization cannot recover
it. A next targeted comparison could reinitialize only the PVT blocks at
1160 mV while leaving the ASIC PLL and work state unchanged.

The test completed and deliberately shut ASIC power off at 205.540 s.
A later HTTP check at uptime 269 s confirmed **34 mV**, zero shares, no mining,
startup result `failed` and diagnostic epoch 0. Saved settings remain **1200 mV /
600 MHz**; the temporary 1160 mV command did not change NVS. **This diagnostic
firmware remains installed and deliberately stops every 1200 mV / 600 MHz
startup after the paired test, even if PVT readings recover.** No further reset
was issued. There is no 600 MHz core activity or sustained efficiency result.

`result.json` holds the paired readings, `sensor-events.json` the raw diagnostic
log lines, and `post-monitor-state.json` the final state. No scope was used.
