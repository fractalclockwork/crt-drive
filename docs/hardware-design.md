# Hardware design

Canonical hardware, signal, and timing notes for replacing U4 with an RP2040. Firmware phasing lives in [firmware-plan.md](firmware-plan.md). The Pico + 74AHCT125 carrier is the KiCad protoboard in [kicad/crt-drive/](kicad/crt-drive/); power nets are in [kicad/power_supply.md](kicad/power_supply.md).

Status language used below: **Decided**, **Working hypothesis**, **Open**.

## PCB identity

**Decided.** Visual inspection and schematic check: the Link MC5 main logic board is the Wyse WY-120 architecture (`© 1993 WYSE` PCB marking).

![PCB](PCB.svg)

### Component map

| Component ID | Part Number | Package | Role |
| --- | --- | --- | --- |
| U4 (ASIC) | Wyse 211009-02 | QFP-100 | Display controller / CRTC / timing |
| MCU | Intel P80C32 | DIP-40 | 8051-family CPU (emulation, I/O) |
| Firmware ROM | 27C512-12 (`VER 3.04`) | DIP-32 | 64 KB EPROM |
| VRAM (x2) | Winbond W2465-70LL | DIP-28 | 8K x 8 SRAM (16 KB total) |
| Buffer | SN74LS377N | DIP-20 | Octal DFF (attribute / control) |
| Clock | XO-10 | Can | 48.000 MHz master oscillator |
| Video buffer | IC401 | DIP / SOIC | Input stage for CRT video amp |

## Topology

### Legacy

```text
[ P80C32 MCU ] <== Address/Data ==> [ 27C512 EPROM ]
      ||
      v
[ Wyse 211009-02 ASIC ] <== 8-bit ==> [ Winbond W2465 SRAM x2 ]
 (48.0 MHz Crystal)
      ||
      +==> /HSYNC (Pin 53) =======> [ Horizontal Deflection ]
      +==> /VSYNC (Pin 59) =======> [ Vertical Deflection ]
      +==> Video 0 / Dim (Pin 64) => [ 100 ohm ] ==> Coax (White) ==> [ IC401 ] ==> [ CRT Cathode ]
      +==> Video 1 / Norm (Pin 61)=> [ 100 ohm ] ==> Coax (Red)   ==> [ IC401 ] ==> [ CRT Cathode ]
```

### RP2040 replacement

**Decided** injection path: solder-side pads labeled V0, V1, V, H, and GND next to U4 ([signals_pcb.png](signals_pcb.png)). PIO state-machine assignment is **Open** (see [firmware plan](firmware-plan.md)). The diagram uses the CMake-aligned three-SM recommendation.

```text
[ RP2040 SRAM Framebuffer ] (800x338 @ 2-bit, 67.6 KB)
      ||
      v (Paced DMA)
[ PIO SM0 (Pixel) ] ======> [ 74AHCT125 ] ==> pads V0 / V1 --> neck board
[ PIO SM1 (/HSYNC)] ======> [ 74AHCT125 ] ==> pad H         --> H-deflection
[ PIO SM2 (/VSYNC)] ======> [ 74AHCT125 ] ==> pad V         --> V-deflection
  (144 MHz sys_clk / 3 = 48 MHz dot clock)
```

The original CPU and RAM bus do not need to be functional. Onboard XO-10 is unused; the RP2040 generates its own 48.000 MHz dot clock.

## Signals

### Pinout and polarity

U4 pin numbers identify the ASIC nets. Physical attach is the labeled pads, not flywires to the QFP.

| Signal | U4 pin | Polarity | Nominal rate | Path |
| --- | --- | --- | --- | --- |
| /HSYNC | 53 | Active-low TTL | 31.373 kHz (78 Hz mode) | Solder-side pad **H** → horizontal deflection |
| /VSYNC | 59 | Active-low TTL | 78.041 Hz (first target) | Solder-side pad **V** → vertical deflection |
| V0 / Dim | 64 | Active-high TTL | Pixel rate | Solder-side pad **V0** → neck board |
| V1 / Normal | 61 | Active-high TTL | Pixel rate | Solder-side pad **V1** → neck board |

