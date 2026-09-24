#!/usr/bin/env python3
"""Send a page to crt_vtty: live text, and pictures stored in Pico flash.

The firmware paints from a catalog in the top 512 KB of flash. SHOW names a
slot. PUT writes one bitmap once (linear 2 bpp). Id 0 with an empty slot is
the linked 128×64 card.

Examples:
  uv run python tools/vtty.py caps
  uv run python tools/vtty.py demo
  uv run python tools/vtty.py text "Hello" --crlf
  uv run python tools/vtty.py show 0 --x 16 --y 80
  uv run python tools/vtty.py put 1 picture.ppm
  uv run python tools/vtty.py put 2 card.2bpp --width 128 --height 64
  uv run python tools/vtty.py label 24 48 32 3 "San Francisco"
  uv run python tools/vtty_host_news.py
  uv run python tools/vtty_host_example.py
"""

from __future__ import annotations

import argparse
import array
import fcntl
import glob
import grp
import os
import pwd
import shlex
import struct
import sys
import termios
import time
from pathlib import Path

from vtty_proto import (
    MAX_PAYLOAD,
    TYPE_BLIT,
    TYPE_CAPS,
    TYPE_CLEAR,
    TYPE_FILL,
    TYPE_GOTO,
    TYPE_LABEL,
    TYPE_MODE,
    TYPE_SHOW,
    TYPE_TEXT,
    Ack,
    Hunt,
    transact,
)

TIOCMGET = 0x5415
TIOCMSET = 0x5418
TIOCM_DTR = 0x002
TIOCM_RTS = 0x004

PORT_GLOBS = (
    "/dev/serial/by-id/usb-Raspberry_Pi_Pico*",
    "/dev/serial/by-id/usb-Raspberry_Pi_Pico_*",
)


def find_port(explicit: str | None) -> str:
    if explicit:
        if not os.path.exists(explicit):
            raise SystemExit(f"serial port not found: {explicit}")
        return explicit
    matches: list[str] = []
    for pattern in PORT_GLOBS:
        matches.extend(glob.glob(pattern))
    matches = sorted(set(matches))
    if00 = [p for p in matches if p.endswith("-if00") or "if00" in os.path.basename(p)]
    chosen = if00 or matches
    if not chosen:
        raise SystemExit(
            "no Pico USB serial under /dev/serial/by-id/usb-Raspberry_Pi_Pico* "
            "(is crt_vtty running, not BOOTSEL?)"
        )
    return chosen[0]


def configure(fd: int) -> None:
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    # HUPCL drops DTR when the last fd closes, including a killed host.
    # Without it Linux leaves DTR high and the last picture stays on the glass.
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL | termios.HUPCL
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    cc = list(attrs[6])
    cc[termios.VMIN] = 0
    cc[termios.VTIME] = 1
    attrs[6] = cc
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    buf = array.array("I", [0])
    fcntl.ioctl(fd, TIOCMGET, buf, True)
    buf[0] |= TIOCM_DTR | TIOCM_RTS
    fcntl.ioctl(fd, TIOCMSET, buf, True)


def release(fd: int) -> None:
    """Drop DTR so the Pico returns to the Indian Head, then close."""
    buf = array.array("I", [0])
    try:
        fcntl.ioctl(fd, TIOCMGET, buf, True)
        buf[0] &= ~(TIOCM_DTR | TIOCM_RTS)
        fcntl.ioctl(fd, TIOCMSET, buf, True)
    except OSError:
        pass
    os.close(fd)


def wait_banner(fd: int, timeout_s: float) -> str:
    # Fresh boots repeat the banner every 250 ms. After the first host byte
    # the firmware stays quiet, so a later command must not wait the full timeout.
    deadline = time.monotonic() + min(timeout_s, 2.0)
    buf = b""
    while time.monotonic() < deadline:
        try:
            chunk = os.read(fd, 256)
        except BlockingIOError:
            chunk = b""
        if chunk:
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", errors="replace").strip()
                if text.startswith("crt-vtty"):
                    print(text, flush=True)
                    return text
        else:
            time.sleep(0.05)
    print("no crt-vtty banner; continuing", flush=True)
    return ""


