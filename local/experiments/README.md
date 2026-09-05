# Proto domain-balancing experiments

This archive records the 1102x/1103x startup, PLL, thermal and regulator
comparisons from September 4–5, 2026. The investigation narrative and latest
status are in [domain-balancing.md](../domain-balancing.md).

The latest stock 1103 result is documented in
[2026-09-05-1103-scale-before-limits](2026-09-05-1103-scale-before-limits/README.md):
600 MHz, 2.965 TH/s over a two-minute synthetic-work counter observation,
with a small remaining A1 D2 deficit. The successful 1102 comparison is in
[2026-09-05-1102-full-power-cycle](2026-09-05-1102-full-power-cycle/README.md).

Versioned records include bench/analysis scripts, result summaries, selected
text evidence, plots and firmware hashes. Raw serial/API logs, device backups,
built binaries, source snapshots, downloaded vendor documents and large raw
waveforms/sample tables remain local and are ignored. Historical READMEs also
list those local artifacts; their presence is not implied by a fresh checkout.
Analysis scripts generally require the corresponding local raw captures and
Python dependencies (pyserial, numpy, matplotlib). Hardware-control scripts
encode the setup and authorization of their particular recorded experiment;
review the board, probes, serial port and operating limits before reusing them.

The new bench selectors are disabled in ordinary firmware source; the
preexisting staggered-PLL diagnostic selector remains enabled on this branch.
The warm startup sequence is validated as a bench experiment and does not yet hand off
to normal pool mining. See each experiment's README for completion status;
report-only completion does not imply that every domain passed qualification.
