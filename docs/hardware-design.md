# Hardware design

Canonical hardware, signal, and timing notes for replacing U4 with an RP2040. Firmware phasing lives in [firmware-plan.md](firmware-plan.md).

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

PIO state-machine assignment is **Open** (see [firmware plan](firmware-plan.md)). The diagram uses the CMake-aligned three-SM recommendation.

```text
[ RP2040 SRAM Framebuffer ] (800x338 @ 2-bit, 67.6 KB)
      ||
      v (Paced DMA)
[ PIO SM0 (Pixel) ] ======> [ 74AHCT125 ] ==> Coax Red/White ==> IC401 Video Amp
[ PIO SM1 (/HSYNC)] ======> [ 74AHCT125 ] ==> TP1            ==> H-Deflection
[ PIO SM2 (/VSYNC)] ======> [ 74AHCT125 ] ==> Topside jumper ==> V-Deflection
  (144 MHz sys_clk / 3 = 48 MHz dot clock)
```

The original CPU and RAM bus do not need to be functional. Onboard XO-10 can sit idle; the RP2040 generates its own 48.000 MHz dot clock.

## Signals

### Pinout and polarity

| Signal | U4 pin | Polarity | Nominal rate | Path |
| --- | --- | --- | --- | --- |
| /HSYNC | 53 | Active-low TTL | 31.356 kHz (78 Hz mode) | TP1 → horizontal deflection |
| /VSYNC | 59 | Active-low TTL | 78.0 Hz (first target) | Pin 59 or topside jumper → vertical deflection |
| V0 / Dim | 64 | Active-high TTL | Pixel rate | 100 ohm R404, white coax (R21) → IC401 |
| V1 / Normal | 61 | Active-high TTL | Pixel rate | 100 ohm R402, red coax (R18) → IC401 |

R404 and R402 are pull-downs at the IC401 inputs. Driving them from 5 V level-shifted RP2040 outputs gives four brightness levels.

### 2-bit luminance

| V0 (White / R21 / Dim) | V1 (Red / R18 / Normal) | Beam current | Pixel state |
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
| GPIO 0 (V0) | 1A pin 2 → 1Y pin 3 | White coax (R21) → R404 → IC401 |
| GPIO 1 (V1) | 2A pin 5 → 2Y pin 6 | Red coax (R18) → R402 → IC401 |
| GPIO 2 (/HSYNC) | 3A pin 9 → 3Y pin 8 | TP1 (horizontal deflection) |
| GPIO 3 (/VSYNC) | 4A pin 12 → 4Y pin 11 | Topside /VSYNC jumper |
| GND | Pin 7 and all /OE | Common ground |

```text
Pico GPIO (3.3V)           74AHCT125 (VCC = 5V)              CRT Input Stage
----------------            -------------------              ---------------
GPIO 0 (V0 Pixel)    --->   1A (Pin 2)  -> 1Y (Pin 3)   --->  White Coax (R21) -> R404 -> IC401
GPIO 1 (V1 Pixel)    --->   2A (Pin 5)  -> 2Y (Pin 6)   --->  Red Coax (R18)   -> R402 -> IC401
GPIO 2 (/HSYNC)      --->   3A (Pin 9)  -> 3Y (Pin 8)   --->  TP1 (Horizontal Deflection)
GPIO 3 (/VSYNC)      --->   4A (Pin 12) -> 4Y (Pin 11)  --->  Topside VSYNC / Jumper
GND                  --->   GND (Pin 7), Output Enables (/OE) to GND
```

## Isolation and injection

Prevent contention with U4 before driving the analog boards.

1. **Video isolation:** Lift one leg of R402 / R404 on the ASIC side, or disconnect the 3-wire coax at the mainboard and tap the harness.
2. **/HSYNC tap:** TP1 on the mainboard.
3. **/VSYNC tap:** U4 pin 59 (30 AWG) or the matching topside J-series jumper (confirm with continuity).
4. **Coax:** V0 dim → white (R21); V1 normal → red (R18).
5. **Clock:** Leave XO-10 idle.

## Video timings

### 78 Hz (first target)

**Working hypothesis.** Copied from the planning notes; confirm on a working WY-120 / MC5 with a scope before treating as measured fact.

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
| **Total line** | **1530** | **31.875 us (fH = 31.356 kHz)** |

During active display, /HSYNC stays high; it pulses low for the 140-dot sync interval.

#### Vertical (/VSYNC)

| Stage | Scanlines | Time |
| --- | --- | --- |
| Active display | 338 (26 × 13) | 10.774 ms |
| Front porch | 12 | 0.383 ms |
| Sync pulse (active-low) | 4 | 0.128 ms |
| Back porch | 48 | 1.530 ms |
| **Total frame** | **402** | **12.820 ms (fV = 78.0 Hz)** |

### 60 Hz (later, optional)

**Open.** Named as a follow-on mode only. Do not invent porch/sync counts here. Capture timings from a 60 Hz WY-120 raster or from the maintenance manual before implementing.

Older notes mentioned HSYNC in a ~31.5–38 kHz band and “60 Hz or 78 Hz”. For v1, lock the 78 Hz table above.

## Manual cross-references

Wyse WY-120 Maintenance Manual, document **880491-01**:

| Location | What it covers |
| --- | --- |
| Table 4-2 | 211009 ASIC signal definitions (pins 53, 59, 61, 64) |
| Figure 4-17 | Video amp: V0/V1 through damping resistors into R404/R402 and IC401 |
| Section 6 | Schematics; buffering between the ASIC and header P5 |

## Diagnostic patterns (hardware use)

These are visual targets for analog setup, not firmware how-to (see [firmware-plan.md](firmware-plan.md)):

- **Crosshatch:** overscan border, vertical lines every 80 pixels, horizontal bars every 13 lines (size, centering, linearity, pincushion).
- **Intensity bars:** four bands `00` / `01` / `10` / `11` (brightness and contrast).
- **Focus matrix:** dense character grid (flyback / neck focus).
- **Full-on box:** every pixel on (beam current and overscan limits).
