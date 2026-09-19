# Firmware plan

RP2040 implementation plan for the CRT drive replacement. Timings, polarity, and GPIO map live in [hardware-design.md](hardware-design.md). Do not treat the code below as repo sources; `main.c` and the `.pio` files are not in the tree yet.

Status language: **Decided**, **Working hypothesis**, **Open**.

## Handoff

The Link MC5 / WY-120 first-target hardware path is wrapped: injection pads, Pico carrier (74AHCT125), GPIO map, and the 78 Hz timing table are in the hardware doc. The Dev-Host container and USB Pico path are proven (`make smoke`, `make hello-test`; [toolchains.md](toolchains.md)). Start CRT firmware at **phase 1** below (`make build`); do not skip scope checks after phase 2.

**Decided for v1:** pads V0/V1/H/V/GND, carrier U1 74AHCT125, GPIO 0–3, 144 MHz `sys_clk` / PIO clkdiv 3 → 48 MHz dots, 78.041 Hz first.

**Still open:** three SMs vs combined timing SM; blanking (stall vs padded raster); measured confirmation of 78 Hz counts; 60 Hz timings.

## Architecture

```text
frame_buffer (SRAM, 800x338 @ 2 bpp)
        |
        | DMA (32-bit words, paced by PIO TX DREQ)
        v
PIO pixel SM  --> GPIO 0/1 --> 74AHCT125 --> pads V0 / V1
PIO hsync SM  --> GPIO 2   --> 74AHCT125 --> pad H
PIO vsync SM  --> GPIO 3   --> 74AHCT125 --> pad V
```

Target clock: `sys_clk` = 144 MHz, PIO clkdiv = 3.00 → 48.000 MHz dots (one PIO instruction per dot). First video mode is 78 Hz; 60 Hz is phase 6.

## PIO mapping

**Open.** Two layouts appear in the notes. Recommend three SMs because [`CMakeLists.txt`](../CMakeLists.txt) already generates headers from `video_pixel.pio`, `hsync.pio`, and `vsync.pio`.

| | Recommended (three SMs) | Alternative (two SMs) |
| --- | --- | --- |
| Pixel | SM0: `out pins, 2` to V0/V1 | Pixel SM: same 2-bit shift |
| Timing | SM1: /HSYNC; SM2: /VSYNC, `wait` on IRQ 0 from HSYNC | Combined timing SM for both syncs |
| CMake | Matches current `pico_generate_pio_header` list | Would collapse to fewer `.pio` files |

Pick one before writing the real `.pio` sources. The rest of this plan sketches the three-SM layout.

### Draft cycle counts are not spec

The notes include PIO loops that claim 1390 high + 140 low dots (1530) and 398 high + 4 low lines (402). Those loops do **not** add up. Recount before treating them as the implementation:

- **HSYNC draft:** `set pins,1 [9]` (10) + `set x,30` (1) + 31×32 (992) + `set x,11` (1) + 12×32 (384) = **1388** high, not 1390. Low side: `set pins,0 [7]` (8) + `set x,3` (1) + 4×32 (128) + `irq 0` (1) = **138** low, not 140. Line total **1526**, not 1530.
- **VSYNC draft:** `set x,31` then `wait`/`jmp` is 32 lines; plus `set x,11` is 12 more → **44** high lines, not 398. The 4-line low loop is the only part that matches.

Fix with delay slots and nested X/Y loops so one wrap equals 1530 dots and 402 IRQ-paced lines. Verify on a scope against [hardware-design.md](hardware-design.md).

## Framebuffer packing

**Working hypothesis.** Active area only: 800 × 338 pixels, 2 bits/pixel, MSB-first, 4 pixels/byte.

| Parameter | Value |
| --- | --- |
| Active resolution | 800 × 338 (270,400 pixels) |
| Depth | 2 bpp (V0, V1) |
| Packed width | 200 bytes/line |
| Buffer size | 67,600 bytes (~66 KB of 264 KB SRAM) |
| Shift | `out_shift_right = false`, autopull 32 bits |

| Bits | V0 | V1 | State |
| --- | --- | --- | --- |
| `00` | 0 | 0 | Off / blank |
| `01` | 1 | 0 | Dim |
| `10` | 0 | 1 | Normal |
| `11` | 1 | 1 | Bold |

**Open:** a wrap-forever `out pins, 2` SM consumes a word every 16 dots at all times. The 67,600-byte buffer only covers active video (800 × 338). Either:

- stall/IRQ the pixel SM during porches and vertical blank, or
- pad the DMA stream with blank pixels for the full 1530 × 402 raster.

Decide this in phase 3 so DMA length and PIO wait logic match.

## Phases

### 1. Clock and PIO load

- [ ] `set_sys_clock_khz(144000, true)`
- [ ] `pico_generate_pio_header()` for each `.pio` (already in CMake)
- [ ] Load programs, set clkdiv 3.0, `pio_enable_sm_mask_in_sync`
- [ ] Build succeeds (headers generated, no missing `main.c` / `.pio` once those files exist)

