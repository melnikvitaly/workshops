# Design Review — Workshop 5-1, ESP32-S3 Dev Board (Rev A)

Schematic-only review. `kicad.kicad_pcb` contains no footprints, tracks or board outline, so all layout, EMC, thermal-layout and gerber analysis is out of scope for this pass.

**Verdict: not ready for layout or fabrication.** Three defects will stop the board working as drawn, one of which (U5 footprint) cannot be caught by DRC/ERC. The core architecture — USB-C sink → Schottky → BQ24040 linear charger → 1S Li-ion → TLV75801P LDO → ESP32-S3-WROOM-1 — is sound.

---

## Verification basis

| Claim class | Evidence |
|---|---|
| Netlist / connectivity | **KiCad 10 native netlist export + native ERC** (ground truth), cross-checked against `analyze_schematic.py` — 55 nets, identical on both. |
| U5 BQ24040 pin functions, TS/PRETERM/ISET2 behaviour, ISET equation, package geometry | TI **SLUS941H** (Feb 2021), downloaded to `datasheets/BQ24040.pdf`. Sections cited inline. |
| U6 TLV75801P pinout, V_FB, divider limits, EN behaviour, θJA | TI **SBVS351D** (Oct 2023), downloaded to `datasheets/TLV758P.pdf`. Sections cited inline. |
| U1 ESP32-S3-WROOM-1 pinout | KiCad official `RF_Module` symbol + Espressif pin numbering. **Not** datasheet-verified this pass — see *Limits*. |
| Passives, LEDs, connectors, TVS | **Unverified** — no MPNs and no datasheets in the project. Assessed for plausibility only. |

Computations in `analysis/helpers/calc.py`; analyzer runs in `analysis/2026-07-28_2143/`.

---

## Blockers

### B1 — U5 pin 9 (TS) tied directly to GND: the charger will never charge  `CRITICAL`

`GND` net includes `U5.9`. TI SLUS941H Table 6-1 (p4): *"Pulling terminal Low disables the IC."* Section 8.4.8 (p19) repeats it: *"A low disables charge (similar to a high on the BAT_EN feature)."*

The battery charger is held permanently disabled. Nothing else in the design compensates.

**Fix:** SLUS941H §8.4.8 — *"If this feature is not needed, a fixed 10 kΩ can be placed between TS and VSS to allow normal operation."* Fit a 10 kΩ resistor from TS to GND. If the pack has a thermistor, use a real 10 kΩ NTC (β=3370, e.g. Semitec 103AT-2) instead and get JEITA temperature protection.

### B2 — U5 footprint is the wrong package  `CRITICAL`

Assigned: `Package_SON:WSON-10-1EP_2x3mm_P0.5mm_EP0.84x2.4mm`.
Actual BQ24040DSQR package **DSQ0010A** (SLUS941H package outline, drawing 4218906/A):

| | Datasheet (DSQ0010A) | Assigned footprint |
|---|---|---|
| Body | 2.0 × 2.0 mm (1.9/2.1) | 2 × 3 mm |
| Pin pitch | **0.4 mm** (`8X 0.4`) | **0.5 mm** |
| Exposed pad | ~1.6 × 1.5 mm | 0.84 × 2.4 mm |

Wrong on all three counts. Pitch error alone accumulates 0.4 mm across the pin row — the part cannot be soldered to this land pattern. Invisible to ERC and to DRC.

**Fix:** No stock KiCad footprint matches (the library only has WSON-10 in 2.5×2.5 P0.5, 2×3 P0.5, 4×3 and 4×4). Use TI's published land pattern from the datasheet ("EXAMPLE BOARD LAYOUT": 10 pads 0.5 × 0.2 mm on 0.4 mm pitch, EP with vias) or import TI's footprint. Update both `workshop.kicad_sym` and the schematic instance.

### B3 — LDO enable is a momentary button with no pull-down: board cannot stay powered  `CRITICAL`

`Net-(U6-EN)` = `{SW3.2, U6.3}` — nothing else. SW3 is `Switch:SW_Push` / `Button_Switch_SMD:SW_SPST_TL3342`, a **momentary tactile** switch, and the schematic text labels it *"power switch"*.

Two independent problems:
1. **No latch.** VCC is only enabled while the button is physically held down. Release it and the board dies. There is no GPIO on the EN net to hold it up, so firmware cannot latch it either.
2. **EN floats when open.** SBVS351D lists I_EN = 10 nA and the internal pulldown (R_PULLDOWN, 95 Ω) is on **OUT**, not EN — there is no internal pull-down on EN. With the button open, EN is a floating high-impedance node between V_EN(LO) = 0.3 V and V_EN(HI) = 1.0 V. Undefined state.

