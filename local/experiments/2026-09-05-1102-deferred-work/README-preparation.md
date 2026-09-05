# Prepared 1102 firmware: warm reset, no-work PLL ramp, delayed first job

2026-09-05. **Built and tested on the host; NOT FLASHED. No miner or scope commands were issued during preparation.**

The user requested preserving 1160 mV per half, warming the ASICs, resetting/reinitializing them while warm, completing the PLL ramp without sending work, waiting for settling, and only then sending the first job at 600 MHz.

## Prepared behavior

1. Use the existing 1102 regulator configuration: 1300 kHz, scale 0.125, compensation `13 20 C6 19 C6`, active-high GPIO10 enable and PMBus shutdown. Reach **1160 mV per half** through the existing initial voltage ramp.
2. Run the existing qualification-work warmup at **300 MHz**, with the same fan policy. Require hottest temperature 54.5–60°C and coolest at least 52°C continuously for ten seconds, with a ten-minute warmup timeout. Then qualify the warm 300 MHz baseline.
3. Wait for the host scope G/T handshake. Normal work producers are still held behind `ASIC_initalized=false`.
4. Pulse ASIC reset low for **100 ms**, then allow 100 ms after release. **Keep Vcore at 1160 mV per half and VDDIO on**; do not cycle the regulator or reboot the ESP32. Re-enumerate all four ASICs using `MC3_init`, which starts PLL0 at 100 MHz and configures the sensors without sending mining work.
5. Hold 100 MHz at least 500 ms. Ramp explicitly through **125, 150, …, 600 MHz** using the current broadcast PLL routine. Keep the current divider tuples and combined CFG/EN packet unchanged. Wait at least **500 ms through 500 MHz**, **1000 ms at 525–575 MHz**, and **1500 ms after 600 MHz lock**. Temperature checks add some time between steps.
6. No `WORK_CFG`, job-register writes, qualification work or SPDLOG arms are issued between reinitialization and the final settling delay. The existing PLL routine still writes `GLOBAL_SPD`, PLL registers and `ROLLTIME`; this is not a claim of clock gating or zero internal switching. PVT thermal conversion commands continue for monitoring.
7. Send the **same synthetic qualification job used in the recent bench tests**, now as the first job since warm reset at 600 MHz. Immediately arm one 180-second SPDLOG window. Keeping job content, divider values and packet format fixed avoids changing those alongside work timing.
8. Observe all 16 domains, PVT D2 voltages and temperatures for **120 seconds** without another PLL change, work reset or counter rearm. Log whole-window and final-ten-second throughput, then power off normally whether healthy or weak. This is a bounded diagnostic, not a persistent pool-mining image.

Voltage and throughput thresholds remain report-only for this authorized 1102 bench mode. Invalid or >=70°C temperature readings, missing telemetry, rail/communication faults, manual X and acquisition failure still stop the test. Hardware regulator protections remain configured. Failure to enumerate four ASICs or lock a requested frequency prevents the first job. This hardware has no midpoint ADC.

A warm ASIC reset with the regulator left on is **not the same as the historical ESP32 warm reboot**, which cycled the ASIC rail. This build tests the requested reset/no-work sequence while preserving 1160 mV. It does not claim to reproduce every aspect of the dirty older firmware. The exact old divider table was deliberately not restored.

## Files and identity

- **`probe.bin`**: prepared bench application, warm-probe/report-only/deferred-work flags enabled, first-zero hold disabled, target board 1102 and voltage 1160 mV.
- `asic-init-source.c`, `asic-reset-source.c`, `mc3-source.c`, `vcore-source.c`, `device-config-source.h`, `fan-controller-source.c`: source snapshots for the prepared image.
- `normal.bin` and `asic-init-normal-source.c`: normal build with all bench mode flags restored to zero; also not flashed.
- `build-probe.log`, `build-normal.log`, `image-info.txt`: build and image metadata.
- `test_deferred_work.py`, `test-deferred.log`: focused host tests of the actual deferred sequence helpers.
- `test_board_support.py`, `test-board.log`: actual board-selection, report-only, sensing and shutdown helper tests.
- `capture_deferred.py`, `scope_limits.py`: prepared host supervisor; not run against hardware.
- `analyze_warm.py`, `summarize_run.py`: counter and waveform analysis utilities.
- `sha256.json`: artifact hashes.

Working source has all bench modes disabled after building `probe.bin`. `build/esp-miner.bin` is the normal build; use the saved `probe.bin` only when this experiment is explicitly scheduled for flashing. The currently installed 1160 mV first-zero hold image is unchanged.

## Host protocol and future capture

`capture_deferred.py` requires `BITAXE_BENCH_PORT` to select the physical board explicitly. It is intended to attach immediately after a separately authorized flash/reset. It never flashes or resets the ESP itself. Do not run it yet merely to inspect it: running the supervisor changes scope settings and sends G/T/R to a compatible running probe.

Firmware emits `DEFER READY` after warmup; the host arms the scope and sends G. Firmware emits `DEFER ARMED`; the host forces the trigger and sends T. Firmware logs reset, each PLL lock, final settling, `DEFER FIRST_WORK`, then `DEFER LAUNCHED`. The host retrieves the long capture and sends R only if acquisition and event coverage pass. X aborts. R is required within 60 seconds of first-work launch; the final observation deadline stays anchored to launch rather than scope download.

The supervisor preserves **CH1 at 50 Ω**, CH2 at 1 MΩ, DC coupling, 1× and 20 MHz bandwidth. It uses CH1 0.2 V/div, CH2 0.5 V/div, a 2 s/div timebase and 700,000 samples/channel. It verifies actual acquisition metadata and that the record includes launch completion. It rejects clipping or missing coverage. Voltage limits are reported only.

This longer record necessarily has much lower sample rate than the previous 2 MSa/s transition captures. It establishes the broad sequence and rail motion; it may miss fast peaks and cannot support a 20 µs mean when the returned sampling interval is longer than 20 µs. Audit metadata explicitly records the effective averaging window and whether 20 µs is resolved. Separate high-resolution captures would be needed to compare fast transient peaks quantitatively.

## Validation and limits

Both firmware builds succeeded. The focused C harness compiles actual helper bodies and verifies reset precedes reinitialization, all twenty explicit 125–600 MHz steps occur, no job/arm happens during ramp, each settling minimum is honored, and exactly one job plus one counter arm occurs after the final >=1500 ms delay. Injected PLL failure, invalid temperature, manual stop during final settling, wrong chip count, active normal work producers and failed job setup all prevent a subsequent counter arm or earlier first-work launch as appropriate. Existing board-policy tests pass. Python scripts pass syntax compilation. The offline long-capture audit tests pass for coverage, clipping, report-only voltage handling and explicit unresolved-20-µs metadata.

These checks validate software sequencing, not real silicon behavior. The firmware and scope supervisor have not been exercised on the physical board. No conclusion about recovery or 600 MHz throughput is available yet.
