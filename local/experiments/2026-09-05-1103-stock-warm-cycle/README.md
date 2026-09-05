# Stock 1103x: prepared warm power-cycle comparison

2026-09-05. **Built and tested on the host; not flashed or tested on hardware.** No connected device or scope was operated during preparation. This carries the successful 1102 warm-start experiment to a stock 1103 board, with its original TPS output filter and without the added 100 µF PCB half-stack capacitors. Warmup is promising, but startup reliability on stock 1103 is not established.

## Regulator profiles

| Board / output filter | Switching | VOUT_SCALE_LOOP | COMPENSATION_CONFIG |
|---|---:|---:|---|
| 1102 | 1300 kHz | 0.125 | `13 20 C6 19 C6` |
| **Stock 1103** | **550 kHz** | **0.25** | **`13 11 8C 11 86`** |
| Modified 1103, 4 × 100 µF ceramic output | 550 kHz | 0.25 | `13 11 8C 19 86` |

The stock values are restored from the pre-ceramic source (`be978c3:main/power/vcore.c`) and the recorded pre-modification investigation. The ceramic experiment README records the user's byte-3 change from 0x11 to 0x19. These are previously recorded configurations, not a new independent loop-stability calculation for every hardware variant.

`main/power/vcore.c` now defaults `PROTO_1103_CERAMIC_OUTPUT` to 0. Set it to 1 (or override with a compiler definition) only when building for the modified ceramic-output board. The 1102 override remains 1300 kHz with its own compensation regardless of this selector. The saved stock probe keeps 31.75 ms soft start and active-high GPIO10 enable with ON_OFF_CONFIG 0x1F. The 1103 midpoint monitor stays enabled.

## Reused successful sequence

1. Start at 100 MHz, raise Vcore to 1160 mV per half, qualify through 300 MHz.
2. Hash at 300 MHz with temporary low fan PWM. Require hottest >=54.5°C and <=60°C, coldest >=52°C, for 10 seconds; then measure the warm baseline.
3. Host pre-fills/arms the scope and releases the firmware handshake.
4. Hold ASIC reset, disable both Vcore and auxiliary VDDIO supply, command TPS off, wait >=250 ms and verify discharge <=100 mV within the bounded wait.
5. Establish Vcore at 800 mV per half before enabling the auxiliary supply. Reset and re-enumerate all four ASICs at 100 MHz.
6. Raise Vcore 810→1160 mV per half in 10 mV / 150 ms steps. Then broadcast historical PLL settings in 25 MHz steps from 125→600 MHz, without work or thermal conversions during this restart ramp.
7. Wait 1.5 seconds at 600 MHz, require four fresh valid temperatures, send the first synthetic job and observe all 16 domains for 120 seconds. Shut ASIC power off at completion.

The MC3 source is byte-identical to the successful 1102 probe: same historical PLL table, same broadcast CFG/EN burst and same temperature reader. Voltage and throughput thresholds remain report-only for the comparison. Temperature, fresh rail/ADC communication checks, bounded waits, manual stop and scope clipping/coverage checks remain active. This is an instrumented bench image requiring a host handshake, not automatic pool-mining startup firmware.

## Necessary 1103 adaptations

- Report-only mode accepts only the explicitly selected supported board; 1103 requires midpoint sensing. Powered midpoint read/calculation failures still stop the test. Normal builds retain their existing voltage-limit checks.
- At intentional supply-off, fixed calibration offsets cannot represent meaningful powered half-stack voltages. The guard skips midpoint calculation only while the bounded power-cycle flag is set and a fresh TPS measurement is 0–100 mV. Above that range, normal ADC checks resume. A failed fresh TPS read always stops the bench.
- The older `TPS546_get_vout` accessor returns the previous voltage when PMBus reads fail. A new error-returning `TPS546_read_vout` accessor and `VCORE_get_regulator_voltage_mv_fresh` avoid treating stale telemetry as discharge confirmation. Ordinary cached display telemetry retains its previous behavior. The earlier successful 1102 waveform independently confirmed its real rail discharge; its original firmware artifact remains unchanged.

## Before the hardware run

Identify the connected stock 1103 board and serial port; do not assume the previously connected 1102 or modified 1103 is the target. Confirm CH1 is physically across this board's TPS output capacitors and CH2 is GND0−GND. CH1 must remain **50 Ω**, CH2 1 MΩ, both DC/1×/20 MHz bandwidth. The copied supervisor enforces those settings and preserves the successful 5 s/div, 700k-point, 16-second-prefill capture.

Read the target's board identity and saved settings, back up its existing application, and verify TPS boot readbacks: 550 kHz, scale 0.25, compensation `13 11 8C 11 86`, ON_OFF_CONFIG 0x1F. The 1103 board entry still contains +21 mV midpoint / +13 mV total calibration offsets from the previous board; verify them against measurements on the stock target before interpreting small balance differences. Do not transfer prior calibration accuracy claims to another physical board.

Saved test firmware: `probe.bin`, SHA256 **542d0fcbbe0e7d0d8dab811117b5b5a471e8ac66bd8cdcfab1800b488620b49d**. Flash only the application partition after confirming the target. `capture_deferred.py` requires `BITAXE_BENCH_PORT` explicitly. Scope voltage peaks remain descriptive at this timebase: expected 100 µs samples cannot resolve 20 µs averages or rule out faster peaks.

## Validation and artifacts

Both probe and normal ESP-IDF builds succeeded. Focused host tests passed for all three regulator profiles; matching/mismatched board selection; ADC faults and voltage boundaries; bounded discharge handling; fresh PMBus errors, invalid numeric data and cached-telemetry compatibility; full reset/voltage/PLL/work ordering and failure cases; thermal retry; and scope clipping/coverage auditing. `git diff --check` passed.

`probe.bin` / `probe.elf` and source snapshots preserve the stock test build. `normal.bin` and the working source have all bench-enable flags restored to zero; normal.bin does not invoke this warm-cycle experiment. `manifest.json` records hashes and settings. Diffs against the successful 1102 probe are saved separately. No result.json or live serial/scope capture is present because this stock-board run has not happened yet.
