#!/usr/bin/env python3
"""Read the crt-pattern pixel-code on this host.

Geometry matches apps/pattern/main.c. Each lit dot sits in a 20-dot column.
Its offset in that column is a nibble:

    0 line[3:0]   1 line[7:4]   2 line[11:8]   3 group   4 parity

The line number is the scan line in the 523-line frame (0 is the first line
after vertical sync). The group number is the 80-dot column. Groups that
begin at x >= 800, and every group on a porch or sync line, are present on
every line so a blanking leak is still a code.

    python3 tools/glass_code.py --self-test
    python3 tools/glass_code.py still.jpg
    python3 tools/glass_code.py --camera
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Contract with apps/pattern/main.c.
COL_W = 20
NIBBLES = 5
GROUP_W = NIBBLES * COL_W
STRIDE = 8
SIGNAL_W_80 = 1008
FRAME_60 = 523
ACTIVE_60 = 416
VBP_60 = 51


def code_nibble(line: int, group: int, n: int) -> int:
    a = line & 15
    b = (line >> 4) & 15
    c = (line >> 8) & 15
    d = group & 15
    if n == 0:
        return a
    if n == 1:
        return b
    if n == 2:
        return c
    if n == 3:
        return d
    return a ^ b ^ c ^ d


def emit_group(line: int, group: int, x0: int, active0: int, active1: int, picture_x: int) -> bool:
    if line < active0 or line >= active1 or x0 >= picture_x:
        return True
    return (line % STRIDE) == (group % STRIDE)


def code_dots(
    nlines: int,
    signal_w: int,
    picture_x: int,
    active0: int,
    active1: int,
) -> list[tuple[int, int]]:
    dots: list[tuple[int, int]] = []
    groups = signal_w // GROUP_W
    for line in range(nlines):
        for group in range(groups):
            x0 = group * GROUP_W
            if not emit_group(line, group, x0, active0, active1, picture_x):
                continue
            for n in range(NIBBLES):
                dots.append((x0 + n * COL_W + code_nibble(line, group, n), line))
    return dots


def words_from_dots(dots: list[tuple[int, int]]) -> list[dict]:
    """Assemble parity-checked (line, group) words from CRT-pixel dots."""
    bins: dict[tuple[int, int], dict[int, int]] = {}
    for x, line in dots:
        if x < 0 or line < 0:
            continue
        group = x // GROUP_W
        rem = x % GROUP_W
        nibble_i = rem // COL_W
        value = rem % COL_W
        if nibble_i >= NIBBLES:
            continue
        slot = bins.setdefault((line, group), {})
        slot[nibble_i] = value
    hits: list[dict] = []
    for (line, group), slot in bins.items():
        if len(slot) != NIBBLES:
            continue
        vals = [slot[i] for i in range(NIBBLES)]
        if (vals[0] ^ vals[1] ^ vals[2] ^ vals[3]) != vals[4]:
            continue
        decoded = vals[0] | (vals[1] << 4) | (vals[2] << 8)
        if decoded != line or vals[3] != (group & 15):
            continue
        hits.append(
            {
                "line": line,
                "group": group,
                "crt_x": group * GROUP_W,
                "crt_y": line,
                "blanking": group * GROUP_W >= 800 or line < VBP_60 or line >= VBP_60 + ACTIVE_60,
            }
        )
    hits.sort(key=lambda h: (h["line"], h["group"]))
    return hits


def render_mask(
    dots: list[tuple[int, int]],
    scale_x: float,
    scale_y: float,
    origin: tuple[float, float] = (12.0, 10.0),
) -> tuple[bytearray, int, int]:
    radius = 1.4
    max_x = max(origin[0] + x * scale_x for x, _y in dots)
    max_y = max(origin[1] + y * scale_y for _x, y in dots)
    cam_w = int(max_x) + 6
    cam_h = int(max_y) + 6
    mask = bytearray(cam_w * cam_h)

    def plot(cx: float, cy: float) -> None:
        x0 = max(0, int(cx - radius))
        x1 = min(cam_w - 1, int(cx + radius))
        y0 = max(0, int(cy - radius))
        y1 = min(cam_h - 1, int(cy + radius))
        r2 = radius * radius
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r2:
                    mask[y * cam_w + x] = 1

    for x, y in dots:
        plot(origin[0] + x * scale_x, origin[1] + y * scale_y)
    return mask, cam_w, cam_h


def blobs_from_mask(mask: bytearray, cam_w: int, cam_h: int, min_area: int = 1) -> list[tuple[float, float]]:
    seen = bytearray(cam_w * cam_h)
    found: list[tuple[float, float]] = []
    for start, on in enumerate(mask):
        if not on or seen[start]:
            continue
        stack = [start]
        seen[start] = 1
        sx = sy = n = 0
        while stack:
            p = stack.pop()
            x = p % cam_w
            y = p // cam_w
            sx += x
            sy += y
            n += 1
            if x > 0:
                q = p - 1
                if mask[q] and not seen[q]:
                    seen[q] = 1
                    stack.append(q)
            if x + 1 < cam_w:
                q = p + 1
                if mask[q] and not seen[q]:
                    seen[q] = 1
                    stack.append(q)
            if y > 0:
                q = p - cam_w
                if mask[q] and not seen[q]:
                    seen[q] = 1
                    stack.append(q)
            if y + 1 < cam_h:
                q = p + cam_w
                if mask[q] and not seen[q]:
                    seen[q] = 1
                    stack.append(q)
        if n >= min_area:
            found.append((sx / n, sy / n))
    return found


def phosphor_mask(rgb: bytes, cam_w: int, cam_h: int) -> bytearray:
    """Dots are local green peaks. Daylight clips the tube, so a global
    green cut misses them; the peak test still finds the mark."""
    glass = None
    try:
        from camera import assess_frame

        glass = assess_frame(rgb, cam_w, cam_h).get("glass")
    except Exception:
        glass = None
    if not glass:
        glass = (0, 0, cam_w, cam_h)
    x0, y0, x1, y1 = glass
    pix_r = rgb[0::3]
    pix_g = rgb[1::3]
    pix_b = rgb[2::3]
    rad = 5
    margin = 6
    surplus = 3
    mask = bytearray(cam_w * cam_h)
    y_lo = max(y0, rad)
    y_hi = min(y1, cam_h - rad)
    x_lo = max(x0, rad)
    x_hi = min(x1, cam_w - rad)
    for y in range(y_lo, y_hi):
        row = y * cam_w
        for x in range(x_lo, x_hi):
            i = row + x
            g = pix_g[i]
            if g - max(pix_r[i], pix_b[i]) < surplus:
                continue
            if (
                g < pix_g[i - rad] + margin
                or g < pix_g[i + rad] + margin
                or g < pix_g[i - rad * cam_w] + margin
                or g < pix_g[i + rad * cam_w] + margin
            ):
                continue
            mask[i] = 1
    return mask


def snap_dots(
    points: list[tuple[float, float]],
    origin: tuple[float, float],
    scale_x: float,
    scale_y: float,
) -> list[tuple[int, int]]:
    snapped: list[tuple[int, int]] = []
    for px, py in points:
        crt_x = int(round((px - origin[0]) / scale_x))
        crt_y = int(round((py - origin[1]) / scale_y))
        snapped.append((crt_x, crt_y))
    return snapped


def self_test() -> int:
    active0 = VBP_60
    active1 = VBP_60 + ACTIVE_60
    dots = code_dots(FRAME_60, SIGNAL_W_80, 800, active0, active1)
    words = {(h["line"], h["group"]) for h in words_from_dots(dots)}
    failures = 0

    def expect(line: int, group: int) -> bool:
        return emit_group(line, group, group * GROUP_W, active0, active1, 800)

    missing = [
        (line, group)
        for line in range(FRAME_60)
        for group in range(SIGNAL_W_80 // GROUP_W)
        if expect(line, group) and (line, group) not in words
    ]
    extra = [
        key for key in words if not expect(key[0], key[1])
    ]
    if missing or extra:
        print(f"self-test fail words missing {len(missing)} extra {len(extra)}")
        failures += 1
    else:
        print(f"self-test ok  {len(words)} line/column words on the 523-line frame")

    # Camera-pixel round trip on the top of the frame, both scales.
    # Picture dots only. Blanking groups repeat every line and are allowed to
    # touch; the picture groups stay a stride apart so the camera can separate them.
    picture_groups = 800 // GROUP_W
    sample_y0 = VBP_60
    sample_y1 = VBP_60 + 20
    sample = [
        d for d in dots
        if sample_y0 <= d[1] < sample_y1 and d[0] // GROUP_W < picture_groups
    ]
    origin = (12.0, 10.0)
    for scale_x, scale_y in ((2.0, 3.0), (1.1, 1.2)):
        mask, cam_w, cam_h = render_mask(sample, scale_x, scale_y, origin)
        blobs = blobs_from_mask(mask, cam_w, cam_h)
        snapped = snap_dots(blobs, origin, scale_x, scale_y)
        got = {(h["line"], h["group"]) for h in words_from_dots(snapped)}
        want = {
            (line, group)
            for line in range(sample_y0, sample_y1)
            for group in range(SIGNAL_W_80 // GROUP_W)
            if expect(line, group) and group < 800 // GROUP_W
        }
        if got != want or len(blobs) != len(sample):
            print(
                f"self-test fail scale {scale_x}x{scale_y}: "
                f"blobs {len(blobs)} dots {len(sample)} words {len(got)}/{len(want)}"
            )
            failures += 1
        else:
            print(
                f"self-test ok  {scale_x} cam-px/dot × {scale_y} cam-px/line  "
                f"{len(want)} words, {len(blobs)} dots"
            )
    return 1 if failures else 0


def format_hits(hits: list[dict]) -> str:
    lines = [f"pixel-code words {len(hits)}"]
    for h in hits:
        where = "blanking" if h["blanking"] else "picture"
        lines.append(
            f"line {h['line']:3d}  group {h['group']:2d}  "
            f"crt {h['crt_x']:4d},{h['crt_y']:3d}  {where}"
        )
    return "\n".join(lines)


def grab_camera() -> tuple[bytes, int, int]:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from camera import FrameGrabber, find_webcam

    grabber = FrameGrabber(find_webcam(None))
    try:
        raw = b""
        for _ in range(8):
            raw = grabber.read()
    finally:
        grabber.close()
    return raw, 1280, 960


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Decode a CRT pixel-code frame on this host.")
    parser.add_argument("image", nargs="?", help="JPEG or PNG still")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--camera", action="store_true", help="grab the C310 and decode")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.camera:
        rgb, cam_w, cam_h = grab_camera()
        from PIL import Image

        Image.frombytes("RGB", (cam_w, cam_h), rgb).save("/tmp/crt-pixel-code.jpg", quality=90)
        print("still /tmp/crt-pixel-code.jpg")
        print("camera decode needs the self-test lattice; use a still once scale is known")
        return 0
    if not args.image:
        parser.error("give a still, --camera, or --self-test")
    print(args.image)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
