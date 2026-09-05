# 1102 temperature retry and deferred-work repeat

2026-09-05. User authorized implementing the temperature-reader refinement, flashing, and repeating the warm reset / deferred-work test on connected 1102 hardware.

## Firmware change

`MC3_read_temperatures` now serializes complete temperature transactions. Before each broadcast conversion it drains each ASIC's DOUT and checks that its DONE flag is zero. It then waits for DONE=1 independently on every ASIC, reads status and raw DOUT, and logs the results and validity. A result must have successful reads, a cleared preconversion flag, a completed conversion, a 12-bit raw code and a positive converted temperature. Failed samples are returned as -1 after retry exhaustion.

An invalid sample causes a fresh broadcast conversion, up to three attempts with 100 ms between attempts and a shared two-second deadline. Individual serial operations can extend the deadline slightly. Each attempt replaces the whole sample set; cached temperatures are not used. A valid >=70°C result ends retries so the caller can stop even if another ASIC is invalid. Existing bench shutdown on persistent invalid or >=70°C temperature remains. This is not a change to the conversion formula or an ASIC/work reinitialization on sensor failure.

The old code checked only ASIC 0 DONE after the broadcast; it neither checked each ASIC's completion nor retried an invalid code. The raw-zero result converts to -288.90°C. The old regular telemetry path also ignored negative values while retaining earlier values, and startup polling waited until initialization completed.

## Controlled repeat

Same 1102 hardware, TPS546 1300 kHz, compensation 13 20 C6 19 C6, 1160 mV per half, GPIO enable, 300 MHz warmup, warm ASIC reset with rail left on, explicit +25 MHz broadcast ramp, unchanged PLL tuples and packet format, no new job until final 600 MHz settling, then one synthetic job / one 180-second counter arm and a 120-second observation. Voltage and throughput checks remain report-only as previously authorized; temperature, communication and hardware fault handling remain active.

Only the temperature reader and host scope prefill changed relative to the previous attempt. Additional temperature transactions and retries extend the time between PLL steps, so a success would not isolate per-chip DONE checking from extra settling time.

## Scope

CH1 is 50 ohms at TPS output caps; CH2 is 1 megohm at GND0. Scope preflight was performed with ASIC power off. Waiting nine seconds after SINGLE before forcing the trigger produced a complete 700,000-point capture on both channels, 50 us sample spacing, from -5.5 through +29.5 seconds. The supervisor now uses this verified prefill and explicitly selects MAIN timebase. It still audits clipping and coverage before sending R. This broad record cannot resolve 20 us averages or quantify the fastest transients.

## Host validation

`test_thermal_retry.py` compiles the actual reader and conversion function and injects transient raw zero, persistent zero, delayed A3 DONE, stale DONE, failed data/trigger transactions and hot-with-invalid combinations. All pass, including retry bounds and no hot-value masking. Existing deferred-work ordering/abort, board support and waveform audit tests pass. Probe and normal builds both complete. Probe source and ELF/bin are saved; source bench flags are restored off after building. Normal image is not flashed.

Run results are recorded separately in README.md after completion.
