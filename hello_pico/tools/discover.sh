#!/usr/bin/env bash
# Print Raspberry Pi Pico USB / CDC nodes on this Dev-Host.
set -euo pipefail

echo "=== lsusb 2e8a (Raspberry Pi) ==="
if ! lsusb -d 2e8a:; then
  echo "(none)"
fi

echo "=== /dev/serial/by-id ==="
shopt -s nullglob
ids=(/dev/serial/by-id/usb-Raspberry_Pi*)
if ((${#ids[@]})); then
  ls -l "${ids[@]}"
else
  echo "(none — BOOTSEL has no ACM; application CDC should show usb-Raspberry_Pi_Pico_*)"
fi

echo "=== ttyACM ==="
acms=(/dev/ttyACM*)
if ((${#acms[@]})); then
  ls -l "${acms[@]}"
else
  echo "(none)"
fi

echo "=== mode ==="
if lsusb -d 2e8a:0003 >/dev/null 2>&1; then
  echo "BOOTSEL (2e8a:0003) — picotool can load a UF2; no USB serial until the app runs."
elif lsusb -d 2e8a:000a >/dev/null 2>&1; then
  echo "Application USB CDC (2e8a:000a) — firmware is running (hello_pico / Pico SDK stdio)."
  echo "This is the expected state after make hello-test. picotool info needs BOOTSEL;"
  echo "serial ping uses the tty above (make hello-serial)."
elif lsusb -d 2e8a: >/dev/null 2>&1; then
  echo "Raspberry Pi USB device present, but not the usual Pico CDC/BOOTSEL product IDs."
else
  echo "No Pico on USB."
fi