R18 and R21 sit next to the V0/V1 pads (on-board damping). Driving the V0/V1 pads from 5 V level-shifted RP2040 outputs gives four brightness levels.

### 2-bit luminance

| V0 (Dim) | V1 (Normal) | Beam current | Pixel state |
| --- | --- | --- | --- |
| 0 (0 V) | 0 (0 V) | Off | Blank / black |
| 1 (+5 V) | 0 (0 V) | Low | Dim text |
| 0 (0 V) | 1 (+5 V) | Medium | Normal text |
| 1 (+5 V) | 1 (+5 V) | High | Bold / high intensity |

## Level shift (3.3 V to 5 V TTL)

RP2040 GPIO is 3.3 V CMOS. CRT drive expects 5 V TTL, so translation is required.

**Decided (recommended part):** 74AHCT125 quad bus buffer, VCC = +5 V.

| Parameter | 74AHCT125 |
| --- | --- |
| VIH | 2.0 V (accepts 3.3 V Pico highs) |
| Propagation delay | ~3.8 ns (fine at 48 MHz) |

**Alternative:** 74HCT245 if more channels are needed.

Tie all /OE pins to GND so outputs stay enabled.

### GPIO map

| Pico GPIO (3.3 V) | 74AHCT125 | CRT side |
| --- | --- | --- |
| GPIO 0 (V0) | 1A pin 2 → 1Y pin 3 | Pad **V0** (neck video, dim) |
| GPIO 1 (V1) | 2A pin 5 → 2Y pin 6 | Pad **V1** (neck video, normal) |
| GPIO 2 (/HSYNC) | 3A pin 9 → 3Y pin 8 | Pad **H** |
| GPIO 3 (/VSYNC) | 4A pin 12 → 4Y pin 11 | Pad **V** |
| GND | Pin 7 and all /OE | Pad **GND** (common with Pico) |
| Pico VSYS / AHCT VCC | — | Logic-board **+5 V** (on-board; no pad ID claimed) |

```text
Pico GPIO (3.3V)           74AHCT125 (VCC = 5V)              CRT pads (solder side, near U4)
----------------            -------------------              --------------------------------
GPIO 0 (V0 Pixel)    --->   1A (Pin 2)  -> 1Y (Pin 3)   --->  V0
GPIO 1 (V1 Pixel)    --->   2A (Pin 5)  -> 2Y (Pin 6)   --->  V1
GPIO 2 (/HSYNC)      --->   3A (Pin 9)  -> 3Y (Pin 8)   --->  H
GPIO 3 (/VSYNC)      --->   4A (Pin 12) -> 4Y (Pin 11)  --->  V
GND                  --->   GND (Pin 7), /OE to GND     --->  GND
Logic-board +5 V     --->   J1 → U1 VCC (Pin 14); D1 → Pico VSYS
```

### Injector protoboard (Pico carrier)

**Decided.** Hand-wired through-hole protoboard (KiCad 10), not a fabbed 2-layer PCB. Schematic and jumper layout: [kicad/crt-drive/](kicad/crt-drive/). Power: [kicad/power_supply.md](kicad/power_supply.md).

| Ref | Part | Role |
| --- | --- | --- |
| A1 | Raspberry Pi Pico | RP2040; VSYS on pin 39, GND tap pin 38 |
| U1 | 74AHCT125 | 3.3 V → 5 V TTL, `/OE` tied to GND |
| J1 | 1×2 | Logic-board **+5 V** / **GND** |
| J2 | 1×5 | Harness **V0 V1 H V GND** (after series resistors) |
| R1, R2 | 100 Ω | Damping on V0 / V1 |
| R3, R4 | 47 Ω | Damping on H / V (47–68 Ω band) |
| D1 | 1N5817 | +5 V → VSYS; blocks USB back-feed |
| FB1 | 100 Ω @ 100 MHz | Isolates `+5V_BUFFER` for U1 |

## Isolation and injection

