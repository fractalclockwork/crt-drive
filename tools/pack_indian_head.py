#!/usr/bin/env python3
"""Rasterize the RCA Indian Head SVG into a 800x338 2 bpp framebuffer.

The WY-120 raster is ~2.37:1. The card is 4:3, so it is letterboxed to 448x336
and centered. Side columns use V0/V1 as isolated patches and resolution bursts.

Luminance (see docs/hardware-design.md):
  00 OFF     black
  01 DIM     V0 only
  10 NORMAL  V1 only
  11 BOLD    V0+V1
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSET = ROOT / "assets" / "indian_head"
SVG = ASSET / "RCA_Indian_Head_Test_Pattern.svg"
RENDER = ASSET / "render_1792x1344.png"
PREVIEW = ASSET / "preview_800x338.png"
HEADER = ASSET / "indian_head_pattern.h"

FRAME_W, FRAME_H = 800, 338
CARD_W, CARD_H = 448, 336
CARD_X, CARD_Y = 176, 1
HI_W, HI_H = 1792, 1344  # 4x card

PIXEL_OFF, PIXEL_DIM, PIXEL_NORMAL, PIXEL_BOLD = 0, 1, 2, 3
RECON = np.array([0, 85, 170, 255], dtype=np.float32)

# Portrait ellipse on the 448x336 card (positive 4-level; rest of card inverts).
PORTRAIT_CX, PORTRAIT_CY = 224.0, 54.0
PORTRAIT_RX, PORTRAIT_RY = 48.0, 40.0


def render_svg() -> Image.Image:
    if not SVG.exists():
        raise SystemExit(f"missing {SVG}")
    subprocess.check_call(
        [
            "convert",
            "-background",
            "white",
            "-density",
            "192",
            str(SVG),
            "-resize",
            f"{HI_W}x{HI_H}!",
            "-colorspace",
            "Gray",
            "-depth",
            "8",
            str(RENDER),
        ]
    )
    return Image.open(RENDER).convert("L")


def quantize_card(hi: np.ndarray) -> np.ndarray:
    """hi is HI_H x HI_W uint8. Returns CARD_H x CARD_W values in 0..3."""
    # Area-average to card size, then classify.
    img = Image.fromarray(hi, mode="L").resize((CARD_W, CARD_H), Image.Resampling.BOX)
    g = np.array(img, dtype=np.float32)

    yy, xx = np.ogrid[:CARD_H, :CARD_W]
    portrait = ((xx - PORTRAIT_CX) / PORTRAIT_RX) ** 2 + (
        (yy - PORTRAIT_CY) / PORTRAIT_RY
    ) ** 2 <= 1.0

    out = np.zeros((CARD_H, CARD_W), dtype=np.uint8)

    # Geometry: white paper -> OFF, black ink -> BOLD, gray wedges inverted
    # onto DIM/NORMAL/BOLD so the pie still has distinct steps.
    geo = np.empty((CARD_H, CARD_W), dtype=np.uint8)
    geo[g >= 232] = PIXEL_OFF
    geo[g <= 28] = PIXEL_BOLD
    mid = (g > 28) & (g < 232)
    inv = 255.0 - g
    # 28..232 inverted is ~227..23. Map to DIM, NORMAL, BOLD (keep ink as peak).
    t = np.clip((inv - 40.0) / (200.0), 0.0, 1.0)
    geo_mid = np.where(t < 0.33, PIXEL_DIM, np.where(t < 0.66, PIXEL_NORMAL, PIXEL_BOLD))
    geo[mid] = geo_mid[mid]

    # Portrait stays a positive 4-level image (hair dark, feathers light).
    tone = np.empty_like(geo)
    t2 = np.clip(g / 255.0, 0.0, 1.0)
    tone = np.where(
        t2 < 0.20,
        PIXEL_OFF,
        np.where(
            t2 < 0.45,
            PIXEL_DIM,
            np.where(t2 < 0.72, PIXEL_NORMAL, PIXEL_BOLD),
        ),
    ).astype(np.uint8)

    out[:, :] = geo
    out[portrait] = tone[portrait]
    return out


def draw_hburst(fb: np.ndarray, x0: int, x1: int, y0: int, y1: int, on: int, off: int, color: int) -> None:
    period = on + off
    xs = np.arange(x1 - x0)
    col = np.where((xs % period) < on, color, PIXEL_OFF).astype(np.uint8)
    fb[y0:y1, x0:x1] = col[np.newaxis, :]


def draw_vburst(fb: np.ndarray, x0: int, x1: int, y0: int, y1: int, on: int, off: int, color: int) -> None:
    period = on + off
    ys = np.arange(y1 - y0)
    row = np.where((ys % period) < on, color, PIXEL_OFF).astype(np.uint8)
    fb[y0:y1, x0:x1] = row[:, np.newaxis]


def compose_frame(card: np.ndarray) -> np.ndarray:
    fb = np.zeros((FRAME_H, FRAME_W), dtype=np.uint8)
    fb[CARD_Y : CARD_Y + CARD_H, CARD_X : CARD_X + CARD_W] = card

    # Left: isolated V0, V1, and V0+V1 patches (bloom / channel check).
    left_top, left_bot = 8, FRAME_H - 8
    for x0, x1, color in (
        (16, 56, PIXEL_DIM),
        (64, 104, PIXEL_NORMAL),
        (112, 160, PIXEL_BOLD),
    ):
        fb[left_top:left_bot, x0:x1] = color
        # 2-pixel OFF gutter already from zeros
        # Bottom of each patch: 1-1 burst in that channel (bandwidth of V0 or V1).
        draw_hburst(fb, x0, x1, left_bot - 40, left_bot, 2, 2, color)

    # Right: horizontal resolution at 48 MHz dots, then per-channel bursts.
    rx0, rx1 = 632, 792
    bands = [
        (8, 52, 1, 1, PIXEL_BOLD),    # Nyquist 24 MHz
        (56, 100, 2, 2, PIXEL_BOLD),  # 12 MHz
        (104, 148, 4, 4, PIXEL_BOLD),
        (152, 196, 8, 8, PIXEL_BOLD),
        (200, 244, 2, 2, PIXEL_DIM),     # V0 only
        (248, 292, 2, 2, PIXEL_NORMAL),  # V1 only
        (296, 330, 1, 1, PIXEL_BOLD),    # vertical Nyquist in remaining rows
    ]
    for y0, y1, on, off, color in bands[:-1]:
        draw_hburst(fb, rx0, rx1, y0, y1, on, off, color)
    y0, y1, on, off, color = bands[-1]
    draw_vburst(fb, rx0, rx1, y0, y1, on, off, color)

    # Overscan box in BOLD (same role as the crosshatch box).
    fb[0, :] = PIXEL_BOLD
    fb[FRAME_H - 1, :] = PIXEL_BOLD
    fb[:, 0] = PIXEL_BOLD
    fb[:, FRAME_W - 1] = PIXEL_BOLD
    return fb


def pack_msb(fb: np.ndarray) -> bytes:
    """4 pixels/byte, MSB first, matching set_pixel()."""
    h, w = fb.shape
    assert w % 4 == 0
    out = bytearray(h * (w // 4))
    i = 0
    for y in range(h):
        row = fb[y]
        for x in range(0, w, 4):
            byte = (
                ((row[x] & 3) << 6)
                | ((row[x + 1] & 3) << 4)
                | ((row[x + 2] & 3) << 2)
                | (row[x + 3] & 3)
            )
            out[i] = byte
            i += 1
    return bytes(out)


def write_header(packed: bytes) -> None:
    stride = FRAME_W // 4
    lines = []
    lines.append("/* Generated by tools/pack_indian_head.py — do not edit. */")
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append(
        f"static const uint8_t indian_head_packed[{FRAME_H}][{stride}] = {{"
    )
    for y in range(FRAME_H):
        chunk = packed[y * stride : (y + 1) * stride]
        body = ",".join(f"0x{b:02x}" for b in chunk)
        comma = "," if y + 1 < FRAME_H else ""
        lines.append(f"    {{{body}}}{comma}")
    lines.append("};")
    HEADER.write_text("\n".join(lines) + "\n")


def preview_rgb(fb: np.ndarray) -> Image.Image:
    recon = RECON[fb]
    return Image.fromarray(recon.astype(np.uint8), mode="L")


def main() -> int:
    print(f"rendering {SVG.name} -> {RENDER.name}", flush=True)
    hi_img = render_svg()
    hi = np.array(hi_img, dtype=np.uint8)
    print("quantize card", CARD_W, "x", CARD_H, flush=True)
    card = quantize_card(hi)
    fb = compose_frame(card)
    packed = pack_msb(fb)
    write_header(packed)
    preview_rgb(fb).save(PREVIEW)
    print(f"wrote {HEADER} ({HEADER.stat().st_size} bytes)")
    print(f"wrote {PREVIEW}")
    # occupancy of the four levels
    for name, val in (
        ("OFF", PIXEL_OFF),
        ("DIM", PIXEL_DIM),
        ("NORMAL", PIXEL_NORMAL),
        ("BOLD", PIXEL_BOLD),
    ):
        print(f"  {name:6} {(fb == val).mean() * 100:5.1f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
