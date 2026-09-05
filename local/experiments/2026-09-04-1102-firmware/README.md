# 1102x firmware prepared for the matching warm-ramp comparison

Prepared on 2026-09-04. **No device was flashed or operated during this
preparation.** The user says 1102 is not connected yet. The previous 1103
ceramic test remains the latest hardware result.

## Regulator and board configuration

The user initially requested 1500 kHz, then corrected it to **1300 kHz**.
1102 selects 1300 kHz, VOUT_SCALE_LOOP=0.125 and COMPENSATION_CONFIG
**13 20 C6 19 C6**. These match the supplied 1102 warm boot log's setting and
readback and the reference source at cd82a9e. The local July 2 WEBENCH report
also specifies 1.3 MHz, 2.4 V/30 A, 230 nH, two 100 µF plus two 22 µF output
capacitors. Report pages 7 and 8 describe the compensation and operating point;
it does not directly print the five-byte register encoding.

References:
- `../2026-09-04-1102-warm-log/original.log`: frequency at line 86, compensation
  setting at line 93 and readback at line 167.
- `/Users/skot/Bitcoin/bitaxe/bitaxeProto-1102x/WebBench/WBDesign-1102x.pdf`.
- Historical `main/power/vcore.c` and `main/device_config.h` at cd82a9e.

The 1102 board entry was restored alongside 1103. The KiCad netlist confirms
U12 GPIO10 (pin 18), U2 EN/UVLO (pin 27), TP1 and R3 pin 2 share `/Data/PWR_EN`.
Thus both boards support active-high GPIO10 ASIC enable. The old firmware's
1102 entry omitted that capability; this port enables it. GPIO2/ADC1_CH1
(U12 pin 38) is explicitly unconnected. No midpoint sensing or 1103 ADC/rail
calibration offsets are assigned to 1102.

1103 retains 550 kHz, scale loop 0.25, and its user-selected ceramic
compensation 13 11 8C 19 86. Common voltage startup and ASIC logic are preserved.

## Test image

`esp-miner-1102-warm-probe.bin` is the prebuilt app image for the next test.
It has `PROTO_RUN_WARM_RAMP_PROBE=1` and `PROTO_WARM_BOARD_VERSION="1102"`.
It rejects a mismatched Proto board, wrong configured chip count, or self-test
mode before the ASIC power sequence. Relevant source snapshots and sdkconfig
are retained locally, with hashes in `sha256.json`.

The ASIC test sequence is the same as the last 1103 ceramic run: temporary
1140 mV per half, initial qualification to 300 MHz, reduced-fan warmup until
hottest >=54.5°C and coldest >=52°C for ten seconds, warm baseline
qualification, then scope-coordinated 25 MHz broadcast steps toward 600 MHz.
Domain PASS/FAIL, PVT, temperatures, qualification duration, work resets and
fixed-frequency recovery observation remain in place. `sequence-check.json`
records the unchanged ASIC call expressions inside the warm-ramp function.

Two hardware adaptations are necessary:

- **No midpoint ADC:** the independent 20 ms firmware guard uses TPS total
  voltage and stops on invalid/nonpositive readings or >=2400 mV. It cannot
  detect redistribution between the two halves. Logs say PCB/balance
  `unavailable`; they do not treat half of total voltage as a measurement.
  Internal ASIC PVT and domain counters remain available.
- **Shutdown:** assert GPIO enable low, then issue PMBus OPERATION OFF on
  1102. Failed shutdown calls are propagated and logged rather than claiming
  power-off success. The 1103 guard keeps its established GPIO-only disable.

The scope continues to assess lower=CH2 and upper-estimate=CH1−CH2 with raw
peaks <1.25 V and sliding 20 µs means <1.22 V. CH1 stays 50 Ω at the TPS output
capacitors, CH2 1 MΩ at GND0−GND, DC/1×/20 MHz bandwidth. These are after-event
scope checks, not instantaneous protection. Lack of firmware midpoint sensing
is a material difference from the 1103 run, even with identical ASIC commands.

`capture_warm.py` retains that protocol and requires `BITAXE_BENCH_PORT` to
select the connected 1102 serial port explicitly; there is no default to the
1103 port. It has not been run. Target identity, actual probe connections,
partition/boot selection, GPIO shutdown and TPS readbacks must be checked on
connection before interpreting any hardware result.

## Builds and verification

The guarded test image and normal image both build with ESP-IDF 5.5.1.
`esp-miner-normal.bin` has the warm probe disabled; the working-tree source
also has the warm probe disabled, with the next probe target left at 1102.
`build/esp-miner.bin` is the normal build, **not the saved test image**. Use the
explicit saved test artifact when preparing the subsequent controlled flash.
Persistent device settings have not been changed.

`test_board_support.py` compiles the actual regulator-selection, rail-check,
and shutdown helper functions against injected telemetry and IO failures.
Checks cover both board configurations, the exact voltage boundaries,
invalid telemetry, absent/failed ADC, GPIO-first shutdown, PMBus fallback and
error propagation. They passed under host C11 with warnings treated as errors.
A successful host test/build does not verify physical enable wiring or live
regulator readback; that remains part of connection-time checks.
