# Test patterns

Canonical visual geometry for v1 Pico-generated patterns. Timings, polarity, and pads live in [hardware-design.md](hardware-design.md). PIO, DMA, packing, and `set_pixel` live in [firmware-plan.md](firmware-plan.md). Generators are in [`apps/pattern/main.c`](../apps/pattern/main.c); the RCA Indian Head raster is packed by [`tools/pack_indian_head.py`](../tools/pack_indian_head.py). PIO / packing live in [`video/`](../video/).

Status language: **Decided**, **Working hypothesis**, **Open**.

**Decided for v1:** analog setup starts on [`apps/cross60`](../apps/cross60/). Six drawings live in [`apps/pattern`](../apps/pattern/) on the **same four modes** (`m` 80/132, `r` 60/78, boot 60 Hz 80-col). USB: pattern keys `1`–`6`, plus `a`/`d`/`w`/`s`.

**Working hypothesis:** exact line/pixel coordinates for the six drawings, scaled to the current mode.

## Role

Firmware **phase 5**. [`apps/cross60`](../apps/cross60/) is the measure drawing. [`apps/pattern`](../apps/pattern/) is the six analog-setup pictures on the same 60/78 × 80/132 scanout ([`video/scanout.c`](../video/scanout.c)). This doc is the drawing spec and the HIL record, not the PIO how-to.

## Luminance and packing

Same 2-bit field as the hardware luminance table and the firmware `PixelColor` enum. MSB-first, 4 pixels/byte.

| Bits | V0 (dim) | V1 (normal) | Name |
| --- | --- | --- | --- |
| `00` | 0 | 0 | `PIXEL_OFF` |
| `01` | 1 | 0 | `PIXEL_DIM` |
| `10` | 0 | 1 | `PIXEL_NORMAL` |
| `11` | 1 | 1 | `PIXEL_BOLD` |

Use `set_pixel` / `clear_buffer` from the firmware plan. Do not invent a second packing helper.

## Standalone measure ([`apps/cross60`](../apps/cross60/))

This is the only drawing in active HIL. Boot **60 Hz 80-col** measure. USB CDC (`make monitor`):

| Key | Drawing | Analog use |
| --- | --- | --- |
| `4` meas (boot) | Bold raster box; normal grid; bold plus at center | Deflection (box vs bezel), cell pitch for linearity |
| `2` box | Raster-edge rectangle only | H/V size, overscan |
| `3` grid | Cell grid + plus | Linearity, pincushion |
| `1` plus | One-pixel H and V through center | Phase |
| `m` | Toggle 80-col / 132-col | Appendix B widths; H timing stays with column count |
| `r` | Toggle 60 Hz / 78 Hz | 416/523 vs 377/402; same H as the current column mode |
| `a`/`d` | H phase ±16 px (leading DMA zeros; later = right) | Digital H position without reflash |
| `w`/`s` | V back porch ±1 line (up/down) | Digital V position without reflash |
| `0` | hpad=0, vbp=mode default (51 @ 60 Hz, 8 @ 78 Hz) | Restore firmware default |

`r` also drives **GP4** high in 78 Hz (U6 pin 5 / **VR301** select). That is mode-select, not a signal-integrity pin. Lift the 8032 pin first if that MCU still drives it.

### HIL record (this MC5 / WY-120)

Bezel **24.6 cm × 19.0 cm**. Factory Section 3: **11 mm ±2 mm** each side, both rates; **size then linearity**. Pots: **VR302** 60 Hz V-size, **VR301** 78 Hz V-size, **VR303** V-linearity (**shared**), **VR201** H-hold, **L201** H-width.

**Analog order (decided).** Lock 60 Hz on the glass first (VR302, VR303, L201). Then switch `r` and fix 78 Hz in firmware. Do not reopen VR302/VR303/L201 for 78 Hz: VR303 is common, and VR302-min 60 Hz is already the factory 11 mm. A 12-line V-BP trim did **not** move a 3 cm top gap; extra VR303 with a short top ate the bottom (2.4 / 1.0 cm). Restoring 60 Hz analog restored 60 Hz and 78 Hz lost deflection again (3.5 / 1.6 cm at 338 lines).

**Clock (decided).** Pico **PLL_SYS 128.4 MHz** for all four `cross60` modes so USB CDC stays up. Do not switch the PLL at runtime. Do not go back to a 48 MHz crystal story for this app.

