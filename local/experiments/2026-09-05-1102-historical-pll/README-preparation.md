# Historical PLL table comparison on 1102

2026-09-05. User asked to recover the historical warm 600 MHz / ~3 TH/s operating point, including working temperatures on all ASICs.

The only functional source difference from the preceding temperature-retry probe is seven PLL_CONFIGS rows at 450, 475, 500, 525, 550, 575 and 600 MHz. The original tuple bytes were verified against the exact saved factory image (`previous-factory.bin`, SHA256 01551029f035275096cb56e7b2b298335ef43535f25b447a4b3ea9f347fbe802), offset 0x36F18, matching the complete 37-row older PLL table. The changed rows and patch are saved here. The current table's fractional entries remain but are never visited by this explicit +25 MHz bench ramp.

Keep all other experimental variables: 1102 regulator 1300 kHz / existing compensation, 1160 mV per half, same 300 MHz warmup and fan policy, ASIC-only warm reset with rail retained, same burst CFG/EN writes, same per-ASIC sensor completion checks/retries, same settling intervals, same synthetic first job after final settling at 600 MHz, and the same 120-second final observation. Voltage/throughput checks remain report-only under existing authorization. Valid overtemperature, persistent invalid sensor readings, communication faults, manual stop and scope audit retain their existing behavior.

This is a controlled test of PLL values, not a complete recreation of the dirty older firmware or proof of hardware-synchronized clocks. Normal source bench flags are restored off after saving the probe. The actual old image remains available as a positive-control fallback if this does not reproduce the operating point. No old-image restoration is part of this initial comparison.

Probe SHA256: 9b617f604db72ee985f5564cf81a418220f7b5e67bd1549e64848d44c36c47aa.

Focused temperature-retry, deferred-work ordering/abort and board-policy tests passed. The host capture script is unchanged from the successful temperature-retry acquisition (nine-second prefill; CH1 stays 50 ohms). The 50 us sample interval limits fast-transient interpretation.
