# 1102 deferred-work test: stopped before first post-reset job

2026-09-05. **Prepared image flashed and run with user authorization. The test stopped at 550 MHz on an invalid A3 temperature reading; it did not reach 600 MHz or send the first post-reset job.**

## Observed sequence

- Board identity: 1102, MAC `10:B4:1D:D5:CA:14`, serial `/dev/cu.usbmodem11434401`, IP `192.168.30.193`. App-only flash of saved `probe.bin` completed with esptool hash verification. SHA256 `9d391e532ecbc7e165b3dd23aaab1ab788a26eb12229c3f5d8db969ef2d76a24`.
- TPS readbacks confirmed 1300 kHz, scale 0.125, compensation `13 20 C6 19 C6`, ON_OFF_CONFIG 0x1F. Temporary target remained **1160 mV per half**.
- Warm 300 MHz baseline qualified at **99.34%, 1487.72 GH/s**. Immediately before reset, A1/A2/A3/A4 temperatures were 55.17/54.02/55.01/55.01°C. The baseline qualification timer had finished by the `wait_defer` samples; their zero interval rates are not evidence of failed domains.
- Warm reset began at boot time **287.903 s**. Vcore and VDDIO stayed enabled. Four ASICs re-enumerated at 100 MHz at 288.550 s.
- Every 25 MHz PLL step from 100 through **550 MHz** reported lock on all four ASICs. No mining configuration, work target, version base or SPDLOG arm was logged after reset. `DEFER FIRST_WORK` and `DEFER LAUNCHED` were never reached.
- Last valid temperature set, after settling at 525 MHz, was A1 58.47°C / A2 57.15°C / A3 58.63°C / A4 58.80°C at 299.018 s.
- 550 MHz lock was logged at 299.046 s. At **300.073 s**, A3 returned **−288.90°C**. The retained sensor-validity check stopped the run. GPIO10 went low at 300.075 s; ASIC power-off completed at **300.089 s**.

No 575 or 600 MHz transition occurred. There are no post-reset per-domain PASS/FAIL measurements, no first job at 600 MHz, and no two-minute final observation. This is an incomplete work-timing comparison, not a failed 600 MHz qualification.

## Interpretation

The same invalid A3 temperature value occurred in the earlier immediate-work 600 MHz run. Here it occurs at 550 MHz **before any new work is sent after reset**, so the invalid sensor value is not specific to immediate work reapplication. The raw temperature code was not logged; the cause remains unresolved. It is not a physical −288.9°C temperature or an observed overtemperature.

Valid temperature reports increased during the no-work ramp. With no per-domain counter or usable scope record after reset, this does not establish stopped hash clocks, retained work, healthy domains or a particular current load. “No work sent” describes the firmware sequence; it is not proof of electrically idle ASICs. A reset with Vcore on is also different from the user's historical ESP reboot that cycled the ASIC rail.

## Scope acquisition failure

CH1 stayed **50 Ω**, CH2 1 MΩ, both DC/1×/20 MHz bandwidth. The supervisor set 2 s/div, 700,000 points and 20 kSa/s for a planned 35-second capture. G/T handshake completed and the scope trigger command was sent.

The supervisor timed out waiting for the long acquisition to report STOP. Its finally block stopped the scope. Readback afterward showed **zero acquired points and zero voltage increment on both channels** (`final-scope-state.json`), so no valid waveform was recoverable. There are no measured transient peaks or verified electrical reset/ramp traces for this run. The precise reason for the acquisition failure was not established.

Before flashing, the host-only supervisor was adjusted to allow up to 20 seconds for arming and to preserve a triggered capture after a firmware stop. The original prepared script is retained as `capture_deferred-prepared.py`. Offline audit checks passed, but the actual long-acquisition workflow still requires correction before reuse. No additional miner reset or test was performed after the sensor stop.

## Final state and artifacts

ASIC power is **OFF**, TPS telemetry **44 mV**, fan **70% / 2727 RPM**, scope STOP, CH1 **50 Ω**. Saved NVS settings remain 600 MHz / 1160 mV. The **deferred-work test image is installed and reruns on reset**. Normal source bench flags remain disabled; normal firmware was not flashed.

- `flash.log`, `serial.log`, `capture.log`: actual run.
- `evidence.txt`, `sequence-events.json`, `sequence-integrity-check.txt`: serial sequence verification.
- `initial-api-state.json`, `final-api-state.json`, `scope-settings.json`, `final-scope-state.json`: measured setup and final state.
- `domain-samples.csv/json`, `phase-summary.json`, `warmup-timeline.png/pdf`: warmup-only telemetry and counters.
- `README-preparation.md`: original build-time procedure, tests and limitations, retained as preparation history; its unflashed status is superseded by this result.
- `probe.bin`, source snapshots, build/test logs and `image-info.txt`: exact prepared artifact.
- `sha256-prepared.json`: original preparation manifest; `sha256.json`: current artifact manifest.

The next attempt must address sensor-read validity and scope triggering/acquisition. This run cannot answer whether delayed first work restores 600 MHz throughput.
