# AxeOS core/domain diagnostics — stock 1103x

Adds **Core & domain health** below the existing dashboard widgets for MC3.
Each ASIC shows D0–D3 PASS/FAIL counts, GH/s, and the number of cores with no
PASS (out of 39). Expand **Individual cores** for the 156 logical core IDs,
PASS counts and GH/s; hovering/focusing a cell also exposes FAIL counts.
Zero PASS is red; domain throughput below 90% of the ideal frequency-based
rate is amber. A missing read is shown as unavailable rather than zero.

`GET /api/system/diagnostics` copies a mutex-protected cache. It never accesses
the ASIC bus. The dashboard polls every five seconds, independently of the
regular telemetry WebSocket. The cache uses the existing startup domain reads.
During mining the logger task reads both core banks per ASIC after the existing
timer has expired, before the existing logger rearm. This adds serial reads and
some delay before rearm, but no additional PLL/configuration/work/reset writes.
The same helper and lock order serve startup and live reads.

Counters freeze at the programmed timer; live GH/s uses that duration, not the
longer wall-clock polling interval. During first-work startup observation,
counters are still running and GH/s uses PASS growth divided by elapsed time
between per-chip reads, matching the qualification method. The initial read is
a baseline and is shown as unavailable until the next valid measurement. This
removes initial delivery/bank-read timing skew from the displayed rate.
No startup thresholds, temperatures, fan settings, work strobes, PLL steps, or
settling intervals were changed. The all-ASIC warmup threshold remains 40 C.

Data includes source, startup result, status, frequency, window duration, and
age. Failed qualification data remains accessible after ASIC power-off. It is
RAM data, cleared by ASIC reinitialization or ESP reboot. Chips from different
counter epochs are never combined; chips not yet read in a new window are
unavailable. This is separate from the user-triggered Tuning scan.

Validation:

- ESP-IDF firmware and production AxeOS build passed.
- Full AxeOS test suite: 48 tests passed, including 6 diagnostic tests.
- Host cache tests passed (retention, unavailable vs zero, epoch isolation,
  first-work baselines/deltas, and rejection of counter decreases).
- Existing startup, first-work register-write, and pool-handoff tests passed.
- Two build/flash validation iterations on stock 1103x; NVS retained.
  The first exposed subsecond startup-rate skew; the final image uses deltas.
- Actual dashboard tables and expanded core grid inspected in the browser.

The first observation (`run-01`) failed final qualification at **2046.07 GH/s**.
A1 D0/D2, A2 D0/D2, and A3 D2 each had 39/39 cores with no PASS. The dashboard
retained and highlighted all five domains after firmware shut the ASICs off.
Its 100–300 MHz frozen-window totals matched the log within 0.01 GH/s.
Browser inspection identified inflated subsecond first-work rates; those early
reads now establish a baseline instead of being displayed as a rate. The final
installed image is identified by the top-level `firmware-identity.json`.


Final installed build: qualification failed at **2044.51 GH/s**, with the same
five zero-PASS domains. Retained dashboard chip rates were **373.19, 371.31,
557.44, and 742.47 GH/s**, within 0.22 GH/s of each corresponding qualification
log rate despite the slightly longer last diagnostic interval. The initial
600 MHz samples were correctly marked unavailable until baseline deltas existed.
Counter arrays remained unchanged for over 30 seconds after ASIC shutdown,
while their reported age increased. The final dashboard display was verified
in the browser. ASIC power remains off; the 40 C warmup threshold is installed.

This boot did not reach pool mining, so continuous live-mining collection has
not been hardware-validated in this change. Frozen startup windows validate
its counter units and rate calculation; the build and regression tests pass.
No extra recovery/reset attempts were made beyond the two firmware validation
boots. No scope was connected.
