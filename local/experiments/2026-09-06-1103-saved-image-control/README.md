# Exact saved-image startup control — 2026-09-06

## Result

**Reproduced 2964.49 GH/s at 600 MHz** on one app-flash boot of stock 1103x.
The original saved-image run measured 2964.58 GH/s: a difference of only
-0.10 GH/s (-0.0034%). The final approximately 30-second window measured
2965.21 GH/s. The full observation lasted 120.929 seconds; counter-delta
coverage was about 119.42 seconds after the initial baseline sample.

| ASIC | Whole-hold GH/s | Domain activity |
|---|---:|---|
| A1 | 737.75 | All four active; D2 96.43% ideal, one zero-PASS core |
| A2 | 741.16 | All four active, no zero-PASS cores |
| A3 | 742.57 | All four active, no zero-PASS cores |
| A4 | 743.01 | All four active, no zero-PASS cores |

The other 15 domains achieved 98.89–99.45% of ideal throughput. No whole-domain
collapse or delayed domain recovery occurred in the observed 600 MHz window.
The `healthy_all=0` flag reflects the remaining A1 D2 deficiency, not a failed
whole-domain launch; `WARM END result=COMPLETE` was logged.

## Exact control and procedure

Flashed the archived `../2026-09-05-1103-scale-before-limits/probe.bin` directly
at application offset 0x10000 without rebuilding or changing source, NVS,
partition table, or web assets. SHA256 before flash:
`fe782f3d7e70980c91719aeffeabaf8322779cd61155a8667627a5bc00295313`.
Esptool verified the written data hash. The image reports
`v2.14.0-45-gbe978c3-dirty`; the archived binary hash identifies this control,
not the version string or a default c91844e build.

Board identity was checked before flashing: version 1103, MAC
28:84:85:BA:96:08, USB /dev/cu.usbmodem11434201. ASIC supply was off at 32 mV;
initial fresh warm300 samples subsequently showed the chips warming through
the 30s C. No scope was attached or used.

The image automatically qualified 100–300 MHz, warmed at 300 MHz to its
original hottest >=54.5 C / coldest >=52 C threshold and ten-second dwell,
then cycled ASIC Vcore and VDDIO. The measured rail discharged to 36 mV.
It reinitialized at 800 mV/half and 100 MHz, raised voltage to 1160 mV/half,
ramped unloaded to 600 MHz, settled, submitted first work and armed its original
active counter observation. No host-issued recovery/reset/configuration strobe
was added. Capture and analysis helpers were copied unchanged from the original
experiment and run in this new directory.

Final PLL lock to first-work marker: 1594 ms. Fresh temperatures immediately
before first work: 56.98 / 55.50 / 56.49 / 56.00 C. Peak post-launch ASIC
temperature: 60.12 C. All logged thermal samples were valid.

## Voltage observation and limits

The independent sampled guard recorded maximum lower/upper half-stack values
of **1238/1177 mV**, versus 1184/1180 mV in the original saved-image run.
These are separate maxima, not a simultaneous voltage pair. The saved bench
uses report-only voltage limits, so the 1.238 V reading did not stop it.
No scope waveform or per-maximum timestamp/duration was captured; this result
cannot establish the actual transient peak or duration, or claim that all
voltages stayed below 1.20 V. Successful hashing does not resolve that question.
Periodic 600 MHz rail ranges are preserved in `comparison.json`.

## Final state and interpretation

The bench powered ASICs off automatically after the hold; API readback was
36 mV. It remains installed. Saved frequency/core voltage settings remain
600 MHz / 1160 mV. This test uses synthetic work, not pool shares, and the
archived application lacks the newly added dashboard diagnostics endpoint.
The current working-tree changes and latest production binary remain preserved.

This reproduces the historical full-rate startup on the same board, contrasting
with the recent approximately 2045 GH/s boots. It supports returning to the
archived startup behavior as a reference. It does not yet isolate the hotter
warmup, original work/configuration strobes, or another image difference as the
cause, and one additional success does not establish cold/warm repeatability.

Artifacts: `firmware-identity.json`, `flash.log`, `serial.log`, `capture-status.json`,
`result.json`, `comparison.json`, `thermal-summary.json`, `final-state.json`, and
`warmup-timeline.png`. Raw logs and detailed sample arrays remain local.
