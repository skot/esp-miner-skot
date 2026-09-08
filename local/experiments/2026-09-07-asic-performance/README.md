# Unified ASIC Performance card — 2026-09-07

Replaces the MC3 Hashrate Registers widget and standalone Core & domain health
card with one full-width dashboard widget. Per-ASIC voltage meters and the
TPS546 output meter move from Power into this card. Other ASIC models retain
their existing registers and voltage displays.

The overview shows total hashrate, measured active-domain count, frequency,
system power, TPS546 output, lower/upper board-stack voltages and their absolute
difference. Each ASIC row shows summed domain hashrate, temperature, chip VDD,
and four internal-domain tiles with GH/s, active core counts and textual
Active / Low rate / No PASS states. Expand Core counters for the existing
PASS/FAIL table and all 156 individual core counts/rates. Whole-domain failure
is distinct from one inactive core. Voltage formatting retains millivolts.

Hashrates use the existing cached per-core counter windows, with ASIC and
system totals derived from the same domain measurements. Incomplete chip
windows do not produce a misleading partial system total. Startup/retained
measurements remain labelled; telemetry disconnects are labelled separately.
Chip VDD and board-stack voltages are not internal D0–D3 voltage readings.
No per-internal-domain voltage telemetry is currently exposed by this API.

Existing saved MC3 layouts expand the compact registers widget once; other
widget dimensions are preserved, and subsequent user resizing is respected.
The desktop default is 12 columns wide and 24 grid rows high. Narrow cards
wrap each ASIC's domain tiles into a two-by-two grid, with vertical scrolling
for the remaining ASICs and expanded details. Development mock telemetry was
updated to exercise the four-ASIC Proto display.

## Validation

- AxeOS suite: **52 tests passed**, including four added cases for sensor
  mapping/precision, incomplete counter windows, missing/invalid sensors and
  stale telemetry distinct from retained counters.
- `npm run build`: production bundle passed. The performance component's
  3.12 kB stylesheet exceeds the 2 kB warning budget but remains below the
  4 kB error limit; Home and Tuning also have stylesheet budget warnings.
- Browser checks: one integrated card, removal of duplicate MC3 voltage
  meters, migration preserving other widths, later resize preserved,
  156 expanded core cells, no horizontal overflow at 390 px, and explicit
  retained-domain failure. No browser exceptions were observed.
- Desktop content fits within its default card height. Mobile screenshots
  show the top and bottom of the same vertically scrollable card.
- `git diff --check` passed.

Screenshots (`desktop.png`, `mobile.png`, `mobile-bottom.png`, and
`failed-domain.png`) use development/demo data, not a new hardware test.
`browser-checks.json` records the browser verification. Local preview:
`http://127.0.0.1:4201/` while the dev server is running.

The implementation changes only frontend source. Before deployment, no firmware
startup, ASIC register writes, backend API changes, flashing, or miner settings
changes were performed. See the deployment result below.
At the time of this UI test, the prior fallback was commit `591dfde`. The UI
is included in the subsequent 1200 mV/PVT mining checkpoint.

## Deployment to stock 1103x

Both `www.bin` and matching `esp-miner.bin` were built with ESP-IDF 5.5.1 and
uploaded through `/api/system/OTAWWW` and `/api/system/OTA`. The board rebooted
once; no serial connection was opened and NVS settings were retained. Both
versions now report `v2.14.0-53-g591dfde-dirty`. Downloaded index, JavaScript and
version files match the build hashes. The new card was verified on the device.

**Flash succeeded, but this boot failed 600 MHz qualification at 2594.87 GH/s.**
A1 D0 and A2 D2 each had 39 zero-PASS cores; the remaining 14 domains were active.
Initial ASIC temperatures were approximately 39–40 C. Warmup at 300 MHz passed
at 1486.27 GH/s before the full supply cycle. Before first 600 MHz work,
temperatures were 54.68 / 53.36 / 54.18 / 54.02 C, satisfying the configured
minimums. The firmware shut ASIC power off, with 36 mV regulator telemetry,
and did not start pool mining. The planned two-minute pool hold was not reached.

Startup and MC3 source hashes still match the previously successful hot
production archive. This is further evidence that startup repeatability remains
unresolved; the failure alone does not establish a cause. No additional reset,
recovery attempt or firmware change followed. The installed card retains and
highlights the real failure data for inspection.

Deployment records, image hashes, whitelisted state, qualification excerpts
and the on-device screenshot are under `flash/`. Raw logs and binaries remain
ignored local artifacts. The HTTP log buffer contains records from before and
after the OTA reboot; use the captured state and final qualification excerpt
to identify this boot rather than aggregating the entire log as one session.
