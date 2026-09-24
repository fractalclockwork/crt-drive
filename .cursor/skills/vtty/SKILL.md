---
name: vtty
description: >-
  Drives the crt_vtty video TTY from the Dev-Host: framed USB commands, the
  glass-font caret, Noto labels, and the weather, news, and horoscope page.
  Use when editing apps/vtty, tools/vtty.py, tools/vtty_proto.py,
  tools/vtty_host_news.py, tools/vtty_host_example.py, or when the user
  mentions vtty, the CRT terminal window, a horoscope, or the weather page.
---

# Video TTY

Usage for a person is [docs/vtty.md](../../../docs/vtty.md). This skill is the path that keeps the glass and the USB link intact.

## Build and flash

Edit on the Dev-Host. Compile and load only through repo-root Make:

```bash
make build APP=vtty
make flash APP=vtty
```

Do not `cmake` on the host, do not install the Pico SDK or `gcc-arm-none-eabi` here, and do not `picotool load -f`. Do not run bare `make test` (that flashes `cross60`). `make test APP=vtty` is the CDC check: banner, framed `Hello`, SHOW of the linked card.

`font_init()` stays before `scanout_init()`. Noto uses `STBTT_RASTERIZER_VERSION` 1 and `STBTT_SCANLINE_CHECKPOINT()`. The float rasterizer stalls scanout and the watchdog drops USB.

## Host session

From the repo root, after `uv sync`:

```bash
make vtty-start
make vtty-status
make vtty-stop
uv run python tools/vtty.py caps
```

`make vtty-start` runs `tools/vtty_host_news.py` unless `HOST=` names another `tools/<name>.py` (`HOST=vtty_host_example` is the short FILL/LABEL/AREA/TEXT page). The pid is `.tmp/vtty-host.pid` and the log is `.tmp/vtty-host.log`. `make vtty-stop` signals that session and any host still holding the Pico serial node. `make flash` and `make test` stop the page first. Do not `pkill -f` the host command; that pattern matches the shell that launched it. Do not `sudo usermod`. `tools/vtty.py` re-executes with `sg dialout` when this shell is not in the group.

Boot and a dropped DTR show the Indian Head with "PLEASE STAND BY" (`indian_head_standby` in `video/indian_head.c`). The first host byte turns the blanking-interval bars off. Host opens set `HUPCL` and drop DTR on close. Without that, Linux leaves DTR high and the last picture stays, including the linked card from `make test APP=vtty`. The news loop is the default page on the glass: weather for 10 seconds, news, one zodiac sign, news. News and the horoscope type at 400 words a minute and hold 5 seconds. After a vtty flash, `make vtty-start` if that page should be on the glass again. One-shot `tools/vtty.py` commands need the page stopped so they can open the port.

## Commands that are safe on a live link

- Blank and paint with FILL and BLIT. A signed blit may start off the glass. Width and `x` are multiples of 4. The ACK size is what was drawn.
- LABEL draws Noto on the Pico. `--measure` does not draw. Text ink is normal (2) or bold (3).
- AREA (`0x0C`, four `u16`: column, row, columns, rows) limits the cursor, wrap, and scroll to that cell window. Zero columns or rows restores the full grid. Scroll moves only that scanline band.
- TEXT and GOTO show the blinking underscore. FILL, BLIT, a drawn LABEL, SHOW, and MODE hide it.

PUT and DATA erase the flash catalog while USB's interrupt handler is in flash. Do not store a bitmap on a live CDC session. A picture for the current page is a framebuffer BLIT.

## Weather page

[`tools/vtty_host_news.py`](../../../tools/vtty_host_news.py) is the cycle. Horoscopes come from CosmyDay (`/content/daily`), one pull per UTC day, cached in `.tmp/news.json` with the news cursor and the sign index. Do not add an API key. The zodiac mark is a host 2 bpp blit. The sign name is a LABEL. The horoscope is TEXT inside the AREA under that header, then AREA is restored to the full grid before the next weather FILL.