### 2. Stable sync

- [ ] /HSYNC: 31.373 kHz, active-low, 140-dot pulse (2.917 us)
- [ ] /VSYNC: 78.041 Hz, active-low, 4-line pulse (0.128 ms)
- [ ] Line period 31.875 us, frame period 12.813 ms
- [ ] Enable SMs in lockstep so VSYNC IRQ alignment is repeatable

Sketch (`hsync.pio` / `vsync.pio`) — delays are placeholders; fix counts as noted above:

```pio
.program hsync
; SET pin = /HSYNC (idle high)

.wrap_target
    set pins, 1 [9]
    set x, 30
high_loop_1:
    jmp x-- high_loop_1 [31]

    set x, 11
high_loop_2:
    jmp x-- high_loop_2 [31]

    set pins, 0 [7]
    set x, 3
low_loop:
    jmp x-- low_loop [31]
    irq 0
.wrap
```

```pio
.program vsync
; SET pin = /VSYNC (idle high). Paced by IRQ 0 from hsync.

.wrap_target
    set pins, 1
    set x, 31
high_lines_1:
    wait 1 irq 0
    jmp x-- high_lines_1

    set x, 11
high_lines_2:
    wait 1 irq 0
    jmp x-- high_lines_2

    set pins, 0
    set x, 3
low_lines:
    wait 1 irq 0
    jmp x-- low_lines
.wrap
```

### 3. Pixel SM and packing

- [ ] Adjacent GPIOs for V0/V1 (`sm_config_set_out_pins(..., pin_v0, 2)`)
- [ ] MSB-first autopull 32
- [ ] `set_pixel` / `clear_buffer` helpers
- [ ] Blanking strategy chosen (stall vs padded raster)
- [ ] Scope: 2-bit stream on GPIO 0/1 at 48 MHz during active line; idle/blank before /HSYNC

```pio
.program video_pixel
; OUT pins: V0 (dim), V1 (normal). Autopull 32, shift left (MSB first).

.wrap_target
    out pins, 2
.wrap
```

```c
#define FRAME_WIDTH     800
#define FRAME_HEIGHT    338
#define BYTES_PER_LINE  (FRAME_WIDTH / 4) /* 200 */

uint8_t frame_buffer[FRAME_HEIGHT][BYTES_PER_LINE];

typedef enum {
    PIXEL_OFF    = 0b00,
    PIXEL_DIM    = 0b01,
    PIXEL_NORMAL = 0b10,
    PIXEL_BOLD   = 0b11
} PixelColor;

void set_pixel(uint16_t x, uint16_t y, PixelColor color) {
    if (x >= FRAME_WIDTH || y >= FRAME_HEIGHT) return;

    uint16_t byte_idx = x / 4;
    uint8_t shift = (3 - (x % 4)) * 2;

    frame_buffer[y][byte_idx] &= ~(0b11 << shift);
    frame_buffer[y][byte_idx] |= ((color & 0b11) << shift);
}

void clear_buffer(PixelColor color) {
    uint8_t packed_byte = (color << 6) | (color << 4) | (color << 2) | color;
    memset(frame_buffer, packed_byte, sizeof(frame_buffer));
}
```

Init sketch (three SMs, pin_v1 = pin_v0 + 1):

```c
void init_crt_pio(PIO pio, uint pin_v0, uint pin_hsync, uint pin_vsync) {
    set_sys_clock_khz(144000, true);

    uint offset_pixel = pio_add_program(pio, &video_pixel_program);
    uint offset_hsync = pio_add_program(pio, &hsync_program);
    uint offset_vsync = pio_add_program(pio, &vsync_program);

    pio_sm_config c_pixel = video_pixel_program_get_default_config(offset_pixel);
    sm_config_set_out_pins(&c_pixel, pin_v0, 2);
    sm_config_set_out_shift(&c_pixel, false, true, 32);
    sm_config_set_clkdiv(&c_pixel, 3.0f);
    pio_sm_init(pio, 0, offset_pixel, &c_pixel);

    pio_sm_config c_hsync = hsync_program_get_default_config(offset_hsync);
    sm_config_set_set_pins(&c_hsync, pin_hsync, 1);
    sm_config_set_clkdiv(&c_hsync, 3.0f);
    pio_sm_init(pio, 1, offset_hsync, &c_hsync);

    pio_sm_config c_vsync = vsync_program_get_default_config(offset_vsync);
    sm_config_set_set_pins(&c_vsync, pin_vsync, 1);
    sm_config_set_clkdiv(&c_vsync, 3.0f);
    pio_sm_init(pio, 2, offset_vsync, &c_vsync);

    pio_enable_sm_mask_in_sync(pio, (1u << 0) | (1u << 1) | (1u << 2));
}
```

### 4. DMA loop

- [ ] Data channel: 32-bit incrementing read from `frame_buffer`, write to `pio->txf[sm]`, DREQ = PIO TX
- [ ] Control channel: reload data-channel read address via `al3_read_addr_trig`
- [ ] Start after clock + PIO init
- [ ] TX FIFO stays non-empty (`fstat`); no underflow gaps on V0/V1
- [ ] Stream repeats every 12.813 ms
- [ ] Active line ends cleanly before the /HSYNC pulse (depends on blanking choice in phase 3)