**Fix — pick one:**
- **Slide/latching switch** (simplest): replace SW3 with an SPDT slide switch, VBAT → EN, plus a 100 kΩ EN → GND pull-down. Matches the "power switch" intent.
- **Soft-latch**: momentary button sets EN via a P-FET/diode, ESP32 GPIO holds EN high after boot, firmware releases it to power down. Adds parts, gives software shutdown.
- **Always-on**: SBVS351D §6.3.2 — *"If shutdown capability is not required, connect EN to IN."* Drop SW3, tie EN to VBAT.

Whichever you choose, a pull-down on EN is mandatory.

### B4 — U5 pin 4 (PRETERM) tied directly to GND: out of spec  `HIGH`

`GND` net includes `U5.4`. SLUS941H Table 6-1 (p4): *"Expected range of programming resistor is 1 k to 10 k (2 k: Ipgm/10 for term; Ipgm/5 for precharge)."* Electrical Characteristics (p7) specifies R_PRE-TERM ≥ 1 kΩ, and gives the default-termination condition as `R_PRE-TERM = High Z`.

0 Ω is below the specified minimum. Precharge and termination thresholds are undefined — the charger may fail to terminate, which on a Li-ion cell is a safety issue, not just a nuisance.

**Fix:** 2 kΩ from PRETERM to GND (gives the documented 10 % termination / 20 % precharge), or leave the pin unconnected for the High-Z default.

### B5 — USB1 has no footprint  `HIGH`

`USB1` (USB4105-GF-A-120, `Connector:USB_C_Receptacle_USB2.0_16P`) has an empty `Footprint` property. Netlist → PCB will fail. Every other component has one.

### B6 — Zero MPN coverage  `HIGH`  *(analyzer SS-001, DS-001)*

0 of 35 components carry an MPN. The IC *values* happen to be real part numbers (`ESP32-S3-WROOM-1-N16R8`, `BQ24040DSQR`, `TLV75801PDBVR`), which is what let me verify U5 and U6 — but every passive, LED, TVS, switch and connector is unsourced and unverifiable. Not orderable, and not fully reviewable (see *Limits*).

---

## Warnings

### W1 — Feedback divider is 3.6× the datasheet maximum

R9 = 1 MΩ, R10 = 200 kΩ. SBVS351D §7.1.1 eq. 3: `R1 + R2 ≤ VOUT / (I_FB × 100)`, with I_FB = 0.1 µA max → **330 kΩ max**. Actual sum is **1.2 MΩ**.

Consequence: the I_FB error term is no longer negligible. `R9 × I_FB` = **±100 mV**, i.e. **3.30 V ±3.0 %** from this term alone. Stacked with ±1.5 % V_FB accuracy and 1 % resistors, worst case reaches ≈ 3.50 V. Still inside the module's 3.6 V absolute maximum, so this is an accuracy/margin issue rather than an over-voltage risk — but it eats most of the headroom, and a 1.2 MΩ node is needlessly noise-sensitive.

**Fix:** scale the divider down 4×: **R9 = 255 kΩ, R10 = 51 kΩ** → 0.55 × 6 = 3.30 V, sum 306 kΩ, inside the limit. Worst case tightens to ≈ 3.40 V.

### W2 — Charge current exceeds the USB budget the board advertises

R6 = 1 kΩ on ISET. SLUS941H: `I_OUT = K_ISET / R_ISET`, K_ISET = 540 typ (510–570) → **540 mA typ, up to 570 mA**.

ISET2 is tied Low. Table 6-1: *"Programming the Input/Output Current Limit … BQ24040/5 ⇒ High = 500 mA max, Low = ISET, FLOAT = 100 mA max."* So the input limit is 540 mA.

The board is a USB-C sink with 5.1 kΩ Rd on CC1/CC2 and **nothing reading the CC voltages** — no PD controller, no ADC. It therefore cannot detect an advertised current above USB Default, and must budget for **500 mA**. 540–570 mA input exceeds that.

**Fix:** tie ISET2 **High** (to +5V; abs max on that pin is 7 V, and the rail is ~4.6 V after D4) for the datasheet-specified 500 mA input limit. This is the correct configuration for a USB-only charger and leaves ISET setting the battery-side fast-charge current.

### W3 — Reset RC network is duplicated

`/RESET` = `{U1.3, R3.2, C4.1, R5.2, C6.1, SW2.1}`. R3 (10 kΩ) + C4 (1 µF) sit next to the module; R5 (10 kΩ) + C6 (1 µF) sit in the RESET/BOOT block. They are the same network on the same net, drawn twice.

