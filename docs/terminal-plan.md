# Terminal emulator

RP2040 terminal firmware, separate from the analog-setup pattern generator. Raster, polarity, and GPIO map live in [hardware-design.md](hardware-design.md). PIO / DMA how-to lives in [firmware-plan.md](firmware-plan.md). Patterns stay in [test-pattern-design.md](test-pattern-design.md).

Sources: [`video/`](../video/) (shared 78 Hz 80-col scanout), [`apps/term/`](../apps/term/) (`crt_term` UF2). Do not mix host bytes with pattern CDC keys.

Status language: **Decided**, **Working hypothesis**, **Open**.

## Handoff

The pattern firmware (`crt_pattern`) is the service tool: sync-squares, crosshatch, intensity, focus, Indian Head, full-on. This doc is the start of a real terminal emulator on the same injection path (Pico + 74AHCT125, pads V0/V1/H/V/GND).

**Decided for this slice:** glass TTY only. 78 Hz, 80×26, 10×13 cells, 7×10 glyphs. USB CDC is the host port. Character grid is the source of truth; `frame_buffer` is the scanout cache. No personality parser, keyboard, UART, or 132-column PIO.

**Still open:** measured 78 Hz porch *widths* (same as the pattern firmware); 132-column porch split; which personality to implement first after the glass TTY is HIL-green.

## Architecture

```text
Dev-Host USB CDC
        |
        v
glass TTY (UTF-8, CR/LF/BS/TAB)
        |
        v
TermCell[26][80]     (codepoint + reserved attr)
        |
        | dirty cell blit
        v
frame_buffer (800x338 @ 2 bpp)
        |
        | DMA (shared with crt_pattern)
        v
PIO pixel / hsync / vsync  --> GPIO 0-3
```

Two UF2s, **separate CMake projects** under [`apps/`](../apps/):

| Make | App | Role | CDC |
| --- | --- | --- | --- |
| `make build` / `make flash` | [`apps/pattern`](../apps/pattern/) (`crt_pattern`) | Analog-setup drawings (default) | `1`–`6` / BOOTSEL; `crt-pattern` banner |
| `make build APP=term` / `flash` | [`apps/term`](../apps/term/) (`crt_term`) | Glass TTY | host bytes; `crt-term digest=` banner |
| `make monitor` | running UF2 | CDC attach | banner detect |

`make build` / `make flash` default to the pattern app. Terminal: `make term-build` / `term-flash` / `term-monitor` / `term-test` (or `APP=term`).

## 78 Hz display formats

From the WY-120 intro (26 lines in both). First slice implements the 80-column row only.

| Refresh | Lines | Columns | Cell | Matrix | Active raster | Status |
| :---: | :---: | :---: | :---: | :---: | :---: | --- |
| 78 Hz | 26 | 80 | 10×13 | 7×10 | 800×338 | **Decided** (same PIO as patterns) |
| 78 Hz | 26 | 132 | 9×13 | 7×10 | 1188×338 | later; new `hsync` wrap |
| 60 Hz | 26 | 80 / 132 | 10×16 / 9×16 | 7×12 | 800×416 / 1188×416 | 60 Hz scanout in `cross60` (`m` toggles); TTY still 78 Hz |

**Working hypothesis** for 132-column 78 Hz: line total stays 1530 dots @ 48 MHz (same 31.373 kHz H), so active 1188 + blanking 342. Cell is 9×13 with the same 7×10 glyph at origin (1, 1) and 1 px right margin. Framebuffer ≈ 338 × 300 bytes (~101 KB). Confirm porches on a WY-120 before freezing PIO delays.

## Screen model

**Decided.** Unicode-ready cells so a later parser does not rebuild RAM layout.

```c
typedef struct {
    uint16_t cp;   /* BMP codepoint; 0 = blank */
    uint8_t  attr; /* reserved: dim/bold/reverse/underline/blink */
    uint8_t  flags;
} TermCell;        /* 80 * 26 * 4 = 8320 bytes */
```

