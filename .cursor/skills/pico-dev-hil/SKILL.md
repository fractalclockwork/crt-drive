---
name: pico-dev-hil
description: >-
  Builds, flashes, and USB-CDC verifies RP2040 firmware through this repo's
  pico-dev Docker image and Make targets (no Pi gateway). Use when compiling
  Pico/RP2040 code, flashing a Pico, hello_pico, picotool, UF2, USB serial,
  HIL, or CRT firmware on the Dev-Host.
---

# pico-dev hardware-in-the-loop

## Single toolchain

From the repository root: `make help`. Never `apt install gcc-arm-none-eabi`, never clone pico-sdk onto the host, never `cmake` outside the container, never `picotool load -f`.

## HIL loop

1. `make pico-discover` — `2e8a:000a` CDC (app running), `2e8a:0003` BOOTSEL (load), none = skip HIL and say so.
2. Edit sources in this git tree (bind-mounted at `/workspace` in the container).
3. Compile: `make hello-build` (bring-up), `make build` (patterns), or `make term-build` (glass TTY).
4. Gate: `make hello-test` (unique `digest=` banner + `pong`). CRT glass TTY: `make term-test` (`digest=` + UTF-8 line, or explicit skip if no Pico).
5. Evidence is CDC `digest=`, not ninja exit 0 alone.

## USB and Docker

Flash goes through existing [`hello_pico/tools/flash.sh`](../../../hello_pico/tools/flash.sh) (`reboot -u -f`, then `load -x`). Application unique ID does not match RP2 Boot serial.

Make uses [`docker/with-docker.sh`](../../../docker/with-docker.sh) for the docker socket. Do not reinstall Docker.

## Safety

`hello-test` is the USB Pico on the Dev-Host, not permission to drive a live CRT. Isolation notes stay in the README.

## More detail

[`docs/toolchains.md`](../../../docs/toolchains.md)
