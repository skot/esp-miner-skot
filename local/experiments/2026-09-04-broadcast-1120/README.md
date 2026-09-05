# Broadcast baseline tests at 1.120 V per half-stack

These tests lowered voltage temporarily during initialization; NVS retained
400 MHz / 1160 mV. All PLL updates were broadcast. Both attempts powered off
after the baseline rejected, before the host-controlled test launch.

1. **First attempt:** passed the normal qualification criteria through 350 MHz.
   At 375 MHz, A1/A2 produced 74.06/74.21% throughput; A3/A4 produced
   99.03/98.97%. PCB halves were 1091/1148 mV, 57 mV apart, at 12.17 A.
   The shape resembles the earlier quarter-chip loss, but per-domain counters
   were not captured at this rejected intermediate step, so D2 was not proved.
2. **Second attempt:** intended to qualify 350 then scope 375 MHz. The first
   100 MHz window rejected at 97.89% aggregate throughput, 97.31% minimum
   ASIC throughput (criterion 97.5%). The PCB was balanced at 1116/1123 mV.
   No gating, target launch, or recovery intervention occurred.

These were ASIC power/reset starts on a previously warm board, not controlled
ambient cold-soak tests. No synchronized transition waveform was obtained
from either attempt: the scope-ready handshake follows an accepted baseline.
The sampled rail guard did not trip, which does not exclude fast excursions.

Lowering voltage increases nominal overvoltage headroom but reduced hashing
margin in these runs. It is not established as a cure. The follow-up moves
to 1140 mV and scopes 375→400 MHz, with 425 MHz deferred.

The two subdirectories contain condensed evidence and hashes; full serial
logs, exact firmware binaries, and source snapshots remain locally ignored.
