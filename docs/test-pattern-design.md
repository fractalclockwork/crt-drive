# Test patterns

Canonical visual geometry for v1 Pico-generated patterns. Timings, polarity, and pads live in [hardware-design.md](hardware-design.md). PIO, DMA, packing, and `set_pixel` live in [firmware-plan.md](firmware-plan.md). Crosshatch is in [`main.c`](../main.c); the C below matches that generator. Intensity, focus, and full-on are not in the tree yet.

Status language: **Decided**, **Working hypothesis**, **Open**.

**Decided for v1:** four patterns on the 78 Hz active raster (800 × 338 @ 2 bpp). Pico draws them; factory keyboard chords are not required. Optional USB-CDC switching is later UI.

**Working hypothesis:** exact line/pixel coordinates below. Confirm on the CRT after isolation at pads V0, V1, V, H, and GND.

## Role

Firmware **phase 5**. Crosshatch fills `frame_buffer` before the DMA loop starts. Analog setup uses these as visual targets; this doc is the drawing spec, not the PIO how-to.

First target is the Link MC5 / WY-120 78 Hz mode. Other CRT or TV profiles reuse the same pattern *ideas* with a different active size and output stage.

## Luminance and packing

Same 2-bit field as the hardware luminance table and the firmware `PixelColor` enum. MSB-first, 4 pixels/byte.

| Bits | V0 (dim) | V1 (normal) | Name |
| --- | --- | --- | --- |
| `00` | 0 | 0 | `PIXEL_OFF` |
| `01` | 1 | 0 | `PIXEL_DIM` |
| `10` | 0 | 1 | `PIXEL_NORMAL` |
| `11` | 1 | 1 | `PIXEL_BOLD` |

Use `set_pixel` / `clear_buffer` from the firmware plan. Do not invent a second packing helper.

## Patterns

Character cell for 78 Hz, 80 columns: **10 × 13** (80 × 10 = 800 dots, 26 × 13 = 338 lines). Grid lines follow that cell, not a 26-line “square” pitch.

| Pattern | Drawing | Analog use |
| --- | --- | --- |
| Crosshatch | Bold overscan box; normal lines every 80 px and every 13 lines; bold center reticle | Size, centering, linearity, pincushion |
| Intensity bars | Four equal horizontal bands: off, dim, normal, bold | Brightness / contrast, bloom |
| Focus matrix | Dense `H` or `E` in 10 × 13 cells, 80 × 26 | Center and corner focus |
| Full-on box | Every active pixel bold | Max beam current, overscan limits |

### Crosshatch

**Working hypothesis.** Draw in this order so later strokes win: clear off, interior grid normal, overscan box bold, center reticle bold.

| Feature | Coordinates | Intensity |
| --- | --- | --- |
| Background | Full 800 × 338 | Off |
| Vertical grid | `x = 80, 160, …, 720` | Normal |
| Horizontal grid | `y = 13, 26, …, 325` | Normal |
| Overscan box | `x = 0, 799` and `y = 0, 337` | Bold |
| Center reticle | `x = 399…401` full height; `y = 168…170` full width (center 400, 169) | Bold |

`400` is 5 × 80. `169` is 13 × 13, so the reticle sits on a character-row line.

```c
void generate_crosshatch_pattern(void) {
    clear_buffer(PIXEL_OFF);

    for (uint16_t x = 80; x < FRAME_WIDTH - 1; x += 80) {
        for (uint16_t y = 0; y < FRAME_HEIGHT; y++)
            set_pixel(x, y, PIXEL_NORMAL);
    }
    for (uint16_t y = 13; y < FRAME_HEIGHT - 1; y += 13) {
        for (uint16_t x = 0; x < FRAME_WIDTH; x++)
            set_pixel(x, y, PIXEL_NORMAL);
    }

    for (uint16_t x = 0; x < FRAME_WIDTH; x++) {
        set_pixel(x, 0, PIXEL_BOLD);
        set_pixel(x, FRAME_HEIGHT - 1, PIXEL_BOLD);
    }
    for (uint16_t y = 0; y < FRAME_HEIGHT; y++) {
        set_pixel(0, y, PIXEL_BOLD);
        set_pixel(FRAME_WIDTH - 1, y, PIXEL_BOLD);
    }

    const uint16_t cx = FRAME_WIDTH / 2;   /* 400 */
    const uint16_t cy = FRAME_HEIGHT / 2;  /* 169 */
    for (uint16_t y = 0; y < FRAME_HEIGHT; y++) {
        set_pixel(cx - 1, y, PIXEL_BOLD);
        set_pixel(cx,     y, PIXEL_BOLD);
        set_pixel(cx + 1, y, PIXEL_BOLD);
    }
    for (uint16_t x = 0; x < FRAME_WIDTH; x++) {
        set_pixel(x, cy - 1, PIXEL_BOLD);
        set_pixel(x, cy,     PIXEL_BOLD);
        set_pixel(x, cy + 1, PIXEL_BOLD);
    }
}
```

### Intensity bars

Four bands, each 84 or 85 lines (`338 / 4`). Top to bottom: off, dim, normal, bold. Pack a whole line with `memset` as in the firmware plan.

### Focus matrix

Fill the 80 × 26 character grid with `H` or `E` at normal (or bold in the center cell). Glyph lives inside the 7 × 10 inner matrix of each 10 × 13 cell; leave the cell margin blank. A 132-column (9 × 13) variant is later if needed.

### Full-on box

`clear_buffer(PIXEL_BOLD)`. Do not leave this up longer than needed; it is a beam-current and power-supply stress pattern.

## Load order

1. Init clock, PIO, and pins (phases 1–3).
2. Call one `generate_*` so `frame_buffer` is non-zero where the pattern needs light.
3. Start the DMA loop (phase 4). Restart DMA after switching patterns, or write the new pattern into the same buffer while the previous frame finishes.

On the Dev-Host, a breakpoint or `gdb` peek at `frame_buffer` is enough to prove packing before the CRT is connected.

## Analog use (WY-120 first target)

After the harness is lifted at pads **V0**, **V1**, **V**, **H**, and **GND** and the 74AHCT125 is driving the load-side wires:

| Check | Expected |
| --- | --- |
| Crosshatch box | Visible on the overscan bounds; bold reticle at bezel center |
| Grid cells | Equal 80 × 13 steps; use for H/V size, phase, linearity, pincushion |
| Intensity bars | Four distinct levels, no smear or bloom into neighbors |
| Focus matrix | Sharp in the center; corners show yoke/focus limits |
| IC401 / neck | 0 V / +5 V on V0/V1 matching the luminance table |

Chassis pot names vary; use whatever H-size, H-phase, V-size, V-pos, and pincushion controls the MC5 actually has. Do not work a powered chassis until the anode-cap discharge path is known.

## Open

- USB-CDC (or later key emulation) to switch patterns — not required for first light.
- 60 Hz and non–WY-120 profiles: same four patterns, new `FRAME_WIDTH` / `FRAME_HEIGHT`.
- 132-column focus matrix (9 × 13 cell) — later.
