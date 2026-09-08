# Stock 1103: 40 C warmup, faster ramp, 1180 mV — 2026-09-07

User-requested combined comparison against the successful 45 C / 1180 mV
startup. Every ASIC must now reach **40 C for ten seconds at 300 MHz**, and
remain at least 40 C at the fresh read before first 600 MHz work. The 25 MHz
PLL steps from 300 through 575 MHz now settle for **1500 ms instead of 2000 ms**.
The final 600 MHz settle remains 1500 ms, and the steps below 300 remain 500 ms.
This shortens the configured unloaded ramp by six seconds. Voltage stays at
**1180 mV per board half-stack throughout mining**. No 1200 mV startup or
post-start voltage reduction is implemented in this experiment.

Supply cycling, first-work sequence, thermal retries, voltage ceiling and
report-only sampled domain voltage telemetry are unchanged. Boundary/failure
host tests pass at 40 C, reject a single 39.9 C ASIC, and verify the exact new
PLL cadence and existing final settling. Prior source files are archived here
with `.before` suffixes; the successful 45 C firmware remains in
`../2026-09-07-1103-1180mv-45c/esp-miner.bin`.

## Preparation

ASIC power was stopped via the API. Paused mode forces fan 30%, so the existing
pre-power settings validation was used to hold ASIC power off while allowing
manual fan 100%: temporarily save 1181 mV (unsupported), then release pause.
This voltage was never applied to powered ASICs. The confirmed regulator
output was 34 mV with fan 100%. Cool for at least two minutes, restore the saved
1180 mV and original automatic fan control, then upload application-only OTA.
No serial connection, scope, or automatic firmware recovery loop is used.

`preparation.json` records original state and cooling; `manifest.json` records
firmware identity. API diagnostics reads use the existing passive cache.
A successful startup is followed by a five-minute pool observation. Count
active domains separately from active cores; previous successful boots had
16 active domains but persistent zero PASS on A1 D2 logical core 105.

Efficiency uses the firmware power estimate divided by complete-window TH/s.
The TPS546 path in `main/power/power.c` uses output power plus a configured
board allowance (currently 0 W for Proto); it is not a wall-power measurement.

## Result: 600 MHz qualification failed

ESP-IDF build and startup host tests passed. The flashed application SHA-256
is `945717ed7dfe264e6dc814d009b3d01dab1b774a589b5245bd18811ee646890a`.
First fresh ASIC temperatures were **29.11 / 28.61 / 28.28 / 28.61 C**, close
to the preceding cooled 45 C test. Initial 300 MHz qualification passed at
1483.38 GH/s; after the 40 C dwell it passed again at **1486.66 GH/s**.
End-of-dwell temperatures were **42.14 / 41.32 / 41.48 / 41.81 C**.

The unloaded ramp completed with valid temperature readings before work:
**46.76 / 45.27 / 46.26 / 46.43 C**. First work began at **164.557 seconds**
uptime versus 224.157 seconds in the 45 C / slower-ramp test, about **59.6
seconds earlier**. Measured 300-to-600 MHz PLL update time decreased from
24.449 to 18.451 seconds. These timings compare two individual boots and
include the warmup's dependence on initial temperatures.

The 600 MHz qualification failed at **2593.80 GH/s**. **A2 D0 and A3 D2** each
had all 39 cores at zero PASS. The remaining 14 domains were active, with the
known A1 D2 logical core 105 also at zero PASS: **545/624 active cores** total.
ASIC rates were **736.66 / 556.66 / 557.38 / 743.10 GH/s**. Temperature reads
remained valid throughout the ten-second qualification, ending at
48.90 / 47.58 / 48.90 / 48.41 C. This was a throughput failure after first work,
not a pre-work temperature-conversion failure.

Firmware stopped ASIC power after qualification. A subsequent API check at
uptime 212 seconds confirmed regulator output **34 mV**, startup result
`failed`, no pool mining, and retained diagnostic epoch 11. The 40 C / faster
firmware and 1180 mV setting remain installed; no retry or reset was issued.
The highest sampled half-stack voltage was **1192 mV**, with no sampled values
above 1200 mV. These samples do not resolve fast voltage transients.

There is **no sustained mining or J/TH result** for this attempt. The pool-only
summary's zero observed domain-dropouts refers to zero mining epochs, not to
startup health; `result.json` and `failure.json` hold the failed startup data.
The combined lower-threshold/faster-ramp test did not succeed on this boot.
It does not distinguish temperature from PLL cadence or establish their
individual effects. The next isolating comparison would retain 40 C / 1180 mV
and restore the 2-second high-frequency cadence, starting from similarly cooled
ASICs. The proposed 1200 mV startup followed by reduced mining voltage remains
untested.
