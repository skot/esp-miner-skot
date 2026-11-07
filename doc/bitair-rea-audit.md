# Bitair stock firmware audit — November 6, 2026

REA/Ghidra confirmed the production Bitair's Gamma/BM1370 configuration and physical PWM mode protocol. The audit corrected the port's fan scaling and EMC2101 spin-up setup, and a source review corrected startup/recovery failure handling and concurrent mode publication. This audit began offline. The November 7 device check is recorded in the [Bitair hardware notes](bitair.md#front-status-led).

## Artifact and reproducibility

The source is `firmware-backups/esp32s3-20261104-195544/flash-full-16MB.bin`, SHA-256 `ccbdc2e3fc181993df468671e1e89a294e57e72af66e314a659f9a1744e78be1`. Its factory app at `0x10000` identifies `bitair-miner 0.0.30`, IDF 5.5.4. Every initialized byte of its six app segments was copied unchanged into a synthetic **ELF32/Xtensa** container, SHA-256 `a6dc4b9bf39ed3fcfc1bddfbb9c3321e5fb39d1f34028c56001e729ab482865c`, entry `0x4037569c`. ELF headers, section names, analyst function names and the BSS end are analysis metadata, not recovered original metadata. ROM implementation bytes and purifier-panel MCU firmware are absent.

Local artifacts are in `../analysis/stock-bitair-20261106/` beside this repository: `prepare_elf.py`, `elf-provenance.json`, `rea-adapter-provenance.json`, `xtensa-processor-provenance.json`, `evidence-original-processor.json`, `evidence-bundle.json`, `evidence-supplemental.json`, `hardware-control-instructions.json`, `stock-app.decompiled.c`, `stock-app-linear-disassembly.txt`, `decompilation-coverage.json`, `nvs-hardware-audit.json`, and `board-preset-comparison.json`. The original backup and bulk decompilation are intentionally outside Git; the backup contains private settings.

REA 6.4.0 does not officially admit Xtensa. An isolated copy was extended to admit ELF machine 94 and explicitly select the installed Ghidra 12.1.4 `Xtensa:LE:32:default` processor. Its SLEIGH implementation lacked SALT/SALTU: two constructors were added in an isolated Ghidra copy, using signed/unsigned comparisons producing 0/1. Espressif's assembler confirmed encodings `722340` and `622340` for `salt/saltu a2,a3,a4`. The [Cadence ISA summary](https://www.cadence.com/content/dam/cadence-www/global/en_US/documents/tools/silicon-solutions/compute-ip/isa-summary.pdf) and [Ghidra issue 9095](https://github.com/NationalSecurityAgency/ghidra/issues/9095) describe the instruction semantics/missing coverage. The compiled processor SHA-256 `e249cfe06ed821e276bbd0a6083576ac32ac7c0471de77723af68cb7ce1f2c25` is committed into the experimental REA analysis profile. Installed REA/Ghidra files were not changed.

The bulk pass attempted all **6,785 discovered functions**: 6,784 returned pseudocode; `0x4201ceb4` is an explicit failure after its native decompiler process was terminated for excessive runtime. The focused application/driver batch returned all 232 functions. A supplemental pass recovered three additional instruction-verified entry points missed by automatic discovery (`0x4200d7a8`, `0x4200fcc4`, `0x4202ef90`): OTA setup, status reporting and state/cloud publication. Their pseudocode is appended to the combined C export; the supplemental seed profile and evidence are retained separately. Discovery is heuristic and successful return does not establish correct pseudocode or complete code coverage. Listings retain warnings, unknown types, inferred boundaries and incomplete ROM call prototypes. The supplementary linear GNU disassembly includes data/literal pools and can lose instruction alignment; use function-specific REA listings for conclusions.

## Findings and decisions

| Question | Observed stock evidence | Port decision |
| --- | --- | --- |
| Board/ASIC/calibration | Bitair preset at `0x3c122660` matches Gamma 600/601 apart from its name pointer; BM1370, EMC2101 ideality `0x24`, beta `0`, temperature offset `0`. Parsed active NVS has `boardversion=bitair`, `devicemodel=gamma`, 600 MHz, 1150 mV and no calibration/TPS overrides. 1250 mV is erased history. | Existing ASIC, I2C and thermal calibration retained. Fresh-install 525 MHz is a conservative port default; retained stock settings remain 600 MHz. |
| Physical mode input | `0x4200df18/0x4200e584`: GPIO40, both edges. `0x4200dfe0`: high-time/period, thresholds `<25`, `<=65`, `<=85`, then turbo; stale/static timing includes 250/350/500 ms. Startup HIGH before PWM becomes turbo after 500 ms. | Mode boundaries and polarity confirmed. Three-cycle validation, EMA and 200 ms settling are intentional port filtering, not an exact clone of stock timing. Unknown pauses mining and requests full cooling. |
| Actual fan drive | Stock `0x420146a0` requests 0/50/75/100 logical percent. `0x42017888` multiplies normalized demand by **63 and 0.75**, then truncates before register `0x4c`. | Corrected physical floors to 0/37.5/56.25/75% actual duty, yielding register values 0/23/35/47. Thermal/manual demand may exceed those floors; 100% emergency cooling still writes 63. |
| EMC2101 initialization | `0x4201781c` writes `03=04`, `4a=23`, **`4b=00`**. | Added Bitair-only spin-up write `4b=00`, with error propagation. Other boards retain their initialization. |
| Mining power interlock | GPIO40 off feeds `0x4203141c`; `0x420135f4` stores the inverted enabled flag as the mining pause reason. PM loop `0x420136f8` checks pause/fault/pool state. | Physical off remains an independent pause reason; it cannot clear an AxeOS pause, fault or pool outage. Off has a cooling interval rather than stock's immediate zero fan request. |
| Regulator | `0x420158a4/0x42014d10`: address `0x24`, Gamma family 4, VIN on/off 4.8/4.5 V, OV 6.5 V, scale 0.25, VOUT min/max/start 1/2/1.2 V, single phase, sync `0x10`, 650 kHz. Stock Gamma current warning/fault is **35/40 A**. | Existing 5 V configuration matches. Retain the port's more conservative **25/30 A** protection. The external 15 V enclosure input is not TPS546 VIN. |
| Temperature protection | PM loop uses EMC ASIC temperatures (75°C) and regulator temperature (105°C), minimum 30 seconds cooling and 95°C VR recovery threshold. | Existing thermal policy retained. Environmental sensor data is not used by the decoded fan controller or PM protection loop. No claim of full safety equivalence. |
| GPIO39 / panel indicator | `0x42010914/0x420105f8`: UART2 RX39, 9600 baud, parses sensor frames and copies telemetry through `0x420310c4`. `0x420102a4/0x420104c0`: indicator state drives LED PWM; this is output, not touch-button parsing. | Keep GPIO39/41 reserved against overrides. The front indicator is now ported; sensor and Heatbit services remain unported. Their decoded paths do not replace the GPIO40 mode input. External panel lock and wake behavior remain handled by the panel. |
| GPIO6 / 15 V telemetry | Netlist has the divider; bounded stock ADC call-site inspection found only ADC1 channel 1 (GPIO2/core sensing) at `0x42015ca4`, not channel 5/GPIO6. | Keep GPIO6 reserved; do not invent an unobserved stock voltage interlock. AxeOS continues to report the internal 5 V regulator rail. |

The board pin assignments remain supported by the existing exported production netlist; this audit made no CAD changes. AHT20 ambient telemetry, sensor availability and Heatbit cloud behavior are not ported. Static sensor producer/consumer checks are bounded and do not prove absence of every indirect dependency.

## Corrected source defects

The PWM classification and publication now share one critical section. Previously a reader could classify an old capture, another reader/ISR could invalidate it, and the first reader could overwrite the safe UNKNOWN mode with its stale selection.

Bitair now has an explicit cold-boot-pending flag. Failed initial or recovery ASIC discovery and failed core-voltage restoration command core power off, assert reset and latch a restart-required fault. Recovery callers check the result; failed thermal recovery cannot fall through to the normal voltage-setting path. Shutdown I/O failures are logged; software cannot guarantee physical power removal after an unsuccessful regulator write.

Factory/BOOT self-test is disabled for Bitair with a log. Its separate fan/power ownership conflicts with the physical-off interlock. This is an explicit Bitair policy restriction, not evidence that all diagnostics failed on the old firmware. Other board families retain their existing diagnostic behavior.

## Evidence index

Evidence IDs below refer to the saved local REA bundles. Addresses and names inferred by the analyst remain separate from original artifact authority.

| Scope | Evidence ID |
| --- | --- |
| Complete recovered PWM task | `ev_b0a4be8a857f0f0bc881f5f3ba46a168b2b05c5ec2f5161be7d4524cf4d776b2` |
| PWM instruction listing | `ev_85557ff1778f3f508d40daf8cbf9c10e1d6756f8ddbad3be0c2edefd20816810` |
| Stock fan gain and ROM arithmetic call listing | `ev_da359c3b37f312d802b30733ecdee74d177aeff6b6f84f41b9e48508773ca713` (original-processor bundle), `ev_1574ef3092defc2f494467dc1a8b3755c77eeb9f979dd27f23480fb8b1083923` |
| EMC2101 init | `ev_ca932290f1fa831a9ecbb477e8bb58deab13682d2cb727f1fe293f55b30ce3e6` (original-processor bundle), `ev_fff5a29fd3a115d0be2aff9d949f36df89a494443b90321bcbcff80b0c6067a3` |
| Regulator config, GPIO input init, sensor producer, state updates | `ev_12372076b8f9df60d9b0c1cbc0f6d11ea82699a73d959e0cc30f9a455dc7f4dd` |
| Regulator writes and indicator task | `ev_ffdb7f778ef32b7250bc9e0415d7ecf3ebcaffe7072113dc7185a785320ca4ff` |
| Fan-mode mapping and inverted mining pause | `ev_4bb8c2b67439177a07d2c0075212ba2c2297cf0e3b271384ccfae5169bd6dedc` |
| Application/drivers including thermal PM loop | `ev_637cdbce537c2dd75f5ac266f40440786fb743a98082c9138322b2cf855b6c50` |
| ADC config call sites | `ev_8278e4596561f4e1c2c5f3bd7c1dd72309425c31464398dc961a2895a2364b3d` |
| Supplemental entry recovery | `ev_7dba50e7ee17d2665e0afcfdb62c838f4aa00fa1aa80ec595aa898da6a5e23e5` (`evidence-supplemental.json`) |
| Discovery/bulk coverage | `ev_261226c074bfdb0316b68bf12887748c8681c7beb2e4a0b127720667003b563c`; all 17 batch IDs and failed-function record in `decompilation-coverage.json` |

## Front indicator follow-up

Stock initialization `0x420103d8` configures GPIO41 and LEDC low-speed timer/channel 0. The six timer words at `0x3c121f18` are `0,10,0,5000,0,0`: 10-bit, 5 kHz, automatic clock. Modes dispatched by `0x42031234` select off, Silent (`0x1ff`), Auto (`0x2ff`) or Turbo (`0x3ff`). Disconnected Wi-Fi selects breathing. `0x42010108` ramps from 5 to 100 percent in 3-point steps, bounded to a 20 ms update period; `0x420102a4` owns the worker. `0x4203010c` repeats a three-flash diagnostic-failure pattern every two seconds via `0x42010594` (100 ms on/off). Evidence: `ev_819c21d0da342caa9a31a96f68dbd73562fa4fcb15ec657d835d68902dce8a93` and `ev_d99fb596f3aea5fd7b932172cca8244f79bff5968e38006babfa83416b6aaed8`, retained in the bulk bundle.

`bitair_indicator.c` reproduces those electrical settings, steady duties and breathing/flash patterns. It starts before Wi-Fi and operates independently of mining startup. Physical off/unknown and AxeOS pause extinguish the connected-state LED. ESP-Miner hardware faults use the stock failed-diagnostic flash pattern; this trigger is an intentional adaptation, with priority over the normal Wi-Fi/mode indication. Heatbit BLE/cloud indication commands and factory self-test are not added. The host test checks the production LEDC initialization, non-Bitair no-op, driver/task failures, exact duties, ramp endpoints/reset/timing, triple flashes and state transitions. The November 7 device check confirmed all three brightness levels and the physical off/on transition.

## Verification and remaining checks

`python3 -m unittest discover -s tools/tests` passes all four host tests. These compile production C for capture, GPIO reservation, the front indicator and power-management behavior. Cases include mode boundaries, noise/stale/static inputs, concurrent invalidation, actual EMC2101 fan register values, pending startup, failed discovery, failed voltage restore, API/physical pause composition and failed thermal recovery. ESP-IDF 6.0.3 builds the updated app successfully; `git diff --check` passes.

The November 4 device results in [bitair.md](bitair.md) apply to that earlier build. The November 7 app-only test confirmed the three physical modes, front LED brightness and off/resume, including an accepted share after resume. Combined AxeOS and physical pauses, hardware fault injection, and sustained thermal testing remain open. No tach signal is available, and the panel timer/lock behavior has not been exhaustively tested. These limits prevent claiming that every behavior or safety property is verified.
