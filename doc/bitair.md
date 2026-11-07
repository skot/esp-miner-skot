# Heatbit Bitair V1.22

The `bitair` board profile supports the production V1.22 mainboard: one BM1370, TPS546D24A core regulator, EMC2101 temperature/fan controller, no display, and the purifier's incoming fan-mode PWM. V1.00/V1.11 prototypes use different fan wiring and are not supported by this profile. Initial hardware bring-up and mining results are recorded below.

The profile recognizes the stock `main/boardversion=bitair` NVS setting even when `devicemodel=gamma`. It disables BAP so its default UART cannot drive the purifier connector. It uses the existing BM1370 driver and Gamma regulator/temperature calibration. No schematic or PCB changes are required.

## Hardware mapping

These connections were checked against a KiCad XML netlist exported from `bitair-mainboard/bitair-mainboard.kicad_sch` in the adjacent hardware repository.

| Function | Connection | Firmware behavior |
| --- | --- | --- |
| ASIC UART | TX17, RX18 | Existing BM1370 driver |
| ASIC reset | GPIO1 | Existing reset control |
| ASIC core voltage ADC | GPIO2 | Existing core voltage measurement |
| I2C | SDA47, SCL48 | EMC2101 at 0x4c and TPS546 at 0x24 |
| Fan PWM output | EMC2101 FAN pin 4 → CN1 pin 10 | Existing fan driver |
| Purifier mode input | GPIO40 → Q3 → CN1 pin 8 | Input only, edge capture |
| Fan tach | EMC2101 ALERT/TACH pin 6 is unconnected | RPM reports 0; RPM-only self-test is skipped |
| CN1 B | GPIO39, CN1 pin 9 | Reserved; stock firmware uses a 9600-baud sensor UART |
| Front LED | GPIO41, CN1 pin 6 | Stock 5 kHz, 10-bit PWM indicator |
| External input voltage | GPIO6, R94/R95 51k/12k divider | Reserved; not sampled by this port |
| BOOT button | GPIO0 | Existing input handling |

The mainboard receives approximately 15 V on CN1, then converts it to 5 V before the TPS546. Keep the Gamma **5 V** regulator configuration: the 15 V enclosure supply is not the TPS546 input. AxeOS input-voltage telemetry therefore describes the internal 5 V rail. Reported power is the regulator output plus the inherited 5 W estimate, not a measured wall-power reading; the Bitair profile advertises a 25 W power range.

GPIO6/39/40/41 are reserved against I2C overrides, and BAP overrides cannot enable a UART on this board. Leave ASIC reset, I2C, and BOOT overrides at their defaults.

## Physical fan controls

The stock firmware measures the high-time duty cycle on GPIO40. The port uses the same mode boundaries. REA decompilation additionally recovered a 0.75 gain in the stock EMC2101 output routine, so the physical fan floors below refer to actual requested PWM duty:

| Input duty | Mode | Fan request | Mining |
| --- | --- | --- | --- |
| Below 25% | Off | 30% for 30 seconds, then 0% | Paused |
| 25% through 65% | Silent | At least 37.5% | Allowed |
| Above 65% through 85% | Auto | At least 56.25% | Allowed |
| Above 85% | Turbo | At least 75% | Allowed |

Thermal control or the AxeOS manual setting can request more airflow than the physical mode. This is an intentional safety difference from the stock fixed-speed override. Overheat mode always requests 100%. The EMC2101 spin-up register is explicitly initialized to zero, matching stock. Physical off adds its own pause reason: switching the purifier back on does not clear a pause made in AxeOS, a hardware fault, or a pool outage. The fan may continue running during the off-mode cooldown.

On the tested unit, holding the lock touch icon for approximately five seconds extinguished the lock indicator and restored the touch controls. Selecting one fan dot also darkened the button lights; the next touch relit them without changing speed. Treat that first touch as a panel wake action and confirm an actual mode transition before interpreting the dots.

At startup or with an invalid/noisy PWM signal, the port requests 100% airflow and pauses mining. It accepts a waveform after three valid cycles, supports periods from 8 microseconds to 100 milliseconds, and rejects pulses shorter than 2 microseconds. A 16-cycle exponential average and 200-millisecond mode settling interval suppress timing jitter under mining/Wi-Fi load. Invalid signals still invoke the safe state immediately. PWM measurements expire after 250 milliseconds; a level stable for 500 milliseconds becomes off (LOW) or turbo (HIGH). An established valid PWM signal transitioning to constant HIGH starts the turbo settling interval after 250 milliseconds, avoiding a spurious unknown/pause interval during the transition to 100%. The hardware has a pull-up, so an unplugged input that stays HIGH cannot be distinguished from a deliberate 100% command.