| Mode | Dot clock | Line | Active | `/HSYNC` | `/VSYNC` | Grid cells | Boxes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 60 Hz 80-col | 32.1 MHz (clkdiv 4) | 1024 | 800 × 416 | 113 FP / 111 sync / 0 BP | 50 FP / 6 sync / 51 BP | 40 × 26 | 20 × 16 |
| 60 Hz 132-col | ~47.988 MHz (clkdiv 2.675) | 1530 | 1188 × 416 | 176 / 166 / 0 | same 523-line V | 54 × 26 | 22 × 16 |
| 78 Hz 80-col | same 32.1 MHz as 80-col | 1024 | 800 × **377** | same 80-col H | 11 FP / 6 sync / 8 BP | 40 × **29** | 20 × 13 |
| 78 Hz 132-col | same ~48 MHz as 132-col | 1530 | 1188 × **377** | same 132-col H | same 402-line V | 54 × **29** | 22 × 13 |

78 Hz **keeps the current column H** so L201 width matches 60 Hz. Factory ASIC ran 78 Hz 80-col as 800/1530 @ 48 MHz; that would shrink the 80-col box on this HIL.

**Packing (decided).** 2 bpp MSB-first, 32-bit LE DMA, PIO shift-left: `set_pixel` stores at `(x/4) ^ 3`. An 80 px grid hid the swap (lines at `x % 16 == 0`). A 40 px grid showed repeating **24 then 56** px pairs. Do not add a second vertical at `x = width-1` on top of the box (that made the last 80-col cell 79 px). Interior grid starts at one cell in; the bold box owns the raster edges.

**60 Hz glass (decided).** Box **22.5 × 17.0 cm** (diag 28.3 cm) → 0.281 mm/px H, 0.409 mm/px V. Cells **~1.1 × 1.0 cm**. Pixels are not square; squaring with L201/VR302 would miss the 11 mm margins. VR302 at **minimum** is ~11 mm top and bottom. 5-dot earlier `/HSYNC` (111 vs 116) evened 14 vs 11 mm H toward 1 cm each side. Leave `vbp=51`. Both column modes filled the same sweep (20×16 vs 22×16 boxes).

**78 Hz glass (decided).** With 60 Hz pots left at the 11 mm setup, Appendix B **338** active (26 × 13 character cells) is short: **1 cm** L/R (H is fine; leave L201), V **3.5 cm** top / **1.6 cm** bottom (~13.9 cm tall vs ~16.8 cm at 60 Hz). 13 rows is odd: the plus through the middle row is expected, not a mid-screen seam.

Painting into that blanking has a hard stop. **390** active (13 × 30) with only 12 blank lines put the top box in retrace: first line missing at `vbp=2`, then a **hairline of the frame at the top-right** at `vbp=3`. That is unblanked flyback (left-to-right scan still climbing), not a drawing off-by-one in `draw_hline(0)`. This yoke needs ~0.5 ms after `/VSYNC` before video.

Settled raster: **377** active (13 × 29), 25-line blank (**8 BP / 11 FP / 6 sync**). `vbp=12` was 1.8 / 1.5 cm; `vbp=8` centers (~**1.65 cm** each). Grid **~1 cm squares**. CDC: `crt-cross60 78Hz 80col meas hpad=0 vbp=8 vfp=11 vsize78=1`.

| 78 Hz try | Blanking | Glass |
| --- | --- | --- |
| 338 / vbp=48 (Appendix B character raster) | 10 FP / 6 / 48 BP | 3.5 cm top / 1.6 cm bottom |
| 390 / vbp=2…3 | 12 total | Top line in retrace (missing, then top-right hair) |
| 377 / vbp=12 | 7 / 6 / 12 | 1.8 / 1.5 cm, top line visible |
| 377 / vbp=8 (**keep**) | 11 / 6 / 8 | centered ~1.65 cm; ~1 cm cells |

### Fold into `apps/pattern` (done)

[`apps/pattern`](../apps/pattern/) uses [`video/scanout.c`](../video/scanout.c): PLL 128.4 MHz, column H from `cross60`, 78 Hz **377 / 8 BP**, `(x/4)^3` packing, interior grid, GP4 in 78 Hz. USB `m`/`r` match the measure app. Boot is 60 Hz 80-col.

Drawings scale to the current raster:

