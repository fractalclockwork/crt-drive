# Test patterns

Canonical visual geometry for v1 Pico-generated patterns. Timings, polarity, and pads live in [hardware-design.md](hardware-design.md). PIO, DMA, packing, and `set_pixel` live in [firmware-plan.md](firmware-plan.md). Generators are in [`pattern/main.c`](../pattern/main.c); the RCA Indian Head raster is packed by [`tools/pack_indian_head.py`](../tools/pack_indian_head.py). PIO / packing live in [`video/`](../video/).

Status language: **Decided**, **Working hypothesis**, **Open**.

**Decided for v1:** five patterns on the 78 Hz active raster (800 × 338 @ 2 bpp). Pico draws them; factory keyboard chords are not required. USB CDC selects a pattern (`1`/`c` crosshatch, `2`/`i` intensity, `3`/`f` focus, `4`/`n` indian-head, `5`/`o` full-on). A short **BOOTSEL** press cycles that same order. Boot default is crosshatch. Hold BOOTSEL while plugging USB to enter the bootloader as usual.

**Working hypothesis:** exact line/pixel coordinates below. Confirm on the CRT after isolation at pads V0, V1, V, H, and GND.

## Role

Firmware **phase 5**. One `generate_*` fills `frame_buffer` before the DMA loop starts; CDC keys and BOOTSEL rewrite the same buffer while DMA continues. Analog setup uses these as visual targets; this doc is the drawing spec, not the PIO how-to.

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
| Focus matrix | Dense `H` in 10 × 13 cells, 80 × 26 (center cell bold) | Center and corner focus |
| Indian Head | 4:3 RCA card letterboxed; V0/V1 side columns | Geometry, resolution, grayscale, channel ID |
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

**Working hypothesis.** Four bands, 84 / 84 / 85 / 85 lines (`338 = 4×84 + 2`). Top to bottom: off, dim, normal, bold. Pack the 200 active bytes with `memset`; keep the 4-byte trailing off word.

```c
void generate_intensity_bars(void) {
    static const PixelColor bands[4] = {
        PIXEL_OFF, PIXEL_DIM, PIXEL_NORMAL, PIXEL_BOLD
    };
    const uint16_t base = FRAME_HEIGHT / 4;
    const uint16_t rem = FRAME_HEIGHT % 4;
    uint16_t y = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t h = (uint16_t)(base + (i >= (4 - (int)rem) ? 1 : 0));
        uint16_t y_end = (uint16_t)(y + h);
        for (; y < y_end; y++) {
            uint8_t packed_byte = (uint8_t)((bands[i] << 6) | (bands[i] << 4)
                                          | (bands[i] << 2) | bands[i]);
            memset(frame_buffer[y], packed_byte, BYTES_PER_LINE);
            memset(&frame_buffer[y][BYTES_PER_LINE], 0, LINE_STRIDE - BYTES_PER_LINE);
        }
    }
}
```

### Focus matrix

**Working hypothesis.** Fill the 80 × 26 character grid with `H` at normal, bold in the center cell `(39, 12)` (0-based; top-left of that cell is 390, 156). Glyph lives in the 7 × 10 inner matrix at offset `(1, 1)` of each 10 × 13 cell; leave the 1-pixel left/top and 2-pixel right/bottom margin blank. A 132-column (9 × 13) variant is later if needed.

```c
static const uint8_t glyph_h[10] = {
    0b1000001, 0b1000001, 0b1000001, 0b1000001, 0b1111111,
    0b1000001, 0b1000001, 0b1000001, 0b1000001, 0b1000001,
};

void generate_focus_matrix(void) {
    clear_buffer(PIXEL_OFF);
    const uint16_t cx = 39;
    const uint16_t cy = 12;
    for (uint16_t row = 0; row < 26; row++) {
        for (uint16_t col = 0; col < 80; col++) {
            PixelColor color = (col == cx && row == cy) ? PIXEL_BOLD : PIXEL_NORMAL;
            uint16_t ox = col * 10 + 1;
            uint16_t oy = row * 13 + 1;
            for (uint16_t r = 0; r < 10; r++) {
                for (uint16_t c = 0; c < 7; c++) {
                    if (glyph_h[r] & (1u << (6 - c)))
                        set_pixel(ox + c, oy + r, color);
                }
            }
        }
    }
}
```

