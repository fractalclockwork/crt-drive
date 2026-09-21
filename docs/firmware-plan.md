# Firmware plan

RP2040 implementation plan for the CRT drive replacement. Timings, polarity, and GPIO map live in [hardware-design.md](hardware-design.md). **Build order:** 60 Hz 80-col plus in [`apps/cross60`](../apps/cross60/) first; 78 Hz pattern generators in [`apps/pattern/main.c`](../apps/pattern/main.c) after that raster is centered. Shared 78 Hz PIO/DMA is in [`video/`](../video/). Glass TTY and phosphor reel stay under [`apps/`](../apps/). Terminal emulator: [terminal-plan.md](terminal-plan.md). Phosphor demos: [demo-plan.md](demo-plan.md).

Status language: **Decided**, **Working hypothesis**, **Open**.

## Handoff

The Link MC5 / WY-120 first-target hardware path is wrapped: injection pads, Pico carrier (74AHCT125), GPIO map, and the 78 Hz timing table are in the hardware doc. The Dev-Host container and USB Pico path are proven (`make smoke`, `make test APP=hello`; [toolchains.md](toolchains.md)). CRT firmware is in-tree (`make build` / `make flash`); `/HSYNC`, `/VSYNC`, and V0/V1 checked out on a scope. Do not drive a CRT until isolation.

**Decided for v1:** pads V0/V1/H/V/GND, carrier U1 74AHCT125, GPIO 0–3. First raster is **60 Hz 80-col** (Pico PLL 128.4 MHz / clkdiv 4 → 32.1 MHz dots, 1024-dot line). 78 Hz (144 MHz / 3 → 48 MHz, 1530-dot line) is later. Three PIO SMs. Pixel blanking is FIFO stall plus one trailing off word per stored line (16 pixels).

**Still open:** fold 377-line scanout into [`apps/term`](../apps/term/) (still 338 / 48 BP at 144 MHz). Pattern and demos use [`video/scanout.c`](../video/scanout.c).

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

Target clock (60 Hz foundation): `sys_clk` = 128.4 MHz, PIO clkdiv = 4.00 → 32.1 MHz dots. [`apps/pattern`](../apps/pattern/) and [`apps/demos`](../apps/demos/) share that scanout. `term` still uses 144 MHz / 3 → 48 MHz.

## PIO mapping

**Decided.** Three SMs, matching [`video/CMakeLists.txt`](../video/CMakeLists.txt).

| | Three SMs (implemented) |
| --- | --- |
| Pixel | SM0: `out pins, 2` to V0/V1 |
| Timing | SM1: /HSYNC; SM2: /VSYNC, `wait` on IRQ 0 from HSYNC |
| CMake | `pico_generate_pio_header` in [`video/CMakeLists.txt`](../video/CMakeLists.txt); each app under [`apps/`](../apps/) is its own project |

### PIO wrap totals

[`hsync.pio`](../video/hsync.pio) wrap is **1530** dots (960 high active+FP, 140 low sync, 430 high BP). [`vsync.pio`](../video/vsync.pio) wrap is **402** IRQ-paced lines (396 high + 6 low). The sketches below are historical; they do **not** add up and are not what is in the `.pio` files:

- **HSYNC draft:** `set pins,1 [9]` (10) + `set x,30` (1) + 31×32 (992) + `set x,11` (1) + 12×32 (384) = **1388** high, not 1390. Low side: `set pins,0 [7]` (8) + `set x,3` (1) + 4×32 (128) + `irq 0` (1) = **138** low, not 140. Line total **1526**, not 1530.
- **VSYNC draft:** `set x,31` then `wait`/`jmp` is 32 lines; plus `set x,11` is 12 more → **44** high lines, not 398. The 4-line low loop is the only part that matches.

Porch *widths* for [`apps/pattern`](../apps/pattern/) (800/160/140/430 and 338/10/6/48) are still a **working hypothesis**. [`apps/cross60`](../apps/cross60/) 78 Hz DMA is **8 BP + 377 active + 11 FP + 6 sync** (HIL). An earlier table put all blanks after active (0-line back porch, raster in top overscan). Line and frame *rates* from 48 MHz / 1530 / 402 match Appendix B and checked out on a Pico GPIO scope.

## Framebuffer packing

**Decided.** Active area: 800 × 338 pixels, 2 bits/pixel, MSB-first, 4 pixels/byte. Each stored line is **204** bytes (200 packed + 4 trailing off bytes / 16 off pixels) so a FIFO stall holds V0/V1 low.

