# Stock 1103: 45 C warmup at 1180 mV — 2026-09-07

Requested experiment: lower the warmup temperature while retaining 1180 mV
per board half-stack and a 600 MHz target. Every ASIC must reach **45 C for
ten seconds at 300 MHz**, and the existing fresh read before first work must
again show every ASIC at least 45 C. The warmup maximum, thermal retries,
PLL cadence, final settling interval, supply cycle, work sequence, throughput
checks and report-only domain voltage telemetry are unchanged. The startup
helper now takes the coldest temperature alone; the former 52/54.5 C condition
was removed from both temperature gates.

The boundary/failure host tests pass at exactly 45 C, reject a single ASIC at
44.9 C during warmup and before work, and continue to cover the voltage ceiling,
rail, temperature, chain, PLL, pause and throughput failure paths. The 45 C /
1180 mV sequence is exercised through its full voltage/PLL/work path. ESP-IDF
5.5.1 build passed. MC3 source hash is unchanged from the previous run.

## Preparation

The miner was already above 45 C, so ASIC power was turned off and the fan
was run at 100% for at least two minutes before flashing. Paused mode forces
the fan to 30%, overriding the manual API setting. To obtain the requested
100% fan with ASIC power off without changing fan firmware, the saved voltage
was temporarily set to **1181 mV**, outside the existing supported startup
range, and pause was released. The existing pre-power validation refused ASIC
startup; the regulator remained at 34 mV and manual fan output reached 100%.
This rejected setting was never applied to powered ASICs. Cooling time started
only after power-off, the validation status and 100% fan were confirmed.

The saved setting and original fan settings were restored to **1180 mV** and
automatic fan control before application-only network OTA. The configured
frequency remains 600 MHz. No serial connection or scope is used; no automatic
recovery/cooling code was added to the firmware. The first fresh temperature
read established the actual thermal starting condition.

`preparation.json`, `before.json`, `configured.json` and `manifest.json` record
settings, source/image identity and cooling. The previous hotter 1180 mV image
remains archived in `../2026-09-07-1103-1180mv/esp-miner.bin`.

PASS core totals will be reported separately from active-domain totals: the
previous successful run had 16 active domains but one persistent zero-PASS
A1 D2 core, so it did not have all 624 cores active.

## Result: first attempt passed, all domains active

The first fresh ASIC temperatures were **28.61 / 28.12 / 27.95 / 28.45 C**.
After every ASIC reached 45 C and completed the ten-second dwell, temperatures
were approximately **47.09 / 46.10 / 46.43 / 46.59 C**. Warm 300 MHz
qualification passed at **1491.00 GH/s**. After the supply cycle and unloaded
PLL ramp, the fresh pre-work temperatures were **50.06 / 48.57 / 49.56 /
49.40 C**. The first 600 MHz qualification passed at **2962.43 GH/s**; there
was no retry or additional reset after this startup.

The **300.88-second** pool hold averaged **2963.85 GH/s** across **42 complete
counter epochs**, ranging from **2948.50 to 2977.46 GH/s**. All **16 domains**
remained active. Exactly **623/624 cores** produced PASS in every complete
epoch: **A1 D2 logical core 105** was the sole zero-PASS core throughout,
matching previous successful runs. The weakest individual domain window was
94.37% of its ideal share. This was not a startup with all 624 cores active.

The hold ended with **43 accepted / 0 rejected shares**. Peak pool temperature
was **51.87 C**. Final sampled board halves were **1181 / 1183 mV**, total
2364 mV; the maximum sampled half-stack voltage across startup and mining was
1192 mV, with no sampled readings above 1200 mV. These are sampled readings,
not oscilloscope transient measurements.

`result.json` also preserves a raw API average that includes the initial
unpublished hashrate value of zero. The reported steady rate above uses only
complete per-core counter windows, deduplicated by epoch (`domain-summary.json`
and `pool-epochs.json`). `core-summary.json` records the zero-core union and
intersection across those same 42 epochs.

A subsequent HTTP check at uptime 561 seconds confirmed continued mining at
**2959.78 GH/s**, **44 accepted / 0 rejected shares**, 600 MHz and 1180 mV.
The board was left mining with the 45 C threshold installed. This establishes
one successful startup from cooled ASICs at the lower threshold, not startup
repeatability or the cause of the previous failures. Application-only OTA
rebooted the ESP32; this was not a manual input-power-cycle test.
