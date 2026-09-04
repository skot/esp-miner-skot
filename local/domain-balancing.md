# bitaxeProto 1103x domain-balancing investigation

Last updated: 2026-09-04

## Summary

The bitaxeProto 1103x can run all four MC3 ASICs normally through 400 MHz,
but a frequency transition near 425 MHz can leave A1 and A2 with greatly
reduced throughput. The loss is concentrated in the MC3 internal D2 voltage
domain, which contains slices 6–8: 39 of the ASIC's 156 small cores, or exactly
one quarter of its hashing resources. A3 and A4 normally remain healthy.

The latest evidence points away from a simple lack of voltage or a permanent
hardware defect. After a failing 425 MHz launch, broadcasting the same work
configuration again, without changing any PLL divider, restores A1/A2 D2 to
normal throughput. It then remains healthy under continuous load. Our leading
hypothesis is therefore a timing or state-initialization problem between a PLL
frequency update and the following MC3 `WORK_CFG` reset/work activation.

The suspected firmware fix is to let the newly programmed PLLs settle before
the final broadcast work reset/activation. A delayed second work reset is a
known-effective fallback, but the required delay and minimum production
sequence still need to be measured.

## Hardware and symptom

- Hardware under test is bitaxeProto 1103x with four MC3 ASICs.
- The TPS546D24S supplies the two series half-stacks. At a configured
  1160 mV per half-stack, its total output is approximately 2.32 V.
- The PCB midpoint is `GND0`. The two external measurements are
  `GND0 - GND` and `VDD - GND0`.
- Each MC3 additionally contains four internal series voltage domains, D0–D3.
  Each internal domain contains three slices and 39 small cores.
- A bitaxeProto 1102x has generally reached 600 MHz and approximately 3 TH/s
  with this ASIC family, although genuinely cold starts have sometimes been
  troublesome.
- On 1103x, the first obvious failures were around 425–450 MHz. More granular
  testing also found marginal behavior around 434–438 MHz. In the repeatable
  400-to-425 MHz test, A1 and A2 lose throughput while A3 and A4 remain close
  to expected performance.

The external half-stack imbalance is not perfectly repeatable. Some failing
runs produced a large midpoint shift, while other runs showed poor D2
throughput with the two PCB half-stacks still balanced. This distinction became
important: the midpoint is useful, but it is not by itself a reliable test of
whether every internal MC3 domain is hashing.

## Instrumentation added

The firmware was extended during the investigation to provide:

- calibrated GPIO2 midpoint measurements;
- lower and upper PCB half-stack voltage after frequency steps;
- TPS546D24S current telemetry;
- per-ASIC internal VDD readings;
- all MC3 PVT voltage-sensor taps and calculated D0–D3 voltages;
- per-core SPDLOG counters grouped into slices and internal D0–D3 domains;
- addressed PLL changes for individual ASICs;
- fixed-PLL work-reset and long-duration recovery diagnostics.

This made it possible to distinguish an entire ASIC failure from the loss of a
single internal domain, and to distinguish an electrical voltage collapse from
a domain that has normal voltage but is not doing normal work.

## Experiments performed

### Power-supply and PCB changes

- Corrected the 1103 TPS546D24S configuration for its 550 kHz switching
  frequency and applied the WEBENCH-recommended compensation values.
- Corrected `VOUT_SCALE_LOOP` to 0.25 and verified readback of scale, switching
  frequency, and compensation registers after initialization.
- Added a 100 uF ceramic capacitor to each PCB half-stack.
- Replaced U7 and U10 with `TLV77308PDBVR` devices to test whether the floating
  VDDIO LDO arrangement was contributing leakage or bad regulation.
- Reflowed A1.
- Assembled and tested a second 1103x board.
- Replaced A1 internal-domain bypass capacitors C114–C121 with the older Taiyo
  Yuden 1 uF parts used on 1102x.

These changes did not eliminate the characteristic A1/A2 D2 failure at the
400-to-425 MHz transition. Extra half-stack capacitance did alter the shape of
the scope transient, but did not make the hashing failure disappear.

### PLL and ramp experiments

- Logged both PCB domains and TPS current at every 25 MHz PLL step.
- Tried smaller PLL steps between the normal table entries.
- Corrected `mc3_pll_config_t.frequency_mhz` to preserve fractional frequency
  values used by the fine-step table.
- Tested the equally valid 450 MHz divider tuple
  `(FBDIV=144, POSTDIV1=3, POSTDIV2=1)` and extended that divider family above
  450 MHz.
- Compared addressed per-chip updates with normal broadcast PLL updates.
- Tried alternate chip-update orders across the two PCB half-stacks.
- Tried a ping-pong PLL experiment. The cores did not successfully live-switch
  to the alternate locked PLL, so this is not currently a viable solution.
- Shortened the interval for which the active PLL was stopped during a normal
  reprogramming sequence. Scope captures showed approximately 4.44 ms in the
  original sequence and approximately 2.72 ms after shortening it.

Reducing the PLL-off interval reduced the electrical disturbance, but did not
by itself make operation above the failure threshold reliable. The addressed
tests also showed that the problem is not simply the total load step seen by
the regulator.

### Voltage/frequency and load tests

- Tested several core-voltage/frequency combinations around the failure
  threshold.
- Held the board at a failing frequency and examined loaded and load-release
  behavior.
- Measured internal capacitors directly with a DMM and captured both PCB
  half-stacks plus their difference with a two-channel scope.
- Added a safety limit that terminates diagnostics if either PCB half-stack
  reaches 1.20 V.