| Parameter | Value |
| --- | --- |
| Active resolution | 800 × 338 (270,400 pixels) |
| Depth | 2 bpp (V0, V1) |
| Packed width | 200 bytes/line |
| Line stride | 204 bytes (51 DMA words) |
| Buffer size | 338 × 204 = 68,952 bytes |
| Shift | `out_shift_right = false`, autopull 32 bits. 32-bit LE DMA outputs the high byte first, so `set_pixel` stores at `(x/4) ^ 3`. An 80 px grid hid this (all lines at x%16==0); a 40 px grid showed 24/56 px pairs. |

| Bits | V0 | V1 | State |
| --- | --- | --- | --- |
| `00` | 0 | 0 | Off / blank |
| `01` | 1 | 0 | Dim |
| `10` | 0 | 1 | Normal |
| `11` | 1 | 1 | Bold |

**Decided:** wrap-forever `out pins, 2` stalls on empty TX FIFO during H/V blank. DMA sends 51 words per line (800 active pixels + 16 off). 64 V-blank lines are extra DMA rows of zeros, not a second framebuffer. After `/VSYNC` rewind the table is **48 back porch + 338 active + 10 front porch + 6 sync** so the vsync SM’s 396-high / 6-low wrap stays lockstep with DMA. (An earlier table put all 64 blanks after active: 0-line back porch, raster in top overscan.) HSYNC `push`es a dummy RX word at the start of active to pace the next line.

## Phases

### 1. Clock and PIO load

- [x] `set_sys_clock_khz(144000, true)`
- [x] `pico_generate_pio_header()` for each `.pio` (already in CMake)
- [x] Load programs, set clkdiv 3.0, `pio_enable_sm_mask_in_sync`
- [x] Build succeeds (`make build`)

### 2. Stable sync

- [x] /HSYNC: 31.373 kHz, active-low, 140-dot pulse (2.917 us) — scope
- [x] /VSYNC: 78.041 Hz, active-low, 4-line pulse (0.128 ms) — scope
- [x] Line period 31.875 us, frame period 12.813 ms — scope
- [x] Enable SMs in lockstep so VSYNC IRQ alignment is repeatable
- [x] PIO wrap totals 1530 dots / 402 lines (see [`hsync.pio`](../video/hsync.pio), [`vsync.pio`](../video/vsync.pio))

Historical sketch (wrong counts; not the repo sources):

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

- [x] Adjacent GPIOs for V0/V1 (`sm_config_set_out_pins(..., pin_v0, 2)`)
- [x] MSB-first autopull 32
- [x] `set_pixel` / `clear_buffer` helpers
- [x] Blanking strategy chosen (FIFO stall + trailing off word; 64 DMA blank lines)
- [x] Scope: 2-bit stream on GPIO 0/1 during active line; idle/blank before /HSYNC

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

    uint16_t byte_idx = (x / 4) ^ 3; /* LE 32-bit word, PIO MSB first */
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

- [x] Data channel: 32-bit incrementing read from `frame_buffer`, write to `pio->txf[sm]`, DREQ = PIO TX
- [x] Per-line kick: hsync RX `push` drains into a dummy, then a control channel writes `al3_read_addr_trig` from a 402-entry line-pointer table (48 BP + 338 active + 10 FP + 6 sync)
- [x] Start after clock + PIO init
- [x] TX FIFO stays non-empty during active; no underflow gaps on V0/V1 — scope
- [x] Stream repeats every 12.813 ms — scope
- [x] Active line ends cleanly before the /HSYNC pulse — scope

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

Implemented DMA is per-line (51 words), not this whole-frame `TOTAL_FRAME_WORDS` loop. The sketch remains as the original control-channel idea.

### 5. Test patterns

Geometry is specified in [test-pattern-design.md](test-pattern-design.md). v1 is pattern generators on the Pico, not factory keyboard chords. USB CDC (`1`/`c`, `2`/`i`, `3`/`f`, `4`/`n`, `5`/`o`, `6`/`s`) selects a pattern; a short BOOTSEL press cycles the same order. Both rewrite `frame_buffer` while DMA runs. Factory-key emulation is still optional later UI.

