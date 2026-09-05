# Current firmware: warm ASIC supply cycle comparison

Warm at 300 MHz / 1160 mV per half, then hold ASIC reset, disable both Vcore and auxiliary supply, command TPS off, wait at least 250 ms and verify TPS <=100 mV within two seconds. Restore Vcore at 800 mV per half followed by auxiliary power, reset/enumerate all four ASICs at 100 MHz, ramp voltage 810–1160 mV in 10 mV / 150 ms steps, then ramp PLL 125–600 MHz in 25 MHz steps using historical divider tuples. No work or thermal conversion during this restart. Require four fresh valid temperatures after 1500 ms at 600 MHz before sending the first synthetic job; observe domain counters for 120 seconds.

This compares a full ASIC power cycle **and** historical startup voltage profile against retained-rail reset. ESP32 and fan remain running. Broadcast CFG/EN burst and current thermal initialization/reader remain unchanged. It is not a complete replay of every old-firmware initialization transaction.

Probe SHA256 `ca853994da970ddfc1bfb898ea4a3b7378481c600101e2e99c2a9c89e390949c`. Saved probe.bin is the flashed bench; normal.bin has all bench flags disabled. Focused tests cover sequencing, discharge/enable failures, no-work requirements, thermal retry, board support, scope audit and explicit zero-rail permission. Firmware build passed.

CH1 50 ohms at TPS output, CH2 1 Mohm at GND0, both DC / 1x / 20 MHz bandwidth. Scope uses 0.5 V/div on both inputs to include supply-off, 5 s/div with 700k samples and 16-second prefill. Voltage/throughput limits remain report-only per prior user authorization; temperature, communications, bounded waits and manual stop retained.
