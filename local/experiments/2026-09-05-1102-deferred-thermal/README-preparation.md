# Defer thermal conversions during the no-work PLL ramp

Prepared 2026-09-05 after the historical PLL entries alone still produced A3 STATUS=3 / DONE=0 at 550 MHz, and the exact older firmware reproduced warm 600 MHz / ~2.97 TH/s with all four temperatures updating.

This probe adds PROTO_WARM_DEFER_THERMAL_READS to the existing deferred-work bench. After the measured 300 MHz warmup and ASIC reset/reinitialization, it issues no temperature conversion at 100 MHz or between PLL steps. MC3_init still configures the thermal sensor; its first conversion/read is after the final 600 MHz settling delay and before first work. Voltage sensor setup/read inside MC3_init is unchanged. The sequence is limited to 20 seconds from reinitialization through PLL settling. Fresh valid temperatures on all four ASICs are required before the first job; retries and >=70°C shutdown remain. During the bounded ramp, the existing asynchronous TPS/communication guard and manual stop remain active.

This isolates temperature-conversion timing relative to the preceding historical-PLL probe. ASIC rail remains at 1160 mV/half across reset; PLL entries remain historical; the CFG/EN burst and synthetic work remain unchanged. It is not yet the exact old ESP reboot's ASIC power cycle. The final test still uses one job and counter arm at 600 MHz and observes 120 seconds before powering off.

Focused actual-helper tests verify exactly one temperature read after final settling, no earlier reads or work/arms, final invalid temperature prevents work, and PLL/stop/chain/producer failures prevent launch. Both builds succeeded. Working source bench flags are restored off after preparing the saved probe.

Probe SHA256: 51daf07615fd5cb9d5866221d0e667fc104ad79cae19313b2862969dbc11b56c.