The AHT20, the sensor protocol on GPIO39, and Heatbit cloud/app services are not implemented. They are not needed to decode the physical fan-mode PWM. The board provides no fan tach signal, so actual airflow must be checked during hardware bring-up.

## Front status LED

The front LED uses GPIO41 through CN1 pin 6. The dedicated indicator task uses the stock low-speed LEDC timer/channel 0, 5 kHz carrier, non-inverted output and 10-bit resolution. It starts before Wi-Fi setup and runs independently of mining/fan tasks, including while ASIC startup is waiting or has failed. LED initialization errors are logged without preventing mining.

| State | LED behavior |
| --- | --- |
| Starting or disconnected from Wi-Fi | Breathing between 5% and 100%, in 3-point steps every 20 ms |
| Connected, physical off/unknown or AxeOS pause | Off |
| Connected, Silent | Duty 511/1023 (about 50%) |
| Connected, Auto | Duty 767/1023 (about 75%) |
| Connected, Turbo | Duty 1023/1023 (100%) |
| Latched hardware fault | Three 100 ms flashes every 2 seconds |

Steady brightness represents the physical purifier mode, not the fan controller's thermal/manual PWM demand or pool connectivity. Connectivity follows Wi-Fi association, as in stock, and is refreshed every 100 ms; it does not wait for DHCP or a pool connection. Disconnection breathing takes precedence over the normal off/mode indication, matching stock. A latched hardware fault takes precedence over both. The fault indication reuses stock's failed-diagnostic three-flash pattern; applying it to ESP-Miner hardware faults is a port adaptation. Heatbit BLE/cloud indication commands and factory diagnostics remain absent. The [REA audit](bitair-rea-audit.md) records the stock GPIO configuration and patterns. On November 7, 2026, an app-only flash verified all three visible brightness levels and the front LED turning off and back on with the purifier. During the same cycle, ASIC core voltage fell to about 21 mV, returned to 1144 mV, and mining resumed with another accepted share.

## Build and configure

Use ESP-IDF **v6.0.3**, Node.js 22 or newer, and the initialized repository submodules. The standard `idf.py build` embeds AxeOS. Run the focused host tests with:

```sh
python3 -m unittest discover -s tools/tests
```

For an app-only update on the verified stock partition layout, flash the supported ESP-Miner application at 0x10000:

```sh
python -m esptool --chip esp32s3 --port /dev/cu.usbmodem11434101 \
  --baud 460800 write-flash 0x10000 build/esp-miner.bin
```

This updates the factory application slot and preserves the existing bootloader, partition table, NVS settings, and OTA selection data. The port keeps ASIC core power off while waiting for Wi-Fi and an enabled physical fan mode; it restores core voltage immediately before initial ASIC discovery. Failed voltage restoration or ASIC discovery commands core power off, holds ASIC reset, and latches a fault requiring restart. Power-management startup waiting ends on either success or failure; failed recovery cannot silently resume or re-enable core voltage.

Only for a fresh installation that should replace settings, generate the Bitair NVS image and include it in the factory merge:

```sh
python "$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py" \
  generate configs/config-bitair.csv config.bin 0x6000
./merge_bin.sh -c esp-miner-factory-bitair.bin
```

The supplied CSV starts at 525 MHz / 1150 mV and uses `display=NONE`. Factory/BOOT self-test is disabled for Bitair: its diagnostic fan/power ownership conflicts with the physical-off interlock. Normal startup, Wi-Fi, and purifier controls remain active even if a retained setting requests self-test. Configure your own pool payout address and network through AxeOS. These are initial Gamma settings, not a hardware-validated Bitair performance tune. Factory release workflows include the `bitair` configuration.

For an installation that retains stock NVS, the existing `boardversion=bitair` selects the profile automatically and the firmware migrates legacy frequency/fan/pool keys. A factory image containing `config.bin` replaces those stored settings. Preserve the full original flash backup before installation. This port retains the stock partition offsets: NVS 0x9000, factory app 0x10000, OTA slots 0x710000/0xb10000, OTA data 0xf10000. Do not rely on Heatbit OTA or its app to install this firmware.

Before enabling sustained mining, verify that the purifier's off/silent/auto/turbo selections are recognized, off removes ASIC power, resuming does not undo an AxeOS pause, fan airflow changes correctly, the regulator sees about 5 V, core voltage matches its setting, one BM1370 is detected, chip temperature is plausible, and a pool accepts shares. Use the normal purifier heatsink, enclosure airflow, and system power supply. USB-C data alone does not power the board.

