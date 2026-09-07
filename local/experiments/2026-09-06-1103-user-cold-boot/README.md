# Cold ASIC startup observation — 2026-09-06

The user plugged in stock 1103 hardware. We attached serial logging and polled
cached diagnostics and whitelisted system information without flashing firmware,
changing settings, or intentionally issuing a reset. Installed application:
`v2.14.0-52-g381ed6e-dirty`, the restored hot production startup image.

## Capture limitation

The captured boot begins with `rst:0x15 (USB_UART_CHIP_RESET)`. Opening serial
may have triggered this reset despite preparing DTR/RTS low before opening.
This is therefore a **cold-ASIC startup observation, not a verified uninterrupted
capture of the original input-power boot**. The first fresh ASIC readings were
28.28 / 27.62 / 27.62 / 27.95 C. For future user power-cycle captures, prefer
HTTP log collection without opening serial, or establish serial before power-on
if the physical setup permits.

## Results

- Initial 300 MHz qualification: **1480.41 GH/s PASS**.
- Warm 300 MHz qualification: **1488.57 GH/s PASS**.
- Before first 600 MHz work: **55.83 / 54.35 / 55.17 / 54.68 C**.
- Final PLL update to first work: **1596 ms**.
- 600 MHz qualification: **2960.18 GH/s PASS**, pool handoff at boot 359.237 s.
- Pool observation: **304.25 s**, 73 global SPDLOG windows averaging
  **2955.34 GH/s**, range **2935.88–2978.36 GH/s**.
- All 16 domains remained active across 55 complete cached counter epochs;
  minimum individual domain window was **93.61%** of ideal throughput.
  The previously observed single A1 D2 core remained at zero PASS.
- **59 accepted shares, one rejected** at the end of the observation.
- The rejection was `Difficulty too low`, logged 374 ms after the pool
  difficulty changed from 1000 to 16384. This is consistent with a difficulty
  transition issue; causality is not proven. No domain dropout accompanied it.
- No startup or thermal faults were found. Peak sampled mining temperature:
  **56.32 C**. Final ASIC temperatures: **53.52 / 52.20 / 52.86 / 52.53 C**.

After the monitor closed, HTTP confirmed continuous uptime (729 s), 600 MHz,
**2965.56 GH/s**, 63 accepted / one rejected, and mining not paused. The board
was left mining. No scope measurements were taken. This single observation
adds evidence for the hotter launch condition but does not establish cold-boot
repeatability.

## Records

- `attachment.json`: capture method and USB-reset caveat.
- `observe.py`, `monitor-status.json`: observation and completion record.
- `summarize.py`, `result.json`, `pool-epochs.json`: computed results.
- `initial-state.json`, `final-state.json`, `final-diagnostics.json`,
  `post-monitor-state.json`: device snapshots.
- Ignored raw `serial.log`, `diagnostic-samples.jsonl` and
  `initial-device-logs.log`: local evidence, potentially containing private
  connection information.