Effective values are 5 kΩ ∥ 2 µF = 10 ms — the same time constant Espressif recommends, so it *works*, but it's two redundant parts and an accidental halving of the pull-up. Almost certainly left over from drawing the module block first.

**Fix:** delete R5 and C6 (or R3 and C4).

### W4 — Power LED may not light

LED3 is `KT-0805G` (green) fed from **VCC = 3.3 V** through R11 = 1 kΩ. Standard InGaN green has V_f ≈ 3.0–3.2 V at 20 mA. That leaves ~0.1–0.3 V across 1 kΩ → **0.1–0.3 mA**: likely invisible.

Unverifiable without an LED datasheet (no MPN). If KT-0805G is a yellow-green GaP part (V_f ≈ 2.2 V) it's fine at ~1 mA. LED1 (same part, on VBAT 3.7–4.2 V) has more headroom and is less at risk. LED2 is red on +5 V — fine.

**Fix:** confirm V_f, and if it's a true green, drop R11 to ~100–220 Ω.

### W5 — Thermal headroom on U5 is modest and depends entirely on the exposed pad

Worst case charging a depleted cell: P = (5.0 − 3.0) × 0.54 = **1.08 W**. With θJA = 63.5 °C/W (SLUS941H §7.4, DSQ WSON-10): ΔT = 68.6 °C → **T_j ≈ 94 °C at 25 °C ambient, 109 °C at 40 °C**. Below the 125 °C thermal-regulation point, so the part will protect itself by tapering charge current rather than failing — but only if the exposed pad is soldered to a real copper pour. That θJA assumes it. **B2 currently makes a correct EP connection impossible.**

U6 is more comfortable: 0.32 W at the ESP32-S3's 355 mA Wi-Fi TX peak, θJA = 176.9 °C/W (SBVS351D §5.4, DBV) → T_j ≈ 82 °C at 25 °C ambient. At a sustained 500 mA it reaches ≈ 105 °C — fine for bursts, worth watching if you ever load the 3.3 V rail continuously.

### W6 — No VBUS bulk capacitor  *(analyzer UC-001)*

VBUS carries only USB1, D2 (TVS) and D4. The 10 µF (C1) is on the far side of the Schottky. USB 2.0 §7.2.4.1 wants 1–10 µF on VBUS itself for a bus-powered device. Functionally harmless here — and the series diode actually helps inrush — but it is a spec deviation. Add 1 µF on VBUS if you care about compliance.

### W7 — ESD protection is incomplete

D1 (+5V), D2 (VBUS), D3 (D+), D5 (D−) are covered. **CC1, CC2, SBU1, SBU2 are not.** CC pins are exposed in the connector and are a common ESD entry point; a strike there can destroy the 5.1 kΩ resistors or track back into the connector shell. Low cost to add a 4-channel array.

### W8 — No battery or USB telemetry, and no over-discharge protection

- No divider from VBAT to an ESP32 ADC pin — firmware cannot read battery voltage or report state of charge.
- No sense of +5V/VBUS — firmware cannot tell whether USB is plugged in (the ~PG signal only drives an LED).
- CN1 is a bare JST-XH to a Li-ion cell. BQ24040 has **no** over-discharge or short-circuit protection. If the cell is a bare unprotected one, deep discharge below ~2.5 V will damage it permanently.

The first two are ordinary dev-board omissions and easy to add (VBAT divider to any free ADC GPIO — you have 33 spare). The third is a safety assumption: **confirm the pack has an integral protection PCM**, or add one.

### W9 — No test points  *(analyzer test_coverage)*

No test points on +5V, VCC, VBAT or RESET, and no debug header. Native USB covers programming and serial, so this is a bring-up convenience issue rather than a defect — but pads on the three rails cost nothing.

---

## Design notes (not defects)

- **LDO drops out on a discharging cell.** TLV75801P dropout is ~150–250 mV in this range, so VCC holds 3.30 V only while VBAT > ~3.5 V. Below that VCC tracks VBAT − V_DO down to the ESP32-S3's 3.0 V minimum. Normal and accepted for LDO-from-1S-Li-ion; a buck-boost is the alternative if you want regulation across the whole discharge curve.
- **Load on the charger output is sanctioned.** SLUS941H p1: *"A system load can be placed in parallel with the battery as long as the average system load does not keep the battery from charging fully during the 10 hour safety timer."* BQ24040 has no dynamic power path, so the ESP32 load does perturb termination detection — acceptable here, worth knowing.
- **USB-C sink configuration is correct.** 5.1 kΩ Rd on both CC1 and CC2, no Rp, sink role. Verified by the analyzer's USB compliance check and by inspection.
- **D+/D− have no series resistors.** Correct for ESP32-S3 native USB — the PHY has integrated termination. Analyzer flagged this as informational only.
- **U5 pin 6 (NC) is correctly left unconnected** with a no-connect flag. SLUS941H: *"Do not make a connection to this terminal."*
- **U5 EP → GND is correct.** SLUS941H: *"Connect exposed thermal pad to VSS terminal of the device and main ground plane."*
- **33 unconnected ESP32 GPIOs.** The NEO-PIXEL and SCREEN blocks are empty placeholders. This is an unfinished schematic, not a defective one — all 33 KiCad ERC violations are this. Add no-connect flags to pins you intend to leave unused before fab.