**Decided.** The guessed TP1 / U4 pin-59 flywire / coax / lift-R402/R404 path was wrong. Factory jumpers as previously described were not present. Access is the solder side of the logic PCB (a few screws).

![Signal pads V0, V1, V, H, GND next to U4](signals_pcb.png)

Solder side under U4: silkscreen **V0**, **V1**, **V**, **H**, **GND**. The photo shows V1, V0, and GND desoldered; those wires ran to the CRT neck board. H and V wires were lifted later and spliced.

Prevent contention with U4 by lifting the factory harness at the pads, then driving the load-side wires:

1. Remove the logic PCB (few screws).
2. Locate labeled pads V0, V1, V, H, and GND near U4.
3. Lift the factory wires at V0, V1, and GND (neck-board video and ground). Lift H and V the same way. Splice the 74AHCT125 outputs into those load-side wires so U4 is isolated.
4. Power the Pico and 74AHCT125 from logic-board +5 V and GND.

## Video timings

### 78 Hz (first target)

**Working hypothesis** for porch and sync widths. Line and frame *rates* follow 48.000 MHz / 1530 dots / 402 lines and match the maintenance manual (Appendix B: 31.372 kHz, 78.041 Hz). Confirm porches on a working WY-120 / MC5 with a scope before freezing PIO delays.

Character cell assumed: 80 columns × 10 dots, 26 rows × 13 scanlines.

| Clock | Value |
| --- | --- |
| `sys_clk` | 144.000 MHz |
| PIO clock divider | 3.00 |
| Dot clock | 48.000 MHz (20.833 ns/dot) |

#### Horizontal (/HSYNC)

| Stage | Dots | Time |
| --- | --- | --- |
| Active video | 800 (80 × 10) | 16.667 us |
| Front porch | 160 | 3.333 us |
| Sync pulse (active-low) | 140 | 2.917 us |
| Back porch | 430 | 8.958 us |
| **Total line** | **1530** | **31.875 us (fH = 31.373 kHz)** |

During active display, /HSYNC stays high; it pulses low for the 140-dot sync interval.

#### Vertical (/VSYNC)

| Stage | Scanlines | Time |
| --- | --- | --- |
| Active display | 338 (26 × 13) | 10.774 ms |
| Front porch | 12 | 0.383 ms |
| Sync pulse (active-low) | 4 | 0.128 ms |
| Back porch | 48 | 1.530 ms |
| **Total frame** | **402** | **12.813 ms (fV = 78.041 Hz)** |

### 60 Hz (later, optional)

**Open** porch and sync counts. Appendix B already gives: same ~31.37 kHz H, **59.999 Hz** V, **416** active lines (26 × 16), 800 dots at 80 columns. Do not invent porches; measure a 60 Hz raster or derive the remaining blanking from a scope capture.

For v1, lock the 78 Hz table above.

## Manual cross-references

Wyse WY-120 Maintenance Manual, document **880491-01**:

| Location | What it covers |
| --- | --- |
| Table 4-2 | 211009 ASIC signal definitions (pins 53, 59, 61, 64) |
| Figure 4-17 | Video amp: V0/V1 through damping resistors into R404/R402 and IC401 |
| Section 6 | Schematics; buffering between the ASIC and header P5 |
| Appendix B | Scan frequency 31.372 kHz; 78.041 Hz / 59.999 Hz; 800×338 @ 78 Hz |

## Diagnostic patterns (hardware use)

These are visual targets for analog setup. Drawing coordinates live in [test-pattern-design.md](test-pattern-design.md); PIO / DMA how-to is in [firmware-plan.md](firmware-plan.md).

- **Crosshatch:** bold overscan box, vertical every 80 pixels, horizontal every 13 lines, bold center reticle (size, centering, linearity, pincushion).
- **Intensity bars:** four bands `00` / `01` / `10` / `11` (brightness and contrast).
- **Focus matrix:** dense `H` or `E` in 10 × 13 cells (flyback / neck focus).
- **Full-on box:** every pixel bold (beam current and overscan limits).
