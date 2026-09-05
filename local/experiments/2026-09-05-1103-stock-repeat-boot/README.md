# Identical reboot of PMBus-only stock 1103 probe

No image or settings change from the preceding PMBus-only test. On this reboot the configured scale 0.25 was already retained. Measured total was 1606 mV for the initial 1600 mV command and all four ASICs enumerated. This makes the first-boot/second-boot TPS configuration difference a concrete lead; PMBus-only enable by itself had not fixed the preceding run.

300 MHz warm qualification reached 1486.42 GH/s (99.25% ideal). The subsequent full ASIC supply cycle stopped before the 600 MHz ramp: TPS total was 682 mV when the sequential ADC midpoint calculation failed. The guard had exempted only totals <=100 mV during the intentional cycle. The two measurements can span different points on a changing rail. Logged derived upper-half maximum 2190 mV is therefore not evidence of an independently measured physical peak. No scope was attached or used.

The firmware shut ASIC power off. Next test retains fresh TPS and ADC read checks throughout the bounded cycle, but resumes derived half-stack voltages only after supply settling and total-rail verification. It also moves TPS scale programming ahead of voltage limits and commands and restores GPIO enable. Logs, raw counters, thermal records and analysis are in this directory.
