#!/usr/bin/env python3
"""Pack assets/rick.gif into 2 bpp scan lines for apps/rick.

Glass pitch on the 60 Hz box is 0.281 mm/px horizontal and 0.409 mm/px
vertical, so a square on the tube is wider than it is tall. The dancer is
keyed off the bright lattice (he is the part darker than the frame-to-frame
wall), sharpened, and given a hard edge. The lattice is blurred to a quiet
field so it does not dither into the same texture as the shirt. Each frame
is then ordered-dithered to the four beam levels and stored in pixel-DMA
order: horizontal pad, then (x/4)^3 packed pixels, then a black tail.

Run from the repo root (Pillow, on the Dev-Host):

    python3 tools/pack_rick.py apps/rick/rick_frames.pack apps/rick/frames.h
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
GIF = ROOT / "assets" / "rick.gif"

# 60 Hz active box, docs/project-writeup.md.
MM_X = 0.281
MM_Y = 0.409
ACTIVE_W = 800
# Matches video/scanout.c H_PAD_BYTES and the 4-byte black tail.
PAD = 20
TAIL = 4
# Largest 16-px-aligned square that leaves a 16-dot black tail and fits
# 2 MB flash beside the firmware (39 frames).
RICK_W = 352
RICK_H = 242
RICK_LEFT = 224  # (800 - 352) / 2, already a multiple of 16
DMA_BYTES = (RICK_LEFT + RICK_W) // 4 + TAIL  # 148
STRIDE = PAD + DMA_BYTES  # 168

# 8x8 Bayer, 0..63. Same matrix every frame so the dither does not crawl.
BAYER8 = np.array(
    [
        [0, 32, 8, 40, 2, 34, 10, 42],
        [48, 16, 56, 24, 50, 18, 58, 26],
        [12, 44, 4, 36, 14, 46, 6, 38],
        [60, 28, 52, 20, 62, 30, 54, 22],
        [3, 35, 11, 43, 1, 33, 9, 41],
        [51, 19, 59, 27, 49, 17, 57, 25],
        [15, 47, 7, 39, 13, 45, 5, 37],
        [63, 31, 55, 23, 61, 29, 53, 21],
    ],
    dtype=np.int16,
)


def composite_frames(im: Image.Image) -> list[Image.Image]:
    """Disposal is 0 and there is no transparency; each frame replaces."""
    canvas = Image.new("RGBA", im.size, (0, 0, 0, 255))
    frames: list[Image.Image] = []
    for i in range(im.n_frames):
        im.seek(i)
        canvas.alpha_composite(im.convert("RGBA"))
        frames.append(canvas.convert("RGB"))
    return frames


# He sits darker than the lattice. The 80th percentile over the clip is the
# wall on pixels he only crosses; the suit, hair, and mic fall below it.
WALL_PCT = 80
FIGURE_DELTA = 28


def enhance_luma(luma_u8: np.ndarray, wall: np.ndarray) -> np.ndarray:
    """Pull the dancer forward: sharp body, soft lattice, face lifted off the suit."""
    luma = luma_u8.astype(np.float32)
    mask = luma < (wall - FIGURE_DELTA)
    grown = Image.fromarray(np.where(mask, np.uint8(255), np.uint8(0)))
    mask = np.asarray(grown.filter(ImageFilter.MaxFilter(5))) > 0
    blur = np.asarray(
        Image.fromarray(luma_u8).filter(ImageFilter.GaussianBlur(7)), dtype=np.float32
    )
    fine = np.asarray(
        Image.fromarray(luma_u8).filter(ImageFilter.GaussianBlur(1.2)), dtype=np.float32
    )
    sharp = np.clip(luma + 0.85 * (luma - fine), 0, 255)
    y = sharp / 255.0
    # Jacket stays off. Face and hands, which were merging with the wall, step up.
    curved = np.where(
        y < 0.22,
        y * 0.55,
        np.where(y < 0.62, 0.12 + (y - 0.22) * 1.55, 0.74 + (y - 0.62) * 0.7),
    )
    curved = np.clip(curved, 0, 1) * 255.0
    out = np.where(mask, curved, blur * 0.72 + 40.0)
    return np.clip(out, 0, 255).astype(np.uint8)


def enhance_frames(frames: list[Image.Image]) -> list[np.ndarray]:
    lumas = [np.asarray(fr.convert("L"), dtype=np.uint8) for fr in frames]
    wall = np.percentile(np.stack(lumas).astype(np.float32), WALL_PCT, axis=0)
    return [enhance_luma(luma, wall) for luma in lumas]


def quantize(luma_u8: np.ndarray) -> np.ndarray:
    small = Image.fromarray(luma_u8, mode="L").resize((RICK_W, RICK_H), Image.Resampling.LANCZOS)
    luma = np.asarray(small, dtype=np.int16)
    ys = np.arange(RICK_H)[:, None] % 8
    xs = np.arange(RICK_W)[None, :] % 8
    bias = BAYER8[ys, xs]
    # 0..255 * 4 + 0..63 → bins 0..3. Black stays off.
    level = (luma * 4 + bias) // 256
    return np.clip(level, 0, 3).astype(np.uint8)


def pack_frame(level: np.ndarray) -> bytes:
    span = RICK_LEFT + RICK_W
    out = np.zeros((RICK_H, STRIDE), dtype=np.uint8)
    pix = np.zeros((RICK_H, span), dtype=np.uint8)
    pix[:, RICK_LEFT : RICK_LEFT + RICK_W] = level
    p = pix.reshape(RICK_H, span // 4, 4)
    byte = (
        (p[:, :, 0] << 6) | (p[:, :, 1] << 4) | (p[:, :, 2] << 2) | p[:, :, 3]
    ).astype(np.uint8)
    idx = np.arange(span // 4)
    out[:, PAD : PAD + span // 4][:, idx ^ 3] = byte
    return out.tobytes()


def gif_delay_ms(im: Image.Image) -> int:
    delays = []
    for i in range(im.n_frames):
        im.seek(i)
        delays.append(int(im.info.get("duration") or 0))
    im.seek(0)
    if not delays or any(d != delays[0] for d in delays) or delays[0] <= 0:
        raise SystemExit(f"expected one positive GIF delay, got {sorted(set(delays))}")
    return delays[0]


def write_header(path: Path, nframes: int, delay_ms: int) -> None:
    text = f"""\
