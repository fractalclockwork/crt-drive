# Phosphor demo reel

RP2040 attract-mode firmware for green-phosphor physics scenes. Raster, polarity, and GPIO map live in [hardware-design.md](hardware-design.md). PIO / DMA how-to lives in [firmware-plan.md](firmware-plan.md). Analog-setup patterns stay in [test-pattern-design.md](test-pattern-design.md). Glass TTY stays in [terminal-plan.md](terminal-plan.md).

Sources: [`video/scanout.c`](../video/scanout.c) (shared four-mode scanout), [`font/`](../font/) (UTF-8 TrueType), [`apps/demos/`](../apps/demos/) (`crt_demos` UF2). Scene keys `1`–`6` stay distinct from pattern/cross60 `m`/`r` timing keys.

Status language: **Decided**, **Working hypothesis**, **Open**.

## Handoff

**Decided for this slice:** six scenes (five phosphor-physics plus a static Unicode text page), software 2-bpp fade on the physics reel, USB CDC + BOOTSEL. Scene holds until a key or BOOTSEL. Unicode Matrix rain is **not** in this UF2; it waits for a refined Unicode terminal and should use real tube afterglow ([terminal-plan.md](terminal-plan.md)).

**Still open:** fade dwell on a live CRT after the 74AHCT125.

## Architecture

```text
USB CDC / BOOTSEL
        |
        v
scene tick (starfield, radar, lissajous, xor, wireframe, text)
        |
        | packed fade + Bresenham / circle, or TrueType blit (apps/demos + font/)
        v
scanout framebuffer (800×416 / 1188×416 / 800×377 / 1188×377 @ 2 bpp)
        |
        | DMA (shared with crt_pattern / crt_cross60)
        v
PIO pixel / hsync / vsync  --> GPIO 0-3
```

| Make | App | Role | CDC |
| --- | --- | --- | --- |
| `make build` / `make flash` | [`apps/pattern`](../apps/pattern/) (`crt_pattern`) | Analog-setup drawings (default) | `1`–`6` / BOOTSEL; `crt-pattern` banner |
| `make build APP=term` | [`apps/term`](../apps/term/) (`crt_term`) | Glass TTY | host bytes; `crt-term digest=` |
| `make build APP=demos` | [`apps/demos`](../apps/demos/) (`crt_demos`) | Phosphor reel | `1`–`6` scenes; `m`/`r`; `crt-demos digest=` |
| `make monitor` | running UF2 | CDC attach | banner detect |

`make demos-build` / `demos-flash` / `demos-monitor` / `demos-test` are aliases.

## Scenes

Boot default is starfield at **60 Hz 80-col**. The scene holds until a scene key, `n`, or BOOTSEL changes it (no 12 s attract timer — glass checks are one scene at a time). `m` / `r` retune scanout the same way as `pattern` / `cross60` and redraw the current scene.

| Key | Scene | Drawing |
| --- | --- | --- |
| `1` | Starfield | Perspective warp; near stars bold; fade trails |
| `2` | Radar | Physically round PPI (HIL fill mm), rings, clockwise sweep, painted contacts |
| `3` / `l` | Lissajous | 3:2 quadrature plot with fade trails |
| `4` / `x` | XOR | Full-active packed `(x ^ y) + phase` chevrons (no fade) |
| `5` | Wireframe | Rotating cube, all 12 edges, fade trails |
| `6` / `t` | Text | Static Noto Sans UTF-8 sample (Latin / Greek / Cyrillic); CRT pixel-aspect |
| `n` | Next scene | Same as BOOTSEL |
| `m` | 80 / 132-col | Shared scanout |
| `r` | 60 / 78 Hz | Shared scanout; GP4 high in 78 Hz |
| `a`/`d` `w`/`s` `0` | H/V nudge / reset | Same as pattern |

`?` prints help plus the current banner. Letter aliases `s`/`r`/`w`/`a` are timing keys, not scenes, so HIL `2` → radar still holds.

## Fade

**Decided.** In-place packed decay on the active store bytes of each line (200 @ 80-col, 300 @ 132-col). Trailing off word stays zero. Byte order is `(x/4)^3` like the rest of scanout; the LUT still walks 2-bit fields inside each byte.

Each 2-bit field: `11` → `10` → `01` → `00` (bold → normal → dim → off). A 256-byte LUT updates a line in one pass. Scenes that want trails call fade every two animation ticks (~50 ms/level, ~150 ms to black) then plot. XOR rewrites the active raster and does not fade.

This is a software stand-in so trails read on a short-persistence P31 and on a scope. It is **not** the path for a later Unicode Matrix waterfall.

## Timing

Animation tick is 25 ms (~40 Hz), not refresh. PIO/DMA keep scanning the same framebuffer at the selected 60 Hz or 78 Hz rate.

## Banner

Until the first host byte, and on `?`, print:

```text
crt-demos digest=<id> 60Hz 80col scene=<name> hpad=0 vbp=51 vfp=50 vsize78=0
```

After the host has spoken, keep the same line on a 250 ms cadence so HIL can see a scene change. `digest=` is `IMAGE_ID` (git short hash, or the unique id `make test APP=demos` injects).

## Verification

- `make build` still produces `crt_pattern` with `crt-pattern pattern=` on CDC.
- `make test APP=demos`: Pico on USB → unique `digest=` then CDC `2` → `scene=radar`; no board (`make pico-discover` empty) → explicit HIL skip, not a pass.
- CRT after isolation and 5 V level shift: trails and bloom on the same four-mode scanout as `pattern`. Tune fade dwell on the tube.

## Later (not this UF2)

- Japanese / halfwidth-katakana Matrix columns on `crt_term`, tube persistence first.
- Extra physics catalog (particles, clock, ECG) and games.
