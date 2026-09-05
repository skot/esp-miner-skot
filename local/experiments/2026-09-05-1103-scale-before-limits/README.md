# Stock 1103: warm power cycle reaches 600 MHz / 2.965 TH/s

Date: 2026-09-05. Stock board (original TPS output capacitor, no added half-stack capacitors), board version 1103, MAC 28:84:85:BA:96:08, USB /dev/cu.usbmodem11434201, IP 192.168.30.9. User confirmed this board has mined previously and authorized flashing and log-only testing without scope probes. No scope was used.

## Result

Completed 120.325 seconds of observation after the first 600 MHz job, with no further job, PLL change or counter rearm. Per-domain counter analysis measured **2964.58 GH/s** (98.978% ideal); final 30 seconds **2964.76 GH/s**. This is a synthetic-work ASIC throughput benchmark, not a pool accepted-share test.

| ASIC | GH/s | D0 ideal % | D1 ideal % | D2 ideal % | D3 ideal % |
|---|---:|---:|---:|---:|---:|
| A1 | 737.81 | 99.129 | 99.258 | 96.438 | 99.303 |
| A2 | 741.14 | 98.931 | 99.143 | 98.945 | 98.890 |
| A3 | 742.61 | 99.202 | 98.961 | 99.451 | 99.080 |
| A4 | 743.03 | 99.131 | 99.442 | 99.111 | 99.230 |

A1 D2 retained one zero-PASS core throughout the observed 600 MHz hold, also present during 300 MHz warmup. It averaged 96.438% ideal, versus 98.890–99.451% for the other 15 domains. No whole-domain collapse occurred. `healthy_all=0` accurately records the remaining deficit; `result=COMPLETE` records completion of the report-only experiment, not an all-domain qualification pass.

All four temperature streams changed normally, with zero invalid readings over the full run. Maximum at 600 MHz: 60.12°C; final A1/A2/A3/A4: 59.95/58.63/59.46/59.13°C. Typical loaded PCB halves were 1154–1157 / 1166–1169 mV. Sampled whole-run maxima were 1184/1180 mV, total2325mV. These are asynchronous software samples with existing calibration offsets, not measurements of fast transient peaks. No scope result is inferred.

## Sequence and TPS regression check

Stock TPS profile: 550 kHz, VOUT_SCALE_LOOP0.25, compensation13 11 8C 11 86. Normal GPIO10 enable path restored, ON_OFF_CONFIG0x1F. The probe deliberately seeded scale0.125 while OPERATION was OFF, then programmed/read back0.25 **before** voltage commands and limits. First boot produced1606mV for1600mV requested and enumerated all four ASICs. This validates the new order against the deliberately seeded former scale state; previous first-boot images with scale written later had stopped at1208mV.

At300MHz, warm to about55°C; warm qualification1485.83GH/s (99.21%), PCB1155/1168mV. Disable Vcore and VDDIO while holding reset; fresh TPS reading36mV confirms discharge. Re-enable at800mV/half, reinitialize4ASICs at100MHz, voltage-ramp to1160mV/half. Broadcast historical PLL tuples in25MHz steps to600MHz without work or thermal conversions; final1500ms settle. Fresh temperatures before the first job:57.48/56.16/56.98/56.98°C. Then one synthetic job and120s observation.

During only the bounded full supply cycle, fresh TPS and raw midpoint ADC reads remain mandatory but derived half-stack calculations are suppressed, because sequential readings span a changing rail. Normal midpoint calculations resume after supply settling and1500–1700mV total verification. Discharge must still reach<=100mV; TPS/ADC errors still stop. Earlier identical-reboot run stopped at682mV due to the sequential measurement artifact; see ../2026-09-05-1103-stock-repeat-boot/README.md.

## Saved state and verification

The probe automatically powered the ASICs off; subsequent API total36mV and power0.0056W confirm discharge. API `actualFrequency=600` is the last programmed frequency; it does not mean the powered-off ASICs are hashing. Saved settings remain450MHz/1160mV, fan minimum99%, auto enabled. Bench fan settings were temporary. The saved probe remains installed and repeats the diagnostic after an ESP reboot; it does not hand over to normal pool mining.

Probe SHA256: `fe782f3d7e70980c91719aeffeabaf8322779cd61155a8667627a5bc00295313`. Application partition only was flashed and verified. NVS/OTA/web assets were retained. The preexisting AxeOS/application version warning is expected for this application-only diagnostic flash.

`probe.bin`, `probe.elf`, source snapshots, SHA256 manifest, build/flash logs, raw serial and counter/thermal analyses are preserved here. The earlier never-flashed candidate is in before-transition-guard/. The normal image was built after restoring all bench and test-only scale-seed flags to0; it was not flashed. GPIO bypass remains0 and stock compensation remains the default. Focused tests passed for voltage-scale order/error handling, fresh PMBus reads, board selection/ADC failures, no-work power-cycle sequencing, and no-scope hold/manual stop behavior. Firmware builds and git diff whitespace check passed.

The stock capacitor configuration is sufficient for this demonstrated near3TH/s startup. The result does not establish repeated cold-start reliability, eliminate physical transients, or identify the cause of the single A1D2 zero-PASS core. Next useful work: reproduce this sequence across starts, then integrate a startup mode that hands off to pool mining.
