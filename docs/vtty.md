# Video TTY

`crt_vtty` is the host-driven page on the glass. The Pico boots to a blank 78 Hz 80-column raster and speaks a framed USB session. Text uses the glass terminal font. Pictures and Noto labels are drawn into the framebuffer. The live page is [`tools/weather.py`](../tools/weather.py).

Build and flash only from the Dev-Host, through repo-root Make. Do not `cmake` on the host, and do not `picotool load -f`. Detail of the image is in [toolchains.md](toolchains.md).

```bash
make build APP=vtty
make flash APP=vtty
uv sync
uv run python tools/vtty.py caps
```

`caps` prints the raster, for example `800x377`. The banner on connect looks like:

```text
crt-vtty digest=… 78Hz 800x377 slots=0
```

`slots=` is how many bitmaps are stored in the flash catalog. A later command on an already-open link does not see the banner again.

The host tool needs group `dialout` on `/dev/serial/by-id/usb-Raspberry_Pi_Pico_*-if00`. `tools/vtty.py` re-executes itself with `sg dialout` when this shell is not in the group. `make flash` and `make monitor` do not need that group. One process owns the serial node. `make flash` and `make test` stop the host page first.

## Pages

```bash
make vtty-start          # tools/weather.py, until make vtty-stop
make vtty-stop
make vtty-status
uv run python tools/vtty.py text "Hello" --crlf
uv run python tools/vtty.py label 24 48 32 3 "San Francisco"
uv run python tools/vtty.py demo
```

`make vtty-start` runs the host page in its own session and writes `.tmp/vtty-host.pid`. The log is `.tmp/vtty-host.log`. `HOST=` picks another `tools/<name>.py` (`make vtty-start HOST=weather` is the default). `make vtty-stop` signals that session, and any host process still holding the Pico serial node. One-shot `tools/vtty.py` commands are not the page; stop the page before them so they can open the port.

`tools/vtty.py` commands:

| Command | What it does |
| --- | --- |
| `caps` | Raster width and height |
| `text MESSAGE` | UTF-8 at the cursor. `--crlf` appends CR LF |
| `goto COL ROW` | Place the cursor, in cells |
| `fill X Y W H COLOR` | Solid rectangle. `X` and `W` are multiples of 4. Color is 0 off, 1 dim, 2 normal, 3 bold |
| `label X Y SIZE COLOR TEXT` | Noto Sans, baseline at `Y`, em height `SIZE`. `--measure` returns the width and does not draw |
| `mode HZ COLS` | `60` or `78`, and `80` or `132` |
| `show ID` | Copy a catalog bitmap. `--x` and `--y` are the origin. Id 0 with an empty slot is the linked 128×64 card |
| `put ID PATH` | Pack a picture, store it, and show it. PPM, or PNG and other files Pillow can open. `--width` and `--height` are required for raw `.2bpp` |
| `clear WHICH` | `0` cells and pixels, `1` cells, `2` pixels |
| `demo` | Linked card, a black band, and one line of text |

The started page cycles three screens:

1. San Francisco weather for 10 seconds. The icon is a 2 bpp blit. The words are Noto, rasterized on the Pico. The clock redraws each minute. The forecast refreshes every 10 minutes.
2. News headlines, one character at a time at 400 words a minute, in the glass font. The next visit continues after the last headline. The last line stays for 5 seconds.
3. One zodiac sign. A drawn mark and the name in Noto stay put. Today's horoscope is typed in the cell window under that header, then held for 5 seconds.
4. News again, then back to weather.

Headlines are Google News RSS. Horoscopes are CosmyDay, one pull a day for all twelve signs. Neither feed needs a key. The sign index and the news cursor are kept in `.tmp/news.json`.

## Frame

After the ASCII banner, the link is binary. Each message is `A5 5A`, a type byte, a little-endian `u16` length, then that many payload bytes (at most 1024). The Pico answers with the same framing, type `0x81`, and 6 payload bytes: the command type, a status byte, then two little-endian `u16` values. Status `0` is success. `1` is a bad length, `3` is off the raster or the grid.

| Type | Bytes | ACK `a`, `b` |
| --- | --- | --- |
| `0x01` TEXT | UTF-8. CR, LF, BS, and TAB move the cursor | column, row |
| `0x02` GOTO | `u16` column, `u16` row | column, row |
| `0x0C` AREA | `u16` column, row, columns, rows | columns and rows actually used |
| `0x08` FILL | `u16` x, y, w, h, then `u8` color | width and height drawn |
| `0x07` BLIT | `u16` x, y, w, then linear 2 bpp rows | width and height drawn |
| `0x0B` LABEL | `i16` x, `i16` baseline y, `u8` size, `u8` color, `u8` flags, then UTF-8. Flag bit 0 measures only | advance width, ascent |
| `0x0A` CAPS | empty | width, height |
| `0x09` MODE | `u8` Hz, `u8` columns | width, height |
| `0x04` SHOW | `u16` id, x, y | bitmap width, height |
| `0x05` PUT | `u16` id, width, height, `u32` byte count | id |
| `0x06` DATA | `u32` offset, then bytes | offset, `1` when the slot is stored |
| `0x03` CLEAR | `u8` which | |

Pixels are linear 2 bpp, four per byte, bits 7–6 the leftmost pixel. Width and a signed blit `x` are multiples of 4. The Pico applies the scanout byte swap. A blit may start off the glass. The ACK is the rectangle that landed.

TEXT, GOTO, and AREA show a blinking underscore at the cursor, the same stroke as the glass font's `_`, on and off every half second. FILL, BLIT, a drawn LABEL, SHOW, and MODE hide it, so a weather paint does not leave a caret on the icon.

AREA limits the cursor, the line wrap, and the scroll to a cell window. Columns or rows of 0 means the whole grid. Scroll moves only that band of scanlines, so a mark and a Noto name above the window stay put. The weather page sets the window back to the full grid before it paints.

## Pictures in flash

SHOW reads a bitmap from the top 512 KB of flash. PUT and DATA write one. Erasing that flash turns XIP off while the USB interrupt handler still lives in flash, so a catalog write on a live link can drop CDC. A page that is up now is a FILL plus BLIT into the framebuffer, not a PUT. `make test APP=vtty` checks the banner, a framed `Hello`, and SHOW of the linked card. It does not store a new slot.

## Type on the Pico

Noto Sans Regular is rasterized on the Pico, not on the host. `font_init()` runs before `scanout_init()`. Walking that font in flash after DMA has started leaves retrace lit. The face uses the fixed-point rasterizer. The float rasterizer does not finish a glyph before the stall watchdog, and the Pico then drops USB and reboots. A glyph taller than 72 pixels or wider than 96 is skipped and the pen still advances. Ink for text is normal or bold. Horizontal scale stays the fractional glass stretch so thin digits survive.

Zodiac marks are not in Noto Sans Regular. They are 2 bpp blits from the host, the same path as the weather icons.