### Full-on box

`clear_buffer(PIXEL_BOLD)` (trailing off bytes stay blank). Do not leave this up longer than needed; it is a beam-current and power-supply stress pattern.

### Indian Head

**Working hypothesis.** Wikimedia Commons SVG ([`assets/indian_head/`](../assets/indian_head/SOURCE.md)), public domain in the US. The 78 Hz raster is 800 × 338 (~2.37:1); the card is 4:3, so it is letterboxed to **448 × 336** at `(176, 1)` rather than stretched. White paper becomes off; black ink becomes bold; gray wedges invert onto dim/normal/bold. The portrait ellipse stays a positive 4-level image so hair stays dark.

Side columns use the leftover width for analog identity of **V0** and **V1**:

| Region | Drawing | Analog use |
| --- | --- | --- |
| Left `x = 16…56` | Solid dim (V0 only) + 2 px burst | V0 path, bloom |
| Left `x = 64…104` | Solid normal (V1 only) + 2 px burst | V1 path, bloom |
| Left `x = 112…160` | Solid bold (V0+V1) + 2 px burst | Peak white |
| Right bands | Bold 1/2/4/8 px bursts; dim and normal 2 px bursts; vertical Nyquist | 48 MHz bandwidth, per-channel MTF |
| Outer box | Bold `x = 0, 799` and `y = 0, 337` | Overscan |

`generate_indian_head_pattern` copies [`indian_head_packed`](../assets/indian_head/indian_head_pattern.h) into `frame_buffer` and clears the trailing off word. Rebuild the header on the Dev-Host (ImageMagick + Pillow), not inside `pico-dev`: `python3 tools/pack_indian_head.py`.

## Load order

1. Init clock, PIO, and pins (phases 1–3).
2. Call one `generate_*` so `frame_buffer` is non-zero where the pattern needs light (boot: crosshatch).
3. Start the DMA loop (phase 4). CDC keys or BOOTSEL rewrite the same buffer while the previous frame finishes; a torn frame during the fill is acceptable.

On the bench, BOOTSEL cycles patterns with no host. On the Dev-Host, `make serial` (or any USB CDC terminal) selects a pattern by key; a breakpoint or `gdb` peek at `frame_buffer` is enough to prove packing before the CRT is connected.

## Analog use (WY-120 first target)

After the harness is lifted at pads **V0**, **V1**, **V**, **H**, and **GND** and the 74AHCT125 is driving the load-side wires:

| Check | Expected |
| --- | --- |
| Crosshatch box | Visible on the overscan bounds; bold reticle at bezel center |
| Grid cells | Equal 80 × 13 steps; use for H/V size, phase, linearity, pincushion |
| Intensity bars | Four distinct levels, no smear or bloom into neighbors |
| Focus matrix | Sharp in the center; corners show yoke/focus limits |
| Indian Head | Circles round; wedges show H/V resolution; left bars are three distinct levels |
| IC401 / neck | 0 V / +5 V on V0/V1 matching the luminance table |

Chassis pot names vary; use whatever H-size, H-phase, V-size, V-pos, and pincushion controls the MC5 actually has. Do not work a powered chassis until the anode-cap discharge path is known.

## Open

- Later key emulation (`Ctrl+Shift+F1` … `F4`) if a keyboard path is added; CDC and BOOTSEL are the analog-setup UI.
- 60 Hz and non–WY-120 profiles: same patterns, new `FRAME_WIDTH` / `FRAME_HEIGHT`.
- 132-column focus matrix (9 × 13 cell) — later.