## Hardware bring-up, November 4, 2026

Only `build/esp-miner.bin` was flashed at 0x10000. Esptool verified its written hash. The stock bootloader, partition table, NVS, and OTA selection data were retained; Wi-Fi was configured through AxeOS during testing. The factory image was not flashed.

The device recognized the Bitair profile, initialized its TPS546D24A and EMC2101, discovered one BM1370, joined Wi-Fi, served AxeOS, generated nonces, and received a pool-accepted share. No rejected shares were observed during the initial test. With the retained 600 MHz / 1150 mV settings, one-minute hashrate was approximately 1.0–1.15 TH/s, chip temperature approximately 40–45°C, regulator temperature up to 53°C, and estimated power approximately 18–19 W. The internal input rail measured approximately 5.02 V; measured core voltage was approximately 1.144 V when enabled and 0.021 V when off.

The physical power button started airflow and enabled ASIC discovery/mining. An initial dot-to-mode inference was withdrawn when an illuminated lock icon and unresponsive controls were discovered. After unlocking with a five-second hold, explicit button-driven transitions confirmed three dots/turbo/100%, then one dot/silent/50%, then two dots/auto/75%. Both the user's observations and live mode/output telemetry agreed. Those percentages describe the November 4 build; the November 6 REA audit corrected physical preset duty to 37.5/56.25/75% to match the stock driver gain, with thermal/manual demand still allowed to increase it. At the subsequent checks, mining continued at approximately 1.1 TH/s with two pool-accepted shares and zero rejected. A combined physical-off/API-pause test remains a useful follow-up. The fan button is the fan icon beside three indicator dots on the top panel; a wake-only touch after the lights darken must not be counted as a speed change.

Bring-up exposed two issues that were corrected: core power could otherwise be enabled while waiting for Wi-Fi in physical off mode, and single-cycle PWM timing could cause brief mode misclassification under load. The updated firmware keeps core power off until initial startup is authorized and averages/debounces the input. The updated app ran without the earlier mode flickers during network monitoring, and AxeOS pause/resume removed/restored core power successfully. Pause includes the inherited frequency ramp-down; telemetry took about 20 seconds to show core power off, and resume took about 14 seconds to report a valid chip temperature again.

Opening the USB monitor reset the ESP32 through USB-Serial/JTAG during early testing. Later observations used AxeOS network telemetry/logs to avoid these induced resets. These results are initial functional tests, not a long-duration thermal or airflow qualification; fan RPM remains unavailable because the board has no tach connection.

## Stock firmware evidence

The read-only analysis used `firmware-backups/esp32s3-20261104-195544/flash-full-16MB.bin`, SHA-256 `ccbdc2e3fc181993df468671e1e89a294e57e72af66e314a659f9a1744e78be1`. Its factory app descriptor identifies `bitair-miner 0.0.30`, built September 23, 2026 with ESP-IDF v5.5.4. Both OTA slots are erased in that backup. Hardware-setting entries include `boardversion=bitair`, `devicemodel=gamma`, `asicmodel=BM1370`, and a 600 MHz frequency; historical voltage entries include 1250 and 1150 mV, so those entries alone are not a performance recommendation.

The following are static observations from the app's Xtensa code, not claims of execution on a device:

| Address / range | Observation |
| --- | --- |
| 0x4200e584–0x4200e603 | Configures GPIO40 as an input with interrupts on both edges and installs ISR 0x4200df18 |
| 0x3c121f00 | GPIO configuration constant: mask 1ULL&lt;&lt;40, input mode, no internal pulls, any-edge interrupt |
| 0x4200df18 | Reads GPIO40 and timestamps transitions to capture high-time and period |
| 0x4200e24f–0x4200e2b3 | Computes high-time / period and classifies the 25/65/85% boundaries |
| 0x420146f1–0x42014754 | Maps off/silent/auto/turbo to 0/50/75/100% fan output |
| 0x4200e349–0x4200e352, 0x4203141c, 0x420135f4 | Off updates heater enable; its downstream inverted flag is consistent with a mining pause |
| 0x42010914–0x4201097c | Configures UART2 at 9600 baud with RX on GPIO39, used by `tpm300a_service` |

The initial November 4 investigation used ESP image/partition parsing and Xtensa objdump. The November 6 follow-up used REA with Ghidra for disassembly and decompilation through a documented experimental Xtensa adapter. See [the REA audit](bitair-rea-audit.md) for artifact hashes, evidence IDs, corrections, intentional differences, coverage, and remaining checks. The original dump, credentials, and extracted app bytes are not included in this repository.
