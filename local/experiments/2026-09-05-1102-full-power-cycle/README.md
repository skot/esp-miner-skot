# Success: current firmware recovers warm 600 MHz / 2.968 TH/s

2026-09-05, connected 1102 hardware. The full ASIC power-cycle variant completed a **120.758-second** 600 MHz observation with **all 16 internal domains healthy**, no zero-PASS cores in the measured hold, and **no invalid temperature readings anywhere in this run**. Firmware then deliberately powered ASICs off. This is a successful instrumented synthetic-work bench, not a deployed automatic startup path or a pool-share measurement.

## Result

| ASIC | Whole-hold passing throughput | Final temperature |
|---|---:|---:|
| A1 | 743.07 GH/s | 59.79°C |
| A2 | 740.03 GH/s | 58.47°C |
| A3 | 742.82 GH/s | 59.62°C |
| A4 | 741.81 GH/s | 59.46°C |
| Total | **2967.73 GH/s** | |

The final 30 seconds averaged **2967.91 GH/s**. Whole-hold per-domain throughput ranged from **98.143% to 99.462% of ideal**. Every ASIC's fresh sensor values continued changing (14–15 distinct values after launch); all reported DONE=1 with valid data. Maximum reported temperature was 60.28°C. See result.json for every domain and both measurement windows.

Warm 300 MHz qualification passed at 1487.77 GH/s (99.34%). ASIC supplies were disabled at boot 315.136 s, and TPS discharge to 48 mV was verified at 315.417 s. All four ASICs were re-enumerated at 100 MHz. Vcore was raised from 800 to 1160 mV per half, then the PLLs ramped without work or thermal conversion. All four PLLs locked at 600 MHz at 334.552 s. First fresh temperatures after settling were **57.81/56.16/57.15/57.31°C**. The first synthetic job was sent at 336.150 s, observation launched at 336.327 s, and successful completion logged at 457.085 s. Power-off followed at 457.094 s.

## What changed and what this establishes

Compared with the failed deferred-thermal experiment, this version fully disabled both Vcore and VDDIO, verified Vcore discharge, restarted at 800 mV per half and repeated the voltage ramp at 100 MHz. Both variants used historical PLL tuples, the current broadcast CFG/EN burst, the current strict per-ASIC temperature reader, no work during the PLL ramp and the same final settling interval.

The complete sequence works on this board while retained-rail ASIC reset did not. This strongly points toward startup/power-state sequencing. It does **not** yet isolate rail cycling from the 800→1160 mV voltage trajectory or quantify repeatability. The current temperature reader and broadcast CFG/EN burst can both work at 600 MHz; neither must be removed to reproduce this successful run. The earlier sensor failures were not simply fabricated negative temperatures or a single transient read failure.

The exact old-image positive control separately reached 2968.79 GH/s with real pool work and four changing temperatures after a warm ESP reboot. Together these reproduce the user's hardware result in both old firmware and current instrumentation.

## Scope

CH1 remained **50 ohms** at TPS output caps; CH2 remained 1 Mohm at GND0. The 700k-point/channel record spans -10 to +60 seconds at **100 µs spacing**, including the power cycle, ramp, first job and initial hashing. Both channels have **zero clipped samples**. Capture handshake/audit passed and the firmware received its acknowledgement.

Raw maxima: TPS **2.58 V**, lower half **1.78 V**, upper difference estimate **1.28 V**. These are uncorrected measurements at different physical points. The sampling does not resolve 20 µs averages and may miss faster peaks. Prior voltage thresholds were exceeded, as in the successful old-image control; voltage remained report-only under the user's existing instruction. Thus the presence of these measured peaks does not by itself identify failed startup.

## Artifacts and installed state

- `probe.bin` / `probe.elf`: tested image, SHA256 `ca853994da970ddfc1bfb898ea4a3b7378481c600101e2e99c2a9c89e390949c`; app-only flash verified. Still installed after the run, with ASIC power off.
- `normal.bin`: ordinary build with all bench flags disabled; **does not automatically invoke this successful warm-cycle sequence**. It was built but not flashed.
- Source snapshots, build logs, focused sequencing/thermal/board/scope/zero-rail tests, serial.log, thermal-summary.json, domain-samples.json/csv, phase-summary.json and result.json.
- `deferred-start/raw.npz`, input-settings.json, audit.json and waveform.png/pdf preserve the scope measurement.
- `capture_deferred.py` is the tested supervisor; this bench requires its scope/serial handshake on each run. NVS settings remained unchanged.

Next implementation step: turn this demonstrated warm-cycle sequence into an explicit startup mode that returns control to ordinary pool work after qualification, then validate sustained pool mining and repeat cold/warm starts. Preserve this exact successful artifact as the reference before reducing its steps.