- [x] Crosshatch generator in [`apps/pattern/main.c`](../apps/pattern/main.c) (grid, bold box, bold reticle)
- [x] Intensity bars / focus matrix / full-on
- [x] RCA Indian Head (letterboxed 4:3 + V0/V1 side columns)
- [x] Sync-squares (raster-edge box + 100 mm scale square + 80 × 80 + center cross)
- [x] USB CDC pattern select (`make monitor`)
- [x] BOOTSEL cycles patterns (flash-CS sample from RAM)
- [x] Scope: V0/V1 pattern vs /HSYNC (vertical bars every 1.667 us)
- [ ] CRT after isolation and 5 V level shift

| Pattern | Drawing | Analog use |
| --- | --- | --- |
| Sync-squares | Raster-edge box; 100 mm scale square; 80 × 80; center cross | H/V phase, size, mm/px |
| Crosshatch | Bold overscan box; vertical every 80 px; horizontal every 13 lines; bold center reticle | Size, centering, linearity, pincushion |
| Intensity bars | Four horizontal bands: off, dim, normal, bold | Brightness / contrast, no bloom |
| Focus matrix | Dense `H` in 10 × 13 cells (132 later if needed) | Center/corner focus |
| Indian Head | Letterboxed RCA card; V0 / V1 / bold patches; resolution bursts | Geometry, grayscale, bandwidth |
| Full-on box | All pixels bold | Max beam current, 78 Hz overscan |

Sketch for crosshatch (helpers from phase 3). Draw order: grid, then bold box and reticle. Pattern generators are in [`apps/pattern/main.c`](../apps/pattern/main.c); intensity bars pack each line (84 / 84 / 85 / 85) and leave the trailing off word blank. Indian Head is a packed 2 bpp blit.

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
    static const PixelColor bands[4] = {
        PIXEL_OFF, PIXEL_DIM, PIXEL_NORMAL, PIXEL_BOLD
    };
    const uint16_t base = FRAME_HEIGHT / 4; /* 84, remainder 2 → last two bands 85 */
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

On the CRT (after isolation and 5 V level shift):

- [ ] Distinct black / dim / normal / bold, no smearing
- [ ] +5 V pulses at IC401 inputs when Pico outputs high
- [ ] Geometry grid usable for yoke and pincushion adjustments

### 6. Optional 60 Hz

- [x] Standalone plus/measure [`apps/cross60`](../apps/cross60/) (`make test` / `make monitor`): 32.1 MHz, 1024-dot line, V 50/6/51; CDC `4` meas, `m` 80/132, `r` 60/78, `a`/`d` H, `w`/`s` V
- [x] 60 Hz 132-col: 1530-dot `/HSYNC` at ~48 MHz (clkdiv 2.675, PLL stays 128.4 MHz), 1188×416, same 523-line V
- [x] 78 Hz 80/132-col: 402-line `/VSYNC`, **377** active (13×29) / 8 BP (centered; ~1 cm cells)
- [ ] Measure 60 Hz porches (do not invent). Appendix B: 416 active lines, 59.999 Hz, same ~31.37 kHz H
- [ ] Second timing table in shared `video/` (cross60 porches 50/6/51)
- [x] Keep 60 Hz 80-col plus as the default app (`make test`)

## Scope / visual checklist

| Check | Expected |
| --- | --- |
| /HSYNC | 31.373 kHz, active-low, ~2.917 us pulse |
| /VSYNC | 78.041 Hz, active-low, 6 lines |
| V0 / V1 | 2-bit stream, no FIFO holes, repeats every 12.813 ms |
| Active line vs /HSYNC | Video ends before the sync pulse |
| IC401 inputs | 0 V / +5 V matching the luminance table |
| Screen | Four intensity levels; crosshatch on overscan bounds |

## Open questions

1. ~~Three PIO SMs vs combined timing SM~~ — three SMs.
2. ~~Pixel SM blanking: stall vs padded full-raster DMA~~ — FIFO stall + trailing off word.
3. [`apps/cross60`](../apps/cross60/), [`apps/pattern`](../apps/pattern/), and [`apps/demos`](../apps/demos/) 78 Hz porch widths are HIL-settled (377 / 11 / 6 / 8). `term` still uses 338 / 10 / 6 / 48.
4. 60 Hz: Pico PLL 128.4 MHz / 4 = 32.1 MHz dots, **1024**/line (800/113/111/0), 523 lines, V **50/6/51**. Box **22.5 × 17.0 cm**; VR302 min ≈ 11 mm V; H ~1 cm after 111-dot `/HSYNC`. 78 Hz on the same analog: 377 lines, ~1.65 cm V, ~1 cm cells. Polarity is factory active-low.
5. Factory-key pattern switching is optional UI; USB CDC and BOOTSEL are the analog-setup switch.