```c
#define TOTAL_FRAME_BYTES (FRAME_HEIGHT * BYTES_PER_LINE) /* 67600 */
#define TOTAL_FRAME_WORDS (TOTAL_FRAME_BYTES / 4)         /* 16900 */

static int data_chan;
static int ctrl_chan;
static const uint32_t *frame_buffer_start = (const uint32_t *)frame_buffer;

void setup_framebuffer_dma(PIO pio, uint sm) {
    data_chan = dma_claim_unused_channel(true);
    ctrl_chan = dma_claim_unused_channel(true);

    dma_channel_config c_data = dma_channel_get_default_config(data_chan);
    channel_config_set_transfer_data_size(&c_data, DMA_SIZE_32);
    channel_config_set_read_increment(&c_data, true);
    channel_config_set_write_increment(&c_data, false);
    channel_config_set_dreq(&c_data, pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&c_data, ctrl_chan);

    dma_channel_configure(
        data_chan,
        &c_data,
        &pio->txf[sm],
        frame_buffer,
        TOTAL_FRAME_WORDS,
        false
    );

    dma_channel_config c_ctrl = dma_channel_get_default_config(ctrl_chan);
    channel_config_set_transfer_data_size(&c_ctrl, DMA_SIZE_32);
    channel_config_set_read_increment(&c_ctrl, false);
    channel_config_set_write_increment(&c_ctrl, false);

    dma_channel_configure(
        ctrl_chan,
        &c_ctrl,
        &dma_hw->ch[data_chan].al3_read_addr_trig,
        &frame_buffer_start,
        1,
        false
    );

    dma_channel_start(ctrl_chan);
}
```

Length `TOTAL_FRAME_WORDS` only matches an active-area buffer. If phase 3 pads blanking, this count must grow.

### 5. Test patterns

Geometry is specified in [test-pattern-design.md](test-pattern-design.md). v1 is pattern generators on the Pico, not factory keyboard chords. Optional USB-CDC or later key emulation (`Ctrl+Shift+F1` … `F4`) can switch patterns; that UI is not required for first light.

| Pattern | Drawing | Analog use |
| --- | --- | --- |
| Crosshatch | Bold overscan box; vertical every 80 px; horizontal every 13 lines; bold center reticle | Size, centering, linearity, pincushion |
| Intensity bars | Four horizontal bands: off, dim, normal, bold | Brightness / contrast, no bloom |
| Focus matrix | Dense `H` or `E` in 10 × 13 cells (132 later if needed) | Center/corner focus |
| Full-on box | All pixels bold | Max beam current, 78 Hz overscan |

Sketch for the first two (helpers from phase 3). Crosshatch draw order: grid, then bold box and reticle.

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

void generate_intensity_bars(void) {
    uint16_t bar_height = FRAME_HEIGHT / 4;

    for (uint16_t y = 0; y < FRAME_HEIGHT; y++) {
        PixelColor row_color;
        if (y < bar_height)          row_color = PIXEL_OFF;
        else if (y < bar_height * 2) row_color = PIXEL_DIM;
        else if (y < bar_height * 3) row_color = PIXEL_NORMAL;
        else                         row_color = PIXEL_BOLD;

        uint8_t packed_byte = (row_color << 6) | (row_color << 4)
                            | (row_color << 2) | row_color;
        memset(frame_buffer[y], packed_byte, BYTES_PER_LINE);
    }
}
```

On the CRT (after isolation and 5 V level shift):

- [ ] Distinct black / dim / normal / bold, no smearing
- [ ] +5 V pulses at IC401 inputs when Pico outputs high
- [ ] Geometry grid usable for yoke and pincushion adjustments

### 6. Optional 60 Hz

- [ ] Measure 60 Hz porches (do not invent). Appendix B: 416 active lines, 59.999 Hz, same ~31.37 kHz H
- [ ] Second timing table and PIO counts
- [ ] Keep 78.041 Hz as the default

## Scope / visual checklist

| Check | Expected |
| --- | --- |
| /HSYNC | 31.373 kHz, active-low, ~2.917 us pulse |
| /VSYNC | 78.041 Hz, active-low, 4 lines |
| V0 / V1 | 2-bit stream, no FIFO holes, repeats every 12.813 ms |
| Active line vs /HSYNC | Video ends before the sync pulse |
| IC401 inputs | 0 V / +5 V matching the luminance table |
| Screen | Four intensity levels; crosshatch on overscan bounds |

## Open questions

1. Three PIO SMs vs combined timing SM (recommend three; CMake already assumes it).
2. Pixel SM blanking: stall during retrace vs padded full-raster DMA.
3. Confirm 78 Hz numbers on hardware before freezing PIO delays.
4. 60 Hz porches TBD (Appendix B has active size and rates only).
5. Factory-key pattern switching is optional UI, not v1.
