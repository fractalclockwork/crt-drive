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
3. Compile: `make build` (60 Hz plus, default), `make build APP=pattern`, `make build APP=term`, `make build APP=demos`, or `make build APP=hello` (bring-up).
4. Gate: `make test APP=hello` (unique `digest=` + `pong`). 60/78 Hz measure: `make test` (`crt-cross60` banner) then `make monitor` (keys `1`–`4`, `m` 80/132, `r` 60/78, `a`/`d`, `w`/`s`). Analog drawings: `make test APP=pattern` (same `m`/`r`; keys `1`–`6`). Glass TTY: `make test APP=term`. Phosphor reel: `make test APP=demos` (CDC `2` → `scene=radar`; same `m`/`r`; scenes `1`–`6`). CDC attach for any running image: `make monitor`.
5. Evidence is CDC banner (`digest=` where the app prints one), not ninja exit 0 alone. No board is an explicit HIL skip, not a pass.

## USB and Docker

Flash goes through existing [`hello_pico/tools/flash.sh`](../../../hello_pico/tools/flash.sh) (`reboot -u -f`, then `load -x`). Application unique ID does not match RP2 Boot serial.

Make uses [`docker/with-docker.sh`](../../../docker/with-docker.sh) for the docker socket. Do not reinstall Docker.

## Safety

`make test APP=hello` is the USB Pico on the Dev-Host, not permission to drive a live CRT. Isolation notes stay in the README.

## More detail

[`docs/toolchains.md`](../../../docs/toolchains.md)