Glyph blit uses the 10×13 cell already used by the focus matrix: 7×10 ink at (+1, +1), 2 px right and bottom margin. `attr` is stored but unused in this slice (draw `PIXEL_NORMAL`). Unmapped codepoints keep the real `cp` and draw `.notdef`.

### SRAM budget (this slice)

RP2040 has 264 KB. Rough:

| Block | Bytes |
| --- | ---: |
| `frame_buffer` 338 × 204 | 68,952 |
| `TermCell[26][80]` | 8,320 |
| 7×10 ASCII font + `.notdef` | ~1.3 KB |
| Pico SDK / stacks | tens of KB |

Headroom remains for later pages, 132-col cells, and extra Unicode bitmaps.

### Scroll

**Decided.** LF that would leave row 25: memmove the cell grid one row, memmove 13 scanlines × 25 in `frame_buffer`, clear the last cell row and its pixels.

## Glass TTY

**Decided.** USB CDC (`pico_enable_stdio_usb`, UART off). Incoming bytes are not pattern keys.

| Input | Action |
| --- | --- |
| UTF-8 → BMP codepoint | put at cursor, column + 1; wrap at column 80 (row + 1, scroll if needed) |
| CR (0x0D) | column 0 |
| LF (0x0A) | row + 1, scroll at the bottom |
| BS (0x08) | column − 1 if column > 0; no wrap to the previous line |
| TAB (0x09) | next multiple of 8; wrap if that would leave the row |
| BEL (0x07) | ignore |
| ESC and other C0/C1 | drop (no parser) |
| `?` | CDC status only; not drawn |

Idle banner (until the first host byte, then silent except `?`):

```text
crt-term digest=<id> 78Hz 80x26
```

`?` also prints `cursor=col,row`. Do not print a 250 ms banner forever: that would corrupt a host session.

## Font

**Decided for this slice:** [`apps/term/font_7x10.c`](../apps/term/font_7x10.c), U+0020–U+007E, 10 bytes/glyph, bit 6 = left column (same packing as the pattern `H`). `.notdef` for everything else.

Original Wyse soft fonts (four 128-character fonts in 8K font RAM, loaded from the 27C512) are a later pack. Do not dump the EPROM in this slice. Unicode extras are more 7×10 bitmaps, not a second renderer.

## Original terminal features (backlog)

The WY-120 intro. None of this is in the glass TTY slice.

1. Personality parser — choose then: ANSI (VT220-ish + UTF-8) vs native Wyse ASCII (WY-50/WY-120, 8-bit) vs PC Term.
2. Attributes: dim, blink, blank, underline, reverse, protect; hidden vs nonhidden. Nonhidden maps onto per-cell `attr` first.
3. 78 Hz 132-column raster + the same tty.
4. Multipage memory: 1–7 pages nonhidden; 1–3 hidden.
5. Double-high / double-wide on a line basis; smooth scroll at software-controlled rates.
6. Soft font download (4×128) + a larger Unicode glyph table.
7. Hardware UART (original RS-232); keyboard last (ASCII / ANSI / Enhanced PC).

Battery-backed setup RAM is not a v1 goal (Pico has no WY-120 battery SRAM).

## Verification

- `make build` still produces `crt_pattern` with `crt-pattern pattern=` on CDC.
- `make test APP=term`: Pico on USB → unique `digest=` then a canned UTF-8 line; no board (`make pico-discover` empty) → explicit HIL skip, not a pass.
- CRT after isolation and 5 V level shift: 80-col text on the same 78 Hz timing already checked out on a scope.

## Open

1. First personality after the glass TTY.
2. 132-column porch widths.
3. Whether 132-col keeps a full pixel framebuffer or switches to a scanline expander (SRAM is tighter: ~101 KB FB + 132×26 cells).
4. Unicode glyph extras (halfwidth katakana first) for a later Matrix waterfall on this UF2, using tube persistence rather than software fade — see [demo-plan.md](demo-plan.md).
