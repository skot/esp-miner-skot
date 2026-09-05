# C4 / TPS546 output capacitor review

Read-only review prompted by concern that C4's ESR affects the measured PLL
transition response. No miner, scope, compensation, firmware or PCB changes
were made. CH1 remains in the 50 Ω setup from the preceding test.

## Schematic / PCB versus WEBENCH

The PCB connects C4 (330 µF), C21 (47 µF) and C22 (47 µF) across /VDD–GND.
L1 connects /Power/SW1 to /VDD and is labelled 360 nH. C4's schematic part
number is APXF6R3ARA331MF61G, a Chemi-Con conductive-polymer aluminum
capacitor. The manufacturer's current product entry specifies 10 mΩ maximum
at 20°C, 100–300 kHz. This identifies the design's specified part, not what
was actually populated on the bench board.

The saved WEBENCH reports use a different polymer part, APXE6R3ARA331MF80G,
or four Murata GRM32EC80J107ME20L ceramics. The capacitor BOM pages and
operating-value/loop-response pages (2, 7 and 8) of both PDFs were extracted
and visually inspected without modifying the PDFs.

| WEBENCH quantity | WBDesign-1103x.pdf (July 31) | WBDesign-1103x-old.pdf (July 20) |
|---|---:|---:|
| Main output bank | 330 µF PXE, 14 mΩ | 4 × 100 µF X6S, 1 mΩ each |
| Other local output capacitors | 2 × 47 µF | 2 × 47 µF |
| Predicted Vout ripple | 41.503 mV p-p | 6.854 mV p-p |
| Predicted Zout at 550 kHz | 3.929 mΩ | 0.811154 mΩ |
| Predicted crossover | 7.043 kHz | 7.225 kHz |
| Reported phase margin | 93.105° | 92.344° |
| Recommended voltage-loop gain | 1.695 | 2.722 |

The displayed PMBus compensation component rows are the same in both reports,
including RVV=20 kΩ and GMV=50 µS; the different *recommended* gain is not
proof that the simulation or firmware used a correspondingly different gain.
Do not infer exact register changes from these headline recommended gains.
The reports model TPS546D24A, 2.2 V/30 A, 320 nH and 550 kHz. Our recent test
uses TPS546D24S, 2.28 V commanded, roughly 11–12 A settled and a PCB labelled
360 nH, with additional distributed ASIC/half-stack capacitance. Their numbers
are comparisons of the modelled designs, not measurements of the bench board.

## Interpretation

ESR contributes an immediate voltage change when capacitor current changes;
the full transient also depends on capacitance, ESL/layout, inductor-current
slew and feedback response. As a scale illustration, 10 A through 14 mΩ is
140 mV. This does not establish a 10 A measured load step or mean all current
flows through C4; the parallel ceramic branches share the transient current.
The relevant quantity is the output network's frequency-dependent impedance,
not C4's ESR alone. A polymer capacitor in parallel is not a resistor placed
in series with all ASIC current, and removing it alone can reduce useful bulk
capacitance and damping.

Four equal 1 mΩ ceramic ESRs ideally give 0.25 mΩ in parallel, but the complete
bank also includes capacitive reactance, package/layout inductance and existing
parts. The saved report's 0.811 mΩ output impedance is therefore not the same
quantity as ESR/4. Effective capacitance must be checked at about 2.3 V DC,
temperature and tolerance; 4 × 100 µF nominal is not a guaranteed 400 µF.

The roughly sixfold predicted switching-ripple reduction supports testing the
ceramic bank. It does not promise sixfold improvement of the measured
+103/−115 mV load transient or eliminate midpoint redistribution. The crossover
remains around 7 kHz in these reports, so loop dynamics deserve review too.
TI treats output capacitance, ESR, loop bandwidth and compensation together.

## Proposed comparison

Confirm the actually fitted C4 and available ceramic part numbers. Review the
actual L/C and bias-adjusted capacitance in the TPS546D24S model/calculator,
including compensation/stability for the proposed bank. A useful first hardware
comparison is adding local low-ESR ceramics in parallel with C4, keeping the
bulk capacitor, firmware sequence, target voltage and warm temperature fixed.
Then compare the full ceramic replacement separately if desired. Adding and
replacing are distinct conditions and both need appropriate compensation review.
Do not remove C4 without replacement just to eliminate its ESR.

Use the same warm 300→325 MHz event and TPS-local CH1 at 50 Ω; evaluate total
VDD overshoot/undershoot, midpoint excursion and per-domain PASS/FAIL, rather
than judging success from switching ripple alone.

## Sources

- Hardware: /Users/skot/Bitcoin/bitaxe/bitaxeProto-1103x/Power.kicad_sch and bitaxeProto.kicad_pcb
- /Users/skot/Bitcoin/bitaxe/bitaxeProto-1103x/WebBench/WBDesign-1103x.pdf
- /Users/skot/Bitcoin/bitaxe/bitaxeProto-1103x/WebBench/WBDesign-1103x-old.pdf
- Chemi-Con: https://www.chemi-con.co.jp/products/detail-condenser.php?part_number=APXF6R3ARA331MF61G
- TPS546D24S datasheet, output capacitor selection and compensation: https://www.ti.com/lit/ds/symlink/tps546d24s.pdf
- Murata ceramic part specification: https://search.murata.co.jp/Ceramy/image/img/A01X/G101/ENG/GRM32EC80J107ME20-01.pdf