| Drawing | How it follows the mode |
| --- | --- |
| Sync-squares | Outer box on `width-1`/`height-1`; 100 mm square from HIL fill (225×170 mm @ 60 Hz, 226×157 mm @ 78 Hz); 80×80 px box; plus at center |
| Crosshatch | Measure cells **40×26** / **54×26** / **40×29** / **54×29**; box owns edges |
| Intensity | Four bands of `height/4` |
| Focus | 26 rows of 10×16 (60 Hz) or 10×14 (78 Hz); 132-col 9-dot cells |
| Indian Head | Packed 800×338 letterboxed in the current raster |
| Full-on | Every active pixel bold |

`term` still uses the 338-line 144 MHz [`video/video.c`](../video/video.c) path. Demos share [`video/scanout.c`](../video/scanout.c).

## 78 Hz drawings ([`apps/pattern`](../apps/pattern/))

Same four modes as `cross60`. Coordinates below were written for Appendix B **10 × 13** (800 × 338); generators now scale from `scanout_width()` / `scanout_height()`. Character-cell focus uses 26 rows at 16 px (60 Hz) or 14 px (78 Hz).

| Pattern | Drawing | Analog use |
| --- | --- | --- |
| Sync-squares | Raster-edge box; 100 mm scale square; 80 × 80 px box; center cross | H/V phase, size, mm/px |
| Crosshatch | Bold overscan box; normal lines every 80 px and every 13 lines; bold center reticle | Size, centering, linearity, pincushion |
| Intensity bars | Four equal horizontal bands: off, dim, normal, bold | Brightness / contrast, bloom |
| Focus matrix | Dense `H` in 10 × 13 cells, 80 × 26 (center cell bold) | Center and corner focus |
| Indian Head | 4:3 RCA card letterboxed; V0/V1 side columns | Geometry, resolution, grayscale, channel ID |
| Full-on box | Every active pixel bold | Max beam current, overscan limits |

### Sync-squares

**Working hypothesis.** Nested outlines plus a short center cross. The outer box is the full 800 × 338 active raster. The **100 mm square** is sized from the 25 cm × 19 cm bezel (not from glass mm/px), so it is a physical target: 100/250 × 800 = **320 px** wide, 100/190 × 338 ≈ **178 px** tall. The 80 × 80 pixel box stays for mm/px. Center is `(400, 169)`.

Draw order: clear off, raster frame bold, 100 mm square bold, 80 × 80 normal, 3-pixel cross bold.

| Feature | Coordinates | Intensity |
| --- | --- | --- |
| Background | Full 800 × 338 | Off |
| Raster frame | `x = 0, 799` and `y = 0, 337` | Bold |
| 100 mm square | `x = 240…559`, `y = 80…257` (320 × 178; 100 × 100 mm if the raster fills the bezel) | Bold |
| 80 × 80 box | `x = 360…439`, `y = 129…208` | Normal |
| Center cross | `x = 388…412` and `y = 157…181`, 3 px thick at (400, 169) | Bold |

On the glass: measure the 100 mm square’s width and height as the scale baseline (pots will change it). Measure the cross to the four bezels for centering. The 80 × 80 still gives mm/px: inner width / 80 and inner height / 80. A missing left or top edge with a visible right/bottom edge is phase/porch, not H/V size.

```c
void generate_sync_squares_pattern(void) {
    clear_buffer(PIXEL_OFF);
    draw_rect_outline(0, 0, FRAME_WIDTH - 1, FRAME_HEIGHT - 1, PIXEL_BOLD);
    draw_centered_rect(400, 169, 320, 178, PIXEL_BOLD);
    draw_centered_rect(400, 169, 80, 80, PIXEL_NORMAL);
    draw_cross(400, 169, 12, PIXEL_BOLD);
}
```

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
2. Call one `generate_*` so `frame_buffer` is non-zero where the pattern needs light (boot: sync-squares).
3. Start the DMA loop (phase 4). CDC keys or BOOTSEL rewrite the same buffer while the previous frame finishes; a torn frame during the fill is acceptable.

On the bench, BOOTSEL cycles patterns with no host. On the Dev-Host, `make monitor` (or any USB CDC terminal) selects a pattern by key; a breakpoint or `gdb` peek at `frame_buffer` is enough to prove packing before the CRT is connected.

## Analog use (WY-120 first target)

After the harness is lifted at pads **V0**, **V1**, **V**, **H**, and **GND** and the 74AHCT125 is driving the load-side wires:

