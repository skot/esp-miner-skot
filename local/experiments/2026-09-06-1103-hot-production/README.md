# Hot production startup with current first-work sequence — 2026-09-06

## Result

**Cold startup passed at 600 MHz / 2960.90 GH/s and handed off to pool mining.**
The following 304.46-second observed pool hold averaged **2955.80 GH/s** over
71 global SPDLOG windows (2938.24–2979.04 GH/s), with **48 accepted shares and
zero rejected shares**. All 16 domains stayed active. The previously observed
single zero-PASS A1 D2 core remained; minimum measured domain throughput across
the complete dashboard counter snapshots was 94.80% of ideal.

Initial fresh ASIC temperatures were 28.94 / 28.45 / 28.28 / 28.78 C, confirming
a cool start. Immediately before first work: **55.83 / 54.35 / 55.67 / 54.84 C**.
Final PLL update to first-work marker: 1596 ms. Startup per-chip throughput:
737.00 / 739.40 / 740.95 / 743.56 GH/s. Peak temperature during the monitored
pool hold was 56.49 C; final temperatures were approximately
52.37 / 50.88 / 51.71 / 51.38 C. Cooling after successful launch did not cause
the five-domain dropout seen during cooler initialization.

No startup/thermal fault or temperature retry was logged. The initial HTTP
poll timed out while the new application booted; polling then recovered.
A post-monitor API check confirmed mining continued, with the rail about
2323 mV and hashrate above 2.9 TH/s. **The board remains mining at 600 MHz.**
This single successful cold boot and five-minute hold do not establish repeated
cold/warm startup reliability or resolve every earlier intermittent failure.

## Change

Production startup at 300 MHz now requires **coldest >=52 C and hottest >=54.5 C
for ten seconds**, preserving its existing warmup upper bound of 60 C. After
the full ASIC supply cycle and unloaded PLL ramp, the existing fresh temperature
read must again meet those minimums before first work is sent. If too cool, the
normal failure path powers ASICs off; no automatic reset/cooling/recovery loop
was added.

The complete `components/asic/mc3.c` file, including the current first-work
sequence and diagnostic instrumentation, is unchanged from this turn's initial
source hash. Startup still arms counters before the first job, omits the extra
PLL-enable write during that setup, and issues no post-job configuration strobe.
The ten-second qualification and normal pool handoff remain.

The previously requested slower PLL cadence remains: two seconds after updates
from 300 through 575 MHz, with the final 600 MHz settling interval at 1.5 seconds.
Voltage remains 1160 mV/half, with the stock 1103 TPS profile. Existing dashboard
changes and other working-tree edits were preserved.

## Validation and artifacts

- Production startup host tests pass, including hot-threshold boundaries,
  rejection of the former 40 C condition, prevention of first work when cooled
  after the ramp, and existing rail/temperature/PLL/pause failure behavior.
- First-work register order and error-propagation tests pass.
- Qualification-to-pool handoff tests pass.
- ESP-IDF 5.5.1 firmware build and diff whitespace checks pass.
- Exact application binary was archived, flashed at 0x10000 and verified.
  NVS, partitions and installed web assets were retained.
- Before flashing, ASIC power stayed off and the fan ran at 100% for at least
  two minutes. Original automatic/manual fan settings were restored before boot.
- USB serial and read-only API diagnostics covered startup plus the pool hold;
  no scope was attached or used. Monitoring was closed without stopping mining.

Hardware: stock 1103, MAC 28:84:85:BA:96:08, USB /dev/cu.usbmodem11434201,
IP 192.168.30.9. Branch 1103x-support; changes remain uncommitted.

See `firmware-identity.json`, `result.json`, `pool-epochs.json`,
`final-diagnostics.json`, `post-monitor-state.json`, source snapshots and the
local build/flash/serial logs. `summarize.py` calculates per-domain rates from
valid per-core counter snapshots, deduplicated by counter epoch; the reported
pool mean above uses the independent global SPDLOG windows.
