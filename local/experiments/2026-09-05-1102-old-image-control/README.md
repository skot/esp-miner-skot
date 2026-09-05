# Exact older firmware control: warm ESP reboot restores 600 MHz / 2.969 TH/s

2026-09-05. **The user's successful behavior was reproduced on the connected 1102 hardware.** Restoring the exact July 24 application and rebooting the ESP while the ASICs were warm produced near-full throughput on all four ASICs, with all four temperatures continuing to update for more than two minutes.

## Image and procedure

Restored the complete saved 4 MiB factory application at 0x10000, without changing NVS or the bootloader/partition table. Flash hash verification passed. `previous-factory.bin` SHA256 is `01551029f035275096cb56e7b2b298335ef43535f25b447a4b3ea9f347fbe802`; version `v2.14.0-40-gcd82a9e-dirty`, compile Jul 24 2026 11:59:35, ELF SHA `3186ecc062dd020151319e06ab1fa34548420b5eb51fa0ed3382ac5fc42a8e50`.

Saved settings remained 600 MHz, 1160 mV per half, automatic fan with 25% minimum and 60°C target. No firmware source changes or temporary fan settings were applied to this image.

The initial colder boot ran around **2.04 TH/s**, with all four temperatures updating. The first planned warm reboot was interrupted by a supervisor bug: Python 3.9 socket.timeout was not caught as an expected reboot disconnect, and the supervisor sent pause during initialization. This invalid comparison and original script/logs are preserved under `interrupted-supervisor/`. It is not a failed warm-start result.

After correcting and testing network timeout handling, a clean restart began around **2.60 TH/s** (the hardware was partly warmed). The board was allowed to reach the warm condition, the scope was armed, and one `POST /api/system/restart` was sent. The old firmware cycled the ASIC supply, performed its startup ramp, and sent pool work at 600 MHz.

## Successful warm-start result

The final 35-second window contained eight complete SPDLOG measurements:

| ASIC | Mean passing hashrate |
|---|---:|
| A1 | 743.01 GH/s |
| A2 | 741.21 GH/s |
| A3 | 742.20 GH/s |
| A4 | 742.37 GH/s |
| Total | **2968.79 GH/s** |

This is about **99.12% of the 2995.2 GH/s ideal** at 600 MHz. The post-reboot observation lasted at least 120 seconds after fresh four-ASIC telemetry appeared. These are per-ASIC SPDLOG measurements; this unmodified older image did not supply the new bench's per-domain counter samples.

First post-reboot temperatures were A1/A2/A3/A4 **53.69/52.37/53.03/53.36°C**. Final temperatures were **61.27/59.95/60.78/60.45°C**, with automatic fan about 55%. Across 58 post-reboot API samples, each ASIC reported 29–31 distinct temperature values. Thus the apparently healthy temperatures are not four frozen cached values, although raw sensor registers were not instrumented in the old image.

## Scope and interpretation

CH1 remained **50 Ω** at TPS caps; CH2 remained 1 MΩ at GND0. The complete warm-reboot capture has 700,000 points/channel, 100 µs spacing, −10 to +60 seconds relative to force trigger. Neither channel clipped, including the supply-off part of reboot.

The successful startup still contained raw measured TPS peaks to **2.56 V** and lower-half peaks to **1.74 V**. Probe offsets were not calibrated and this slow capture can miss faster peaks. The presence of startup transients is therefore not sufficient to identify a failed startup. The waveform confirms an actual ASIC rail cycle, which the recent warm ASIC-reset-only bench lacked.

The restored historical PLL entries alone failed to repair the newer bench (`../2026-09-05-1102-historical-pll`). The exact old image succeeds, narrowing attention to remaining initialization/work/PVT sequencing and power-cycle differences. It does not isolate a single root cause or establish that the seven PLL changes are harmless in other conditions.

## Follow-up and artifacts

The control image was left running after success, then paused and replaced for the explicitly continued current-firmware deferred-thermal comparison. See `../2026-09-05-1102-deferred-thermal/`; this README records the control outcome rather than the current installed image.

Key artifacts: `result.json`, `serial.log`, `api-samples.jsonl`, `before-reboot-api.json`, `final-api-state.json`, `restart-command.json`, `analysis-summary.json`, `old-firmware-recovery.png/pdf`, and `warm-reboot/raw.npz`, `audit.json`, `waveform.png/pdf`. `run_control.py` and `test_network_retry.py` preserve the corrected supervisor and its validation.