**Measured** on this MC5: bezel opening **24.6 cm × 19.0 cm** (W × H), ~4:3 (24.6/19 = 1.295 vs 4/3 = 1.333). Glass origin is the **upper-left bezel corner**; firmware `(0, 0)` is the upper-left of the *active raster*, which matches that corner only when left/top margins are zero.

| If the 78 Hz raster filled the bezel | Size |
| --- | --- |
| Outer frame | 24.6 cm × 19.0 cm |
| Horizontal pitch | 246 mm / 800 = 0.3075 mm/px |
| Vertical pitch | 190 mm / 338 ≈ 0.562 mm/px |
| Inner 80 × 80 | **2.46 cm × 4.50 cm** (tall rectangle, ~1:1.83) |
| Visual square | ~144 × 80 px → 4.43 cm × 4.50 cm |

**Measured** on this MC5 with sync-squares, no chassis pot changes:

| | Before V-porch DMA fix | After (48-line BP) |
| --- | --- | --- |
| Top | Cut off (overscan) | **30 mm** (frame fully visible) |
| Bottom | 40 mm | **20 mm** |
| Left | 87 mm | 87 mm |
| Right | 12 mm | See H note below |
| Inner 80 × 80 | 17 × 33 mm (~1:1.94) | **17 × 32 mm (~1:1.88)** |

Vertical from the new gaps: active height 190 − 30 − 20 = **140 mm**. Center scale 32 mm / 80 px → 338 lines ≈ 135 mm (agrees). That 338-line picture is short once 60 Hz analog is locked; **do not open VR301/VR303 to chase it** if 60 Hz must stay at 11 mm — [`apps/cross60`](../apps/cross60/) fills in firmware (377 lines, ~1.65 cm V margins). See the HIL record above.

Horizontal: a 17 mm inner box is still 80/800 of ~**170 mm** at center scale (same as before the porch change; H PIO did not move). A right gap of **11.0 cm** with left 8.7 cm would make the outer frame only 5.3 cm, which cannot contain a 1.7 cm / 80 px box at that scale. Treat **11.0 cm vs 1.10 cm** as a possible mix-up with the earlier 12 mm right gap; measure left-vertical to right-vertical as one length. **L201** expands H only after that width is confirmed; **VR201** centers it.

**Measured** with [`apps/cross60`](../apps/cross60/) measure pattern (bold raster box, `hpad=0` `vbp=51`):

| | As found | VR302 at minimum |
| --- | --- | --- |
| Box vs left bezel | **1.4 cm** | (H unchanged) |
| Box vs right bezel | **1.1 cm** | (H unchanged) |
| Box vs top bezel | **0.3 cm** | **~11 mm** |
| Box vs bottom bezel | **0.3 cm** | **~11 mm** |

V phase was already centered. **VR302 min hits the factory 11 mm ±2 mm** top/bottom (Section 3); there is no further analog shrink. 416 lines then span ~16.8 cm. Leave V porch at 51. H 5-dot earlier `/HSYNC` (111-dot pulse) is the flashed default so `a` is not required to even 14 vs 11 mm.

The 78 Hz 3-pixel cross looking like an “H” was the stroke (`x = 399` and `401` with a missing `400`), not a failed `out pins, 2`.

| Check | Expected |
| --- | --- |
| Sync-squares frame | Raster edges on the glass; 100 mm square → 100 × 100 mm at fill; cross at bezel center |
| Crosshatch box | Visible on the overscan bounds; bold reticle at bezel center |
| Grid cells | Equal 80 × 13 steps; use for H/V size, phase, linearity, pincushion |
| Intensity bars | Four distinct levels, no smear or bloom into neighbors |
| Focus matrix | Sharp in the center; corners show yoke/focus limits |
| Indian Head | Circles round; wedges show H/V resolution; left bars are three distinct levels |
| IC401 / neck | 0 V / +5 V on V0/V1 matching the luminance table |

Chassis pots on this MC5: **VR301** 78 Hz V-size, **VR302** 60 Hz V-size (**min ≈ 11 mm** top/bottom on this tube), **VR303** V-linearity, **VR201** H-hold, **L201** H-width (11 mm ±2 mm each side). Do not work a powered chassis until the anode-cap discharge path is known.

## Open

- Later key emulation (`Ctrl+Shift+F1` … `F4`) if a keyboard path is added; CDC and BOOTSEL are the analog-setup UI.
- Fold remaining 338-line drawings in [`apps/term`](../apps/term/) onto `video/scanout.c`.
- 132-column focus matrix glyph margins if 9×14 is tight.
