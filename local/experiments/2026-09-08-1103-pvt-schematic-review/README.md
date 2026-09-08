# PVT supply and setup review - 2026-09-08

Read-only review of the saved stock 1103x schematic (KiCad CLI XML netlist and
rendered schematic), MC3 datasheet v1.1, abridged register guide and Proto errata.
No schematic/firmware edits or hardware tests. Source hashes and selected
exported connectivity are saved alongside this note.

## Confirmed facts

- Datasheet p14, table 5.1: PVT is powered by VDDIO12, nominal 1.2 V,
  operating range 1.08-1.32 V relative to the ASIC's local VSS. Digital control
  supply VDDH is 0.675-0.825 V. Core operating rating is 0.24-0.30 V per internal
  core domain (four domains per chip). A nominal 1.20 V chip is therefore at
  the balanced four-domain upper operating point; it is not a documented PVT
  cutoff. No reviewed document states that VDD reaching VDDIO12 disables PVT.
- A1/A2 VSS is board GND; U11 TLV77312 supplies /ASIC/1V2 relative to GND.
  A3/A4 VSS is GND0; U6 TLV77312 supplies /ASIC/1V2-0 relative to GND0.
  Thus both failed chips share the floating PVT regulator/reference. This rail
  also powers their U15 reference oscillator. U7 supplies their VDDH.
- U6 IN and EN are 5V_SWITCH, GND is GND0. Nominally 5 V input gives about
  3.8 V local input at a 1.2 V midpoint; ordinary DC dropout is not suggested.
  TLV773 input range is 1.4-5.5 V; specified output capacitance is 0.47-40 uF.
  Local output capacitors C55/C90/C92/C130/C131 and oscillator C52 are correctly
  connected between 1V2-0 and GND0. There is no obvious missing local bypass.
- U19 OPA2365 level shifter instead runs between 1V2-0 and board GND, sharing
  the top PVT rail. Its C206 0.1 uF + C207 1 uF bypass is to board GND.
  This is intentional op-amp supply bypass, not evidence those caps are wired
  incorrectly; it does create cross-reference loading of a moving floating rail.
- OPA2365 specified supply minimum is 2.2 V. Its nominal supply is midpoint
  plus 1.2 V: about 2.4 V at operating voltage but only 2.0 V at the 800 mV
  initialization point. That is a separate initialization concern. Higher final
  core voltage improves this DC margin, so it alone does not explain 1200 mV
  PVT failure. Successful ULINK reads argue against total level-shifter failure.
- Datasheet pp27-30 prescribes PVT clock 0x01010000 (6.25 MHz from a 12.5 MHz
  system clock), sensor setup and explicit SDIF completion waits. Current setup
  values match those examples. Nothing here indicates PVT is meant to follow
  the 600 MHz hash PLL. Exact internal clock topology remains a Proto question.
- Abridged register map PDF pp5-6: SDIF STATUS bit0=busy, bit1=lock. Observed
  STATUS=3 means both bits are set, not a decoded temperature or documented
  overvoltage fault code. Persisting DONE=0 means no completed conversion.
- Current mc3_setup_thermal_sensor and mc3_setup_vdd_voltage_sensor broadcast
  to every chip but mc3_wait_register(0,...) checks only A1. Setup ignores the
  bool return of writes and waits. Thus A3/A4 setup completion is not required
  before proceeding. Normal voltage-sweep trigger also waits on A1 only.
  The later bounded diagnostic did independently verify each chip's failure.
- Proto errata corrects channel0=VDDIO12, channel2=VDDH, channel3=VDDA and
  channels13/14=VDD. Above-1 V inputs are prescaled, typically /2. Current
  stack mask 0xFFF0 selects 4-15, excluding the auxiliary supply channels.
  Old datasheet channel mapping is superseded by that errata.

## Interpretation and limits

Best hardware lead: a transient on the floating VDDIO12/reference, potentially
involving its shared oscillator or level-shifter load, stalls the sensor/SDIF
path. This fits A3/A4 failing together and both thermal/voltage converters
failing while ordinary register access survives. It is a hypothesis, not a
measured rail violation. A transient can leave state stuck even after voltage
settles. Reinitialization at 1160 mV restored sensing; lowering voltage alone
did not. We have not established that 1200 mV itself prevents reinitialization.

Best firmware lead: unchecked, A1-only completion after broadcast setup hides
an A3/A4 failure and cannot establish readiness on the floating half. This is
a robustness gap, not proof that it caused the observed voltage dependence.
The failing interval includes setup, voltage ramp, PLL ramp and conversion
attempts, so it cannot yet be localized to the PLL transition.

## Discriminating follow-ups (proposed, not executed)

1. Repeat PVT-only setup at 1200 mV with PLLs fixed, verify setup/clock/status
   on every chip, then fresh thermal and voltage conversions. Compare with
   the already successful 1160 mV reinit. Stop safely if monitoring stays invalid.
2. Trace all-chip setup readiness before and after the voltage ramp; add a
   300 MHz voltage-only comparison to distinguish voltage from hash frequency.
3. When probes can be attached, measure TP4 minus TP3 (VDDIO12 at A3/A4)
   during voltage/PLL changes, alongside midpoint. Compare TP12 minus TP17
   for A1/A2. Also check VDDH and the top oscillator if VDDIO12 is clean.
   Use suitable differential/high-impedance probing; do not put a grounded
   scope ground clip on GND0 or assume the earlier 50-ohm Vcore setup applies.
4. Extend diagnostic channels to fresh VDDIO12/VDDH/VDDA while sensors work;
   a stalled sensor cannot reliably report its own failed supply.
5. Ask Proto for VDD-versus-VDDIO12/VDDA headroom limits, meaning/recovery of
   stuck SDIF busy+lock, and valid PVT clock/startup-delay settings.

No basis yet for recommending additional core-domain capacitance as the PVT fix.

Manufacturer sources checked:
- https://www.ti.com/lit/ds/symlink/tlv773.pdf
- https://www.ti.com/lit/ds/symlink/opa2365.pdf
