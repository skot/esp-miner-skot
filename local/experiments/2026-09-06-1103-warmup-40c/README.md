# 40 C warmup threshold

Requested change: reduce the 300 MHz warmup threshold to 40 C.

The production startup now requires **all four ASICs >=40 C for the existing
ten-second dwell**, replacing hottest >=54.5 C / coldest >=52 C. The existing
60 C upper readiness bound, 70 C temperature stop, 1160 mV setting, warm ASIC
supply cycle, unloaded ramp and final passive qualification remain unchanged.
The startup log includes `min_asic_temp=40C`.

Updated the existing startup test harness to verify that all four at exactly
40 C can proceed, and one at 39.9 C cannot proceed even if the other three are
40.1 C. Existing failure/cleanup checks pass. ESP-IDF 5.5.1 build passed.

Flashed the application once to stock 1103 and captured startup. No NVS/fan
settings changed. The one-shot acquisition script stays in `/tmp`; no reset or
cooldown test controller was added to firmware.

The threshold was verified in the boot log. Last 300 MHz warmup temperatures
before the supply cycle were 42.30/41.32/41.64/41.81 C, rather than the former ~55 C.
Pre-first-work temperatures after the unloaded ramp were 45.93/43.79/44.61/44.94 C.

This single boot **failed** 600 MHz qualification at **2045.50 GH/s**:

- A1 D0 and D2: 39/39 zero-PASS cores in each; A1 373.37 GH/s (49.86%).
- A2 D0 and D2: 39/39 zero-PASS cores in each; A2 371.30 GH/s (49.59%).
- A3 D2: 39/39 zero-PASS cores; A3 557.45 GH/s (74.45%).
- A4: all domains active, 743.39 GH/s (99.28%).

Qualification powered the ASICs off before pool handoff. The requested **40 C
threshold remains installed**; no rollback or additional reset was performed.
This is one observed outcome, not a controlled demonstration that lowering the
threshold caused the additional missing domains. Raw capture, source snapshot
and binaries remain local; `firmware-identity.json` identifies the flashed image.
