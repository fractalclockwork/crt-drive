# Phosphor demo reel

RP2040 attract-mode firmware for green-phosphor physics scenes. Raster, polarity, and GPIO map live in [hardware-design.md](hardware-design.md). PIO / DMA how-to lives in [firmware-plan.md](firmware-plan.md). Analog-setup patterns stay in [test-pattern-design.md](test-pattern-design.md). Glass TTY stays in [terminal-plan.md](terminal-plan.md).

Sources: [`video/`](../video/) (shared 78 Hz scanout), [`apps/demos/`](../apps/demos/) (`crt_demos` UF2). Do not mix host bytes with pattern CDC keys or the glass TTY stream.

Status language: **Decided**, **Working hypothesis**, **Open**.

## Handoff

**Decided for this slice:** one attract reel, five phosphor-physics scenes, software 2-bpp fade, USB CDC + BOOTSEL. Unicode Matrix rain is **not** in this UF2; it waits for a refined Unicode terminal and should use real tube afterglow ([terminal-plan.md](terminal-plan.md)).

**Still open:** fade dwell on a live CRT after the 74AHCT125; 60 Hz; 132-column.

## Architecture

```text
USB CDC / BOOTSEL / attract timer
        |
        v
scene tick (starfield, radar, lissajous, xor, wireframe)
        |
        | packed fade + Bresenham / circle (apps/demos)
        v
frame_buffer (800x338 @ 2 bpp)
        |
        | DMA (shared with crt_pattern / crt_term)
        v
PIO pixel / hsync / vsync  --> GPIO 0-3
```

| Make | App | Role | CDC |
| --- | --- | --- | --- |
| `make build` / `make flash` | [`apps/pattern`](../apps/pattern/) (`crt_pattern`) | Analog-setup drawings (default) | `1`–`6` / BOOTSEL; `crt-pattern` banner |
| `make build APP=term` | [`apps/term`](../apps/term/) (`crt_term`) | Glass TTY | host bytes; `crt-term digest=` |
| `make build APP=demos` | [`apps/demos`](../apps/demos/) (`crt_demos`) | Phosphor reel | scene keys; `crt-demos digest=` |
| `make monitor` | running UF2 | CDC attach | banner detect |

`make demos-build` / `demos-flash` / `demos-monitor` / `demos-test` are aliases.

## Scenes

Boot default is starfield. Attract advances every 12 s. A scene key or BOOTSEL selects immediately and restarts the attract timer.

| Key | Scene | Drawing |
| --- | --- | --- |
| `1` / `s` | Starfield | Perspective warp; near stars bold; fade trails |
| `2` / `r` | Radar | Letterboxed PPI, rings, clockwise sweep, painted contacts |
| `3` / `l` | Lissajous | 3:2 quadrature plot with fade trails |
| `4` / `x` | XOR | Full-active packed `(x ^ y) + phase` chevrons (no fade) |
| `5` / `w` | Wireframe | Rotating cube, all 12 edges, fade trails |

`?` prints help plus the current banner. `a` resumes attract from the next scene (already the default).

## Fade

**Decided.** In-place packed decay on the 200 active bytes of each line. Trailing off word stays zero.

Each 2-bit field: `11` → `10` → `01` → `00` (bold → normal → dim → off). A 256-byte LUT updates a line in one pass. Scenes that want trails call fade every two animation ticks (~50 ms/level, ~150 ms to black) then plot. XOR rewrites the active raster and does not fade.

This is a software stand-in so trails read on a short-persistence P31 and on a scope. It is **not** the path for a later Unicode Matrix waterfall.

## Timing

Animation tick is 25 ms (~40 Hz), not 78 fps. PIO/DMA keep scanning the same `frame_buffer` at 78.041 Hz.

## Banner

Until the first host byte, and on `?`, print:

```text
crt-demos digest=<id> 78Hz scene=<name>
```

After the host has spoken, keep the same line on a 250 ms cadence so HIL can see a scene change. `digest=` is `IMAGE_ID` (git short hash, or the unique id `make test APP=demos` injects).

## Verification

- `make build` still produces `crt_pattern` with `crt-pattern pattern=` on CDC.
- `make test APP=demos`: Pico on USB → unique `digest=` then CDC `2` → `scene=radar`; no board (`make pico-discover` empty) → explicit HIL skip, not a pass.
- CRT after isolation and 5 V level shift: trails and bloom on the same 78 Hz timing already checked out on a scope. Tune fade dwell on the tube.

## Later (not this UF2)

- Japanese / halfwidth-katakana Matrix columns on `crt_term`, tube persistence first.
- Extra physics catalog (particles, clock, ECG) and games.
