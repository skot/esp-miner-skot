# Diagnose manual reset failures on production 1103 firmware

The user reported failure at 600 MHz after both an AxeOS warm restart and an
input power cycle, following the successful production startup/mining check.
No firmware, voltage, frequency or fan settings were changed for this diagnosis.
One API restart was requested with USB serial capture already attached.

## Recovered preceding failure

The existing `/api/system/logs` ring buffer preserves logs across software
restart. It still contained the preceding failed boot, so the failure could be
examined directly rather than inferred from cached temperatures or the UI.
It does not provide a separate record of both user-reported failed attempts.

That boot passed warm 300 MHz qualification at 1487.93 GH/s, discharged the
ASIC supply to 36 mV, reinitialized all four ASICs, and completed the unloaded
ramp. All four PLLs reported lock at 600 MHz. Final PLL-step to first-work call
was 1596 ms, compared with 1599 ms in the earlier successful monitored run.

Pre-first-work temperatures were 55.67/54.35/55.17/54.84 C. During the ten-second
passive check they remained around 55–56.5 C. Qualification then reported:

| ASIC | Missing PASS domain | Zero-PASS cores in that domain | GH/s | Throughput / ideal | PASS rate |
|---|---|---:|---:|---:|---:|
| A1 | D0 | 39/39 | 553.79 | 73.96% | 92.84% |
| A2 | D2 | 39/39 | 555.97 | 74.25% | 94.24% |
| A3 | None | — | 742.24 | 99.12% | 99.38% |
| A4 | None | — | 743.44 | 99.28% | 99.50% |

Total **2595.44 GH/s, FAIL**. The startup qualification deliberately powered the
ASICs off; pool handoff never occurred. A1 D2 retained its previously known
single zero-PASS core but was otherwise active. The new whole-domain failures
were **A1 D0 and A2 D2**.

This identifies the failing stage as domain throughput qualification after
first work at 600 MHz. It does not identify whether an internal domain failed
during clock delivery, first-work configuration or another initialization step.
PLL-lock success does not prove that every internal domain is hashing properly.
There was no temperature abort or fresh-rail-read guard trip in this capture.
No scope measurements were made, so voltage transients cannot be ruled out.

Periodic I2C NACK messages also occur in the earlier successful mining log;
they do not distinguish these two outcomes. The USB console implementation
can vary logging timing depending on whether a host reads it, but this alone
does not establish a logging-related cause. ASIC UART1 is separate from the
UART0/USB console. No logging implementation changes were made.

## Requested monitored restart

**Failure reproduced with USB serial logging attached**, on unchanged production
firmware (boot ELF prefix `f9cd78d96`, matching the installed build).

300 MHz warm qualification passed at 1486.04 GH/s. The warm supply cycle,
reinitialization and unloaded PLL ramp completed. All four PLLs reported lock
at 600 MHz. Final-step to first-work delay was 1601 ms. Pre-first-work temperatures
were 56.16/54.68/55.67/55.34 C; maximum during qualification was 57.48 C.

This time **A3 D2** had all **39/39 cores at zero PASS**. A3 delivered
557.17 GH/s, 74.41% of ideal, with 92.65% PASS rate. A1, A2 and A4 delivered
738.48/740.23/742.89 GH/s (98.62/98.86/99.21% of ideal). A1 D2 retained its
known single zero-PASS core; the rest of that domain was active.

At boot time 328.927 seconds, qualification reported **2778.77 GH/s, FAIL**.
It powered off at 328.963 seconds, before pool handoff. The final API read
confirmed ASIC rail 34 mV, hashrate zero, saved 600 MHz / 1160 mV and automatic
fan/minimum settings retained. Post-shutdown temperatures are cached.

The UI/API power-fault flag was absent while powered and appeared with the
intentional shutdown. `TPS546_check_status` includes the regulator OFF status
in its fault mask; the default text is “Power Fault Detected.” This flag alone
is not evidence that a regulator fault caused the missing-domain failure.

The failure therefore also occurs with logging attached, and the affected
ASIC/domain changes between boots. The current startup remains intermittent.
The captures identify the failing qualification and exclude a logged PLL-lock
or temperature-abort trigger; they do not isolate the underlying electrical or
internal initialization mechanism. A future recovery comparison would need to
hold an actual failed launch at fixed PLL settings before testing work reapply;
this production image deliberately powers off when qualification fails.

Raw UART and ring-buffer captures stay local. The one-shot acquisition script
lives in `/tmp`; no restart or cooldown controller was added to firmware.
No firmware or persistent settings were changed during this diagnosis.