---

## Analyzer output I checked and rejected

Worth recording, because these would have been wrong in the report:

| Analyzer claim | Reality |
|---|---|
| `Regulator U6 estimated Vout = 3.6 V`, and `rail_voltages: VCC = 3.6 V`, and `power_budget` VCC = 3.6 V | **False.** The analyzer used a heuristic V_ref = 0.6 V. SBVS351D §7.1.1 states **V_FB = 0.55 V**. Actual Vout = 0.55 × (1 + 1M/200k) = **3.30 V**. Had this stood, it would have read as a supply sitting at the module's absolute maximum. |
| `power_sequencing: VCC enable_type = always_on` | **False.** VCC is gated by SW3 on U6's EN pin — see B3. |
| `PU-001: U6 pin EN missing pull-up` | **Misdiagnosed.** A pull-up would defeat the switch. The real defects are a missing pull-*down* and a momentary switch used as a latching one. |
| 33 × `NT-001 single-pin net` warnings | **Expected**, not defects — unpopulated GPIO for planned blocks. |

---

## Answer to "some label connections look missing"

Checked directly; **they are not**. Two independent confirmations:

1. All 8 local labels (`D+`, `D−`, `RESET`, `BOOT`, each appearing twice) land on a wire — verified geometrically in `analysis/helpers/label_check.py`. Cross-sheet-block nets resolve correctly: `/D+` = `{USB1.A6, USB1.B6, D3.1, U1.14}`, `/D−` = `{USB1.A7, USB1.B7, D5.1, U1.13}`, `/RESET` and `/BOOT` likewise reach U1.
2. KiCad 10's own netlist export and ERC agree with the analyzer on all 55 nets, and ERC reports **zero** connectivity violations — all 33 are `pin_not_connected` on U1, i.e. the deliberately-unwired GPIOs.

What you are most likely seeing is the 33 bare GPIO pin stubs on the module plus the empty NEO-PIXEL and SCREEN blocks.

One note on the image you shared: it shows the switches as **U2 / U4 / U7**, but the file on disk has them as **SW1 / SW2 / SW3**, leaving gaps in the U-numbering. Your screenshot predates a re-annotation. This review is against the current file (`kicad.kicad_sch`, modified 21:28).

---

## Not performed / limits

- **PCB, EMC, thermal-layout, gerber, DFM, impedance, return-path** — `kicad.kicad_pcb` is empty. Nothing to analyze.
- **SPICE** — no simulator on PATH (`ngspice`/`ltspice`/`xyce` all absent). The RC and divider values here were verified by hand instead.
- **Lifecycle / obsolescence audit** — needs MPNs (none) and distributor credentials (`DIGIKEY_CLIENT_ID`, `MOUSER_SEARCH_API_KEY`, `ELEMENT14_API_KEY` all unset).
- **Prior-review delta** — no previous review or prior analysis run exists.
- **U1 ESP32-S3-WROOM-1 pinout** — verified against the official KiCad `RF_Module` symbol and Espressif pin numbering (pin 1 GND, 2 3V3, 3 EN, 13 IO19/USB_D−, 14 IO20/USB_D+, 27 IO0, 40/41 GND), all consistent. **Not** checked against the Espressif datasheet PDF this pass. Low risk — official library symbol, widely used — but it is a consistency check, not a datasheet check.
- **All passives, LEDs, TVS diodes, switches, connectors** — no MPNs, no datasheets. Values assessed for plausibility only. In particular D1/D2/D3/D5 (LESD5D5.0CT1G) TVS standoff/clamp voltage and capacitance, the LED forward voltages (W4), and the USB-C connector pin mapping are **unverified**.

## Suggested order of work

1. B1 (10 kΩ on TS), B4 (2 kΩ on PRETERM), W2 (ISET2 → +5V) — three wires in the BATTERY block, all datasheet-driven.
2. B3 — decide the power-switch topology; it changes the parts list.
3. W1 (255 k/51 k), W3 (delete R5/C6), W4 (check LED V_f).
4. B2 and B5 — footprints. Do these before starting layout, not during.
5. B6 — populate MPNs, then re-run with datasheet sync for a proper pass over the passives.
