#!/usr/bin/env bash
# ERC + DRC for docs/kicad/crt-drive. Prints reports; does not hide expected protoboard exceptions.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
PROJ="$ROOT/docs/kicad/crt-drive"
SCH="$PROJ/crt-drive.kicad_sch"
PCB="$PROJ/crt-drive.kicad_pcb"
OUT="${TMPDIR:-/tmp}/crt-drive-kicad"

if ! command -v kicad-cli >/dev/null 2>&1; then
  echo "kicad-cli not on PATH" >&2
  exit 127
fi

mkdir -p "$OUT"

echo "=== kicad-cli $(kicad-cli --version) ==="
echo "=== ERC $SCH ==="
kicad-cli sch erc --format report --severity-all \
  --output "$OUT/erc.rpt" "$SCH"
cat "$OUT/erc.rpt"

echo "=== DRC $PCB ==="
kicad-cli pcb drc --format report --severity-all --schematic-parity \
  --output "$OUT/drc.rpt" "$PCB"
cat "$OUT/drc.rpt"

echo "=== reports: $OUT/erc.rpt $OUT/drc.rpt ==="
