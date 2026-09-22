#!/usr/bin/env python3
"""Emit a C byte array from a TTF so the face can live in RP2040 flash."""

from __future__ import annotations

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: ttf_to_c.py <font.ttf> <out.c> <symbol>", file=sys.stderr)
        return 2
    ttf = Path(sys.argv[1])
    out = Path(sys.argv[2])
    symbol = sys.argv[3]
    data = ttf.read_bytes()
    lines = [
        f"/* Generated from {ttf.name} — do not edit. */",
        "#include <stdint.h>",
        f"const uint8_t {symbol}[] = {{",
    ]
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i : i + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append(f"const unsigned int {symbol}_len = {len(data)};")
    out.write_text("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
