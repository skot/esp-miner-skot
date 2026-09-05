# Historical PLL entries alone do not recover the deferred-work ramp

2026-09-05. Restored the seven 450–600 MHz PLL entries from the exact older firmware, also matching the vendor table, and repeated the preceding 1160 mV warm-reset/deferred-work bench. The only functional C change from the temperature-retry probe was those seven entries.

Warm 300 MHz qualified at **1490.56 GH/s, 99.53%**. Every broadcast step through 550 MHz locked. A3 read 58.80°C at 525 MHz, then at 550 MHz all three attempts again returned **raw zero, DONE=0, STATUS=3**. Result logs were at 334.856, 335.516 and 336.176 seconds; the firmware stopped at 336.219 seconds before first work. No 575/600 MHz step or post-reset hashrate measurement occurred.

This rules out the seven changed PLL entries as the sole explanation of the A3 sensor failure in this particular warm ASIC-reset sequence. It does not establish that the alternate entries have identical electrical behavior or that restoring them cannot help another startup path. The recommended older entries remain in working source; all source bench flags are off.

The scope capture completed with no clipping before shutdown; post-shutdown CH1 clipping causes the full-record audit rejection. Powered raw peaks reported TPS 2.560 V, lower half 1.760 V, upper estimate 1.332 V. Sampling is 50 us and probe offsets are uncorrected, so this is not a fast-transient characterization. CH1 stayed 50 ohms.

Probe SHA256: `9b617f604db72ee985f5564cf81a418220f7b5e67bd1549e64848d44c36c47aa`. Flash verification, source snapshots, tests, thermal logs and raw waveforms are retained here. See README-preparation.md and vendor-findings.md for provenance and design.

The board was powered off automatically after this test. The following experiment restores the exact older factory application and tests a warm **ESP reboot with an ASIC rail cycle**, as in the user's successful historical procedure; see ../2026-09-05-1102-old-image-control/.