Increasing capacitance, changing the LDOs, reflowing A1, and changing static
voltage did not provide a consistent cure. Frequency-transition and work-launch
history were more predictive than the settled regulator voltage.

## Internal-domain findings

At a healthy 400 MHz baseline, all four ASICs produce approximately 99% of
expected throughput. Their internal PVT domain voltages are near 0.29 V and
their per-core counters show activity in all four internal domains.

In some earlier 425 MHz failures:

- A1 and A2 fell to roughly 75% total throughput;
- all 39 cores in D2 reported zero passes;
- A1/A2 D2 PVT voltage fell to about 239–240 mV;
- the external PCB split increased to approximately 59 mV.

That looked like a complete internal-domain collapse. However, the latest
clean comparison exposed a more nuanced version of the same failure:

- all four ASICs were healthy at 400 MHz;
- A1, A3, A2, and A4 were changed to 425 MHz back-to-back in approximately
  74 ms;
- the normal identical-work reset was issued immediately after the final PLL
  update;
- A1 reached only 82.14% and A2 only 81.94% of expected throughput;
- A1/A2 D2 produced only about 36% of its ideal share of the ASIC's passes;
- A3 and A4 remained at approximately 99.2%;
- A1/A2 D2 still measured a normal 287.3–287.6 mV;
- the PCB half-stacks remained balanced within 9 mV;
- no D2 core had a zero counter.

This proves that the D2 throughput failure can exist without a persistent D2
voltage collapse. A large midpoint divergence is therefore probably a
consequence or more severe form of the bad domain state, rather than the only
root cause.

## Decisive fixed-PLL recovery test

After reproducing the latest A1/A2 D2 failure, the firmware performed exactly
one additional broadcast reset/reapplication of the same qualification work.
No PLL frequency or divider was changed. It then armed a 30-second SPDLOG
window and sampled A1/A2 D2 counters, PVT voltage, PCB midpoint, and TPS current
without further PLL or work updates.

Results after the additional work reset:

- A1 D2 reached 97.2% during the first reliable interval and then remained at
  approximately 98–101%.
- A2 D2 reached approximately 99% and remained at approximately 97–100%.
- At 30 seconds, cumulative D2 throughput was 98.6% for A1 and 98.8% for A2.
- A1/A2 D2 remained at approximately 287.1–287.6 mV.
- The external half-stack difference remained between 7 and 11 mV.
- TPS current remained approximately 14.16–14.33 A.
- No delayed collapse occurred during continuous 425 MHz work.
- The subsequent rollback to 400 MHz passed at approximately 99% per ASIC.

The first sub-second sample has timing skew because A1 and A2 counters are read
sequentially. It should not be used to compare the two ASICs precisely. The
later interval measurements use the same read order and clearly show sustained
full-rate D2 operation.

## Current root-cause hypothesis

The strongest current hypothesis is an MC3 initialization or synchronization
race during a frequency transition:

1. The PLL dividers are changed and lock is reported.
2. Work is reset/reactivated almost immediately.
3. A1/A2 D2 enters a bad hashing state even though its PLL and voltage can look
   normal.
4. The bad state persists under that work launch; merely waiting does not make
   the initial qualification window pass.
5. Reissuing the identical work reset after the clocks and internal state have
   had time to settle restores D2 immediately and permanently at 425 MHz.

The 1103 regulator, capacitor, or layout changes may reduce electrical margin
and make this race much easier to trigger than on 1102x. They are not completely
exonerated. Nevertheless, normal settled PVT voltage, balanced PCB rails, and
the successful fixed-PLL work-only recovery argue strongly against insufficient
steady-state Vcore, the TPS control loop, or a permanent A1 solder fault as the
primary cause of the latest failure.

It is also not yet clear why A1 and A2 repeatedly expose D2 while A3 and A4 do
not. Possibilities include chain timing, the A1/A2 PCB half-stack environment,
or lower margin in those two ASICs. The repeatability of the D2 grouping makes
an arbitrary quarter-core software-accounting error unlikely, but the exact
MC3 internal mechanism remains unproven.

## Suspected firmware fix

The likely production sequence is:

1. Quiesce or otherwise prepare work for the frequency transition.
2. Program all required PLL dividers.
3. Verify that every PLL reports lock.
4. Wait for a measured settling interval.
5. Broadcast one final `WORK_CFG` reset and reapply work only after that delay.
6. Qualify both per-ASIC throughput and per-domain core activity before
   proceeding to the next frequency.

Until the minimum safe delay is known, a conservative fallback is to issue a
second delayed work reset if qualification detects a weak or inactive internal
domain. This changes no PLL divider and was effective in the fixed-PLL test.

The next experiment should sweep the delay between the last 425 MHz PLL update
and the first/final work reset, for example 0, 10, 50, 100, 250, 500, and
1000 ms. Each point should begin from a fresh qualified 400 MHz state and record
A1/A2 D2 interval throughput, PVT voltage, PCB midpoint, and TPS current. The
smallest delay that repeatedly starts all four domains near full throughput on
cold and warm boots should become the production settling delay, with some
margin added.

## Safety and current status

The TPS546D24S enable path was changed so the active-high enable GPIO is driven
low early and remains disabled while the ESP32 is in its bootloader/reset state.
This prevents the ASIC rail from remaining enabled during USB flashing. The
diagnostics also stop if either external half-stack reaches 1.20 V.

The automatic recovery diagnostic is currently compiled out. After the last
test, the board was restored to the normal 400 MHz firmware and verified at
approximately 1154/1163 mV across the two PCB half-stacks and approximately
13.27 A TPS output current.