def _nearest_level(value: float) -> tuple[int, int]:
    if value < 0:
        value = 0
    elif value > 255:
        value = 255
    level = int(round(value / 85.0))
    if level > 3:
        level = 3
    return level, level * 85


def pack_dither(rgb: bytes, w: int, h: int) -> bytes:
    """Floyd–Steinberg to the four CRT levels. Width is a multiple of 4."""
    luma = [0.0] * (w * h)
    for i in range(w * h):
        o = i * 3
        luma[i] = (rgb[o] * 77 + rgb[o + 1] * 150 + rgb[o + 2] * 29) / 256
    out = bytearray((w // 4) * h)
    for y in range(h):
        for x in range(w):
            i = y * w + x
            level, quant = _nearest_level(luma[i])
            err = luma[i] - quant
            if x + 1 < w:
                luma[i + 1] += err * (7 / 16)
            if y + 1 < h:
                if x > 0:
                    luma[i + w - 1] += err * (3 / 16)
                luma[i + w] += err * (5 / 16)
                if x + 1 < w:
                    luma[i + w + 1] += err * (1 / 16)
            bi = y * (w // 4) + (x // 4)
            out[bi] |= level << ((3 - (x % 4)) * 2)
    return bytes(out)


def pack_rgb(rgb: bytes, w: int, h: int) -> bytes:
    if w % 4 != 0:
        raise SystemExit(f"width {w} is not a multiple of 4")
    if len(rgb) < w * h * 3:
        raise SystemExit("image is shorter than width×height")
    out = bytearray((w // 4) * h)
    for y in range(h):
        for x in range(w):
            o = (y * w + x) * 3
            luma = (rgb[o] * 77 + rgb[o + 1] * 150 + rgb[o + 2] * 29) >> 8
            if luma < 64:
                px = 0
            elif luma < 128:
                px = 1
            elif luma < 192:
                px = 2
            else:
                px = 3
            bi = y * (w // 4) + (x // 4)
            out[bi] |= px << ((3 - (x % 4)) * 2)
    return bytes(out)


def fit_rgb(im, max_w: int, max_h: int):
    """Scale so the whole picture sits inside the raster. Width stays a multiple of 4."""
    from PIL import Image

    max_w &= ~3
    if max_w < 4 or max_h < 1:
        raise SystemExit(f"raster {max_w}x{max_h} is too small")
    sw, sh = im.size
    if sw < 1 or sh < 1:
        raise SystemExit("image has no pixels")
    scale = min(max_w / sw, max_h / sh, 1.0)
    nw = max(4, int(sw * scale))
    nw -= nw % 4
    nh = max(1, int(round(sh * nw / sw)))
    if nh > max_h:
        nh = max_h
        nw = max(4, int(round(sw * nh / sh)))
        nw -= nw % 4
    if nw > max_w:
        nw = max_w
    if nw < 4 or nh < 1:
        raise SystemExit("fitted image has no pixels")
    if (nw, nh) != (sw, sh):
        im = im.resize((nw, nh), Image.Resampling.LANCZOS)
    return im


def load_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise SystemExit(f"{path} is not a binary P6 PPM")
    i = 2
    tokens: list[bytes] = []
    while len(tokens) < 3:
        while i < len(data) and data[i] in b" \t\r\n":
            i += 1
        if i < len(data) and data[i] == ord("#"):
            while i < len(data) and data[i] != ord("\n"):
                i += 1
            continue
        j = i
        while j < len(data) and data[j] not in b" \t\r\n":
            j += 1
        if j == i:
            raise SystemExit(f"{path} has a short PPM header")
        tokens.append(data[i:j])
        i = j
    w, h, maxv = (int(t) for t in tokens)
    if maxv > 255:
        raise SystemExit(f"{path} has maxval {maxv}; only 8-bit P6 is read")
    if i < len(data) and data[i] in b" \t\r\n":
        i += 1
    return w, h, pack_rgb(data[i:], w, h)


def load_rgb(path: Path):
    try:
        from PIL import Image
    except ImportError as exc:
        raise SystemExit("PNG/JPEG needs Pillow: uv sync") from exc
    suffix = path.suffix.lower()
    if suffix == ".ppm":
        data = path.read_bytes()
        if not data.startswith(b"P6"):
            raise SystemExit(f"{path} is not a binary P6 PPM")
        i = 2
        tokens: list[bytes] = []
        while len(tokens) < 3:
            while i < len(data) and data[i] in b" \t\r\n":
                i += 1
            if i < len(data) and data[i] == ord("#"):
                while i < len(data) and data[i] != ord("\n"):
                    i += 1
                continue
            j = i
            while j < len(data) and data[j] not in b" \t\r\n":
                j += 1
            if j == i:
                raise SystemExit(f"{path} has a short PPM header")
            tokens.append(data[i:j])
            i = j
        w, h, maxv = (int(t) for t in tokens)
        if maxv > 255:
            raise SystemExit(f"{path} has maxval {maxv}; only 8-bit P6 is read")
        if i < len(data) and data[i] in b" \t\r\n":
            i += 1
        return Image.frombytes("RGB", (w, h), data[i : i + w * h * 3])
    return Image.open(path).convert("RGB")


def load_pil(path: Path) -> tuple[int, int, bytes]:
    im = load_rgb(path)
    w, h = im.size
    w -= w % 4
    if w <= 0 or h <= 0:
        raise SystemExit(f"{path} has no pixels")
    im = im.crop((0, 0, w, h))
    return w, h, pack_rgb(im.tobytes(), w, h)


def load_bitmap(path: Path, width: int | None, height: int | None) -> tuple[int, int, bytes]:
    suffix = path.suffix.lower()
    if suffix == ".2bpp":
        if not width or not height:
            raise SystemExit("a .2bpp file needs --width and --height")
        if width % 4 != 0:
            raise SystemExit("width must be a multiple of 4")
        raw = path.read_bytes()
        need = (width // 4) * height
        if len(raw) < need:
            raise SystemExit(f"{path} is {len(raw)} bytes, need {need}")
        return width, height, raw[:need]
    if suffix == ".ppm":
        return load_ppm(path)
    return load_pil(path)


def relaunch_with_dialout() -> None:
    """This shell may predate `usermod -aG dialout`. sg applies the group now."""
    if os.environ.get("VTTY_DIALOUT_REEXEC"):
        return
    try:
        group = grp.getgrnam("dialout")
    except KeyError:
        return
    if group.gr_gid in os.getgroups():
        return
    user = pwd.getpwuid(os.getuid()).pw_name
    if user not in group.gr_mem:
        return
    os.environ["VTTY_DIALOUT_REEXEC"] = "1"
    cmd = " ".join(shlex.quote(a) for a in [sys.executable, *sys.argv])
    os.execvp("sg", ["sg", "dialout", "-c", cmd])


def connect(port: str | None, timeout_s: float) -> tuple[int, Hunt]:
    path = find_port(port)
    print(f"using {path}", flush=True)
    try:
        fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    except PermissionError:
        raise SystemExit(
            f"permission denied opening {path}\n"
            "ttyACM is root:dialout. This account is not in that group.\n"
            "  sudo usermod -aG dialout \"$USER\"\n"
            "Open a new shell so the group is picked up, then run the command again."
        ) from None
    configure(fd)
    termios.tcflush(fd, termios.TCIOFLUSH)
    wait_banner(fd, timeout_s)
    return fd, Hunt()


def cmd_text(fd: int, hunt: Hunt, timeout_s: float, args: argparse.Namespace) -> None:
    if args.message == "-":
        data = sys.stdin.buffer.read()
    else:
        data = args.message.encode("utf-8")
    if args.crlf:
        data += b"\r\n"
    ack = transact(fd, hunt, timeout_s, TYPE_TEXT, data)
    print(f"cursor {ack.a},{ack.b}", flush=True)


def cmd_show(fd: int, hunt: Hunt, timeout_s: float, args: argparse.Namespace) -> None:
    ack = transact(
        fd, hunt, timeout_s, TYPE_SHOW, struct.pack("<HHH", args.id, args.x, args.y)
    )
    print(f"show {ack.a}x{ack.b}", flush=True)


def cmd_put(fd: int, hunt: Hunt, timeout_s: float, args: argparse.Namespace) -> None:
    caps = transact(fd, hunt, timeout_s, TYPE_CAPS)
    screen_w = caps.a & ~3
    screen_h = caps.b
    path = Path(args.path)
    if path.suffix.lower() == ".2bpp":
        w, h, bits = load_bitmap(path, args.width, args.height)
        origin_x = 0
        origin_y = 0
    else:
        im = fit_rgb(load_rgb(path), screen_w, screen_h)
        w, h = im.size
        bits = pack_dither(im.tobytes(), w, h)
        origin_x = ((screen_w - w) // 2) & ~3
        origin_y = max(0, (screen_h - h) // 2)
    print(f"fit {w}x{h} at {origin_x},{origin_y} on {caps.a}x{caps.b}", flush=True)
    transact(
        fd,
        hunt,
        timeout_s,
        TYPE_FILL,
        struct.pack("<HHHHB", 0, 0, screen_w, screen_h, 0),
    )
    blit_packed(fd, hunt, timeout_s, origin_x, origin_y, w, h, bits)
    print(f"drew {w}x{h} at {origin_x},{origin_y}", flush=True)


def blit_packed(
    fd: int,
    hunt: Hunt,
    timeout_s: float,
    x: int,
    y: int,
    w: int,
    h: int,
    bits: bytes,
) -> None:
    """Linear 2 bpp (four pixels per byte, MSB leftmost) onto the raster.

    Strips stay inside one payload buffer. The firmware clips to the raster
    and ACKs the width and height it actually wrote.
    """
    stride = w // 4
    if stride < 1:
        raise SystemExit("blit width is zero")
    rows_per = (MAX_PAYLOAD - 6) // stride
    if rows_per < 1:
        raise SystemExit(f"width {w} does not fit in one vtty frame")
    row = 0
    while row < h:
        rows = min(rows_per, h - row)
        chunk = bits[row * stride : (row + rows) * stride]
        transact(
            fd,
            hunt,
            timeout_s,
            TYPE_BLIT,
            struct.pack("<hhH", x, y + row, w) + chunk,
        )
        row += rows


def draw_label(
    fd: int,
    hunt: Hunt,
    timeout_s: float,
    x: int,
    y: int,
    size: int,
    color: int,
    text: str,
    measure: bool = False,
) -> Ack:
    """Noto Sans on the Pico. y is the baseline. ACK a is width, b is cap ascent."""
    if not 1 <= size <= 255:
        raise SystemExit("label size is 1..255 px")
    if not 0 <= color <= 3:
        raise SystemExit("label color is 0..3")
    payload = struct.pack("<hhBBB", x, y, size, color, 1 if measure else 0)
    payload += text.encode("utf-8")
    return transact(fd, hunt, timeout_s, TYPE_LABEL, payload)


def cmd_demo(fd: int, hunt: Hunt, timeout_s: float, _args: argparse.Namespace) -> None:
    caps = transact(fd, hunt, timeout_s, TYPE_CAPS)
    w = caps.a & ~3
    y = 80 if caps.b > 160 else 0
    transact(fd, hunt, timeout_s, TYPE_SHOW, struct.pack("<HHH", 0, 16, y))
    transact(fd, hunt, timeout_s, TYPE_FILL, struct.pack("<HHHHB", 0, 0, w, 16, 0))
    transact(fd, hunt, timeout_s, TYPE_GOTO, struct.pack("<HH", 0, 0))
    transact(fd, hunt, timeout_s, TYPE_TEXT, b"Hello from the host\r\n")
    print(f"page {caps.a}x{caps.b}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=os.environ.get("PICO_PORT", ""))
    parser.add_argument("--timeout", type=float, default=20.0)
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("caps", help="read the active raster size")

    text = sub.add_parser("text", help="write UTF-8 at the cursor")
    text.add_argument("message", help='text, or "-" for stdin')
    text.add_argument("--crlf", action="store_true", help="append CR LF")

    goto = sub.add_parser("goto", help="place the cursor")
    goto.add_argument("col", type=int)
    goto.add_argument("row", type=int)

    clear = sub.add_parser("clear", help="0 both, 1 cells, 2 pixels")
    clear.add_argument("which", nargs="?", type=int, default=0)

    show = sub.add_parser("show", help="copy a flash bitmap onto the raster")
    show.add_argument("id", type=int)
    show.add_argument("--x", type=int, default=0)
    show.add_argument("--y", type=int, default=0)

    put = sub.add_parser("put", help="scale a picture to the raster, store it, and show it")
    put.add_argument("id", type=int)
    put.add_argument("path")
    put.add_argument("--width", type=int)
    put.add_argument("--height", type=int)

    fill = sub.add_parser("fill", help="solid rectangle, color 0..3")
    fill.add_argument("x", type=int)
    fill.add_argument("y", type=int)
    fill.add_argument("w", type=int)
    fill.add_argument("h", type=int)
    fill.add_argument("color", type=int)

    mode = sub.add_parser("mode", help="60 or 78, and 80 or 132")
    mode.add_argument("hz", type=int)
    mode.add_argument("cols", type=int)

    label = sub.add_parser("label", help="Noto Sans at a baseline (scalable face)")
    label.add_argument("x", type=int)
    label.add_argument("y", type=int)
    label.add_argument("size", type=int, help="pixel height of the em")
    label.add_argument("color", type=int, help="0 off, 1 dim, 2 normal, 3 bold")
    label.add_argument("text")
    label.add_argument("--measure", action="store_true", help="return width, do not draw")

    sub.add_parser("demo", help="linked card, a black band, and a line of text")

    args = parser.parse_args()
    relaunch_with_dialout()
    fd, hunt = connect(args.port or None, args.timeout)
    try:
        if args.cmd == "caps":
            ack = transact(fd, hunt, args.timeout, TYPE_CAPS)
            print(f"{ack.a}x{ack.b}", flush=True)
        elif args.cmd == "text":
            cmd_text(fd, hunt, args.timeout, args)
        elif args.cmd == "goto":
            ack = transact(
                fd, hunt, args.timeout, TYPE_GOTO, struct.pack("<HH", args.col, args.row)
            )
            print(f"cursor {ack.a},{ack.b}", flush=True)
        elif args.cmd == "clear":
            transact(fd, hunt, args.timeout, TYPE_CLEAR, struct.pack("<B", args.which))
            print("cleared", flush=True)
        elif args.cmd == "show":
            cmd_show(fd, hunt, args.timeout, args)
        elif args.cmd == "put":
            cmd_put(fd, hunt, args.timeout, args)
        elif args.cmd == "fill":
            if args.x % 4 or args.w % 4:
                raise SystemExit("x and w must be multiples of 4")
            if not 0 <= args.color <= 3:
                raise SystemExit("color is 0..3")
            transact(
                fd,
                hunt,
                args.timeout,
                TYPE_FILL,
                struct.pack("<HHHHB", args.x, args.y, args.w, args.h, args.color),
            )
            print("filled", flush=True)
        elif args.cmd == "mode":
            ack = transact(
                fd, hunt, args.timeout, TYPE_MODE, struct.pack("<BB", args.hz, args.cols)
            )
            print(f"{ack.a}x{ack.b}", flush=True)
        elif args.cmd == "label":
            ack = draw_label(
                fd,
                hunt,
                args.timeout,
                args.x,
                args.y,
                args.size,
                args.color,
                args.text,
                args.measure,
            )
            print(f"width {ack.a} ascent {ack.b}", flush=True)
        elif args.cmd == "demo":
            cmd_demo(fd, hunt, args.timeout, args)
        else:
            raise SystemExit(f"unknown command {args.cmd}")
    finally:
        release(fd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
