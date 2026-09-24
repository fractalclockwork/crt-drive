#!/usr/bin/env python3
"""A short crt_vtty page that shows how a host paints the glass.

FILL clears the raster. LABEL draws Noto on the Pico. AREA opens a cell
window, and TEXT types into it with the glass font. The process holds the
serial port until it stops; the Pico then returns to the Indian Head.

  uv run python tools/vtty_host_example.py
  make vtty-start HOST=vtty_host_example
"""

from __future__ import annotations

import struct
import time

from vtty import connect, draw_label, relaunch_with_dialout, release
from vtty_proto import TYPE_AREA, TYPE_CAPS, TYPE_FILL, TYPE_TEXT, transact

LINES = (
    "This is tools/vtty_host_example.py.",
    "FILL cleared the glass. The title is a LABEL.",
    "These lines are TEXT in an AREA window.",
    "Stop this host and the Indian Head comes back.",
)


def paint(fd: int, hunt, timeout: float) -> None:
    caps = transact(fd, hunt, timeout, TYPE_CAPS)
    screen_w = caps.a & ~3
    screen_h = caps.b
    transact(
        fd,
        hunt,
        timeout,
        TYPE_FILL,
        struct.pack("<HHHHB", 0, 0, screen_w, screen_h, 0),
    )
    title = "crt-vtty"
    size = 48
    measured = draw_label(fd, hunt, timeout, 0, 0, size, 3, title, measure=True)
    x = max(0, (screen_w - measured.a) // 2)
    baseline = size + measured.b
    draw_label(fd, hunt, timeout, x, baseline, size, 3, title)

    cell_w = 9 if screen_w >= 1000 else 10
    cell_h = 13 if screen_h <= 420 else 16
    origin = (baseline + 16 + cell_h - 1) // cell_h
    rows = min(26, screen_h // cell_h) - origin
    cols = screen_w // cell_w
    if rows < 2 or cols < 2:
        raise SystemExit(f"raster {screen_w}x{screen_h} has no room for a window")
    ack = transact(
        fd,
        hunt,
        timeout,
        TYPE_AREA,
        struct.pack("<HHHH", 0, origin, cols, rows),
    )
    text = "\r\n".join(LINES) + "\r\n"
    transact(fd, hunt, timeout, TYPE_TEXT, text.encode("utf-8"))
    print(f"example {screen_w}x{screen_h} window {ack.a}x{ack.b}", flush=True)


def main() -> int:
    relaunch_with_dialout()
    fd = None
    try:
        while True:
            try:
                if fd is None:
                    fd, hunt = connect(None, 20.0)
                    paint(fd, hunt, 20.0)
                time.sleep(1.0)
            except (SystemExit, OSError) as exc:
                print(f"link dropped ({exc}); waiting for the pico to come back", flush=True)
                if fd is not None:
                    release(fd)
                fd = None
                time.sleep(2.0)
    finally:
        if fd is not None:
            release(fd)


if __name__ == "__main__":
    raise SystemExit(main())