/* Generated by tools/pack_rick.py — do not edit. */
#ifndef RICK_FRAMES_H
#define RICK_FRAMES_H

#define RICK_FRAMES    {nframes}
#define RICK_W         {RICK_W}
#define RICK_H         {RICK_H}
#define RICK_LEFT      {RICK_LEFT}
#define RICK_DMA_BYTES {DMA_BYTES}
#define RICK_STRIDE    {STRIDE}
/* Frame delay from the GIF, in milliseconds. */
#define RICK_DELAY_MS  {delay_ms}

#endif
"""
    path.write_text(text)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <frames.pack> <frames.h>", file=sys.stderr)
        return 2
    pack_path = Path(sys.argv[1])
    hdr_path = Path(sys.argv[2])
    im = Image.open(GIF)
    delay_ms = gif_delay_ms(im)
    if im.n_frames < 1:
        print("rick.gif has no frames", file=sys.stderr)
        return 1
    frames = enhance_frames(composite_frames(im))
    blob = bytearray()
    counts = np.zeros(4, dtype=np.int64)
    for fr in frames:
        level = quantize(fr)
        counts += np.bincount(level.ravel(), minlength=4)
        blob += pack_frame(level)
    expect = STRIDE * RICK_H * len(frames)
    if len(blob) != expect:
        print(f"size {len(blob)} != {expect}", file=sys.stderr)
        return 1
    pack_path.parent.mkdir(parents=True, exist_ok=True)
    pack_path.write_bytes(blob)
    write_header(hdr_path, len(frames), delay_ms)
    total = int(counts.sum())
    names = ("off", "dim", "normal", "bold")
    mix = " ".join(f"{n}={counts[i] * 100 / total:.1f}%" for i, n in enumerate(names))
    mm_w = RICK_W * MM_X
    mm_h = RICK_H * MM_Y
    print(
        f"wrote {pack_path} ({len(blob)} bytes) {len(frames)}x {RICK_W}x{RICK_H} "
        f"stride {STRIDE} delay {delay_ms} ms glass {mm_w:.1f}x{mm_h:.1f} mm  {mix}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
