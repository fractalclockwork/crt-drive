#!/usr/bin/env bash
# Flash a UF2 from the pico-dev container. Do not use `picotool load -f`:
# application CDC serial (e.g. E660…) does not match RP2 Boot serial, so -f
# reboots into BOOTSEL then times out looking for the old serial.
set -euo pipefail

UF2="${1:?usage: flash.sh <file.uf2>}"
test -f "$UF2"

in_bootsel() {
  picotool info >/dev/null 2>&1
}

if ! in_bootsel; then
  echo "not in BOOTSEL — requesting USB BOOTSEL reboot from application firmware" >&2
  picotool reboot -u -f || true
  for _ in $(seq 1 40); do
    if in_bootsel; then
      break
    fi
    sleep 0.25
  done
fi

if ! in_bootsel; then
  echo "Pico did not enter BOOTSEL. Hold BOOTSEL, plug USB, then retry." >&2
  picotool info -a || true
  exit 1
fi

picotool load -x "$UF2"
