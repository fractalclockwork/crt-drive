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

Development is on the Dev-Host. pico-dev is only entered from that host, via repo-root Make (`make help`). Do not leave a shell open in the container, and do not reopen the folder inside it. `make shell` and `make shell-usb` return to the host on `exit`.

Never `apt install gcc-arm-none-eabi`, never clone pico-sdk onto the host, never `cmake` outside the container, never `picotool load -f`.

## HIL loop

1. `make pico-discover` — `2e8a:000a` CDC (app running), `2e8a:0003` BOOTSEL (load), none = skip HIL and say so.
2. Edit sources on the Dev-Host. Make bind-mounts this tree at `/workspace` only for the call into pico-dev.
3. Compile: `make build` (60 Hz plus, default), `make build APP=pattern`, `make build APP=term`, `make build APP=demos`, `make build APP=beam`, `make build APP=vtty`, `make build APP=rick`, or `make build APP=hello` (bring-up).
4. Gate: `make test APP=hello` (unique `digest=` + `pong`). 60/78 Hz measure: `make test` (`crt-cross60` banner) then `make monitor` (keys `1`–`4`, `m` 80/132, `r` 60/78, `a`/`d`, `w`/`s`). Analog drawings: `make test APP=pattern` (same `m`/`r`; keys `1`–`6`). Glass TTY: `make test APP=term` (BOOTSEL cycles 78/60 × 80/132; host bytes stay the session). Phosphor reel: `make test APP=demos` (CDC `2` → `scene=radar`; same `m`/`r`; scenes `1`–`6`). Beam-line tests: `make test APP=beam` (CDC `?` → `line=` and `then=` differ; keys `1`–`5`). Video TTY: `make test APP=vtty` (banner, then framed `Hello` and `SHOW` of the linked 128×64 card). Pages after that: `uv run python tools/vtty.py` (Pillow is in the `uv` venv; `uv sync`). How to use the session is [docs/vtty.md](../../../docs/vtty.md). Rickroll: `make test APP=rick` (CDC `?` → `frame=` and `delay=`; advances in vertical blank at the GIF delay, `n` next, `r` 60/78). CDC attach for any running image: `make monitor`.
5. Evidence is CDC banner (`digest=` where the app prints one), not ninja exit 0 alone. No board is an explicit HIL skip, not a pass.
6. Camera HIL always starts with `make camera-check` before any glass picture is trusted. That live C310 view selects crosshatch on `crt-pattern` or the measure box on `crt-cross60`, and it exits 0 only when that raster is visible and inside the frame on every side. The overlay is the measurement: yellow is the dark face, green is the phosphor, both in camera pixels. `make camera` is the same view left open. A clipped tube or a corner of text is not a pass. Daytime light only confirms the tube is fully in frame. Walking the raster out to the chassis blanking border — the dark edge where the beam is off and a pixel will not light — is a night session in a dark room, brightness and contrast at maximum. The procedure is in [project-writeup.md](../../../docs/project-writeup.md) under Camera on glass.

## USB and Docker

Flash goes through existing [`hello_pico/tools/flash.sh`](../../../hello_pico/tools/flash.sh) (`reboot -u -f`, then `load -x`). Application unique ID does not match RP2 Boot serial.

Make uses [`docker/with-docker.sh`](../../../docker/with-docker.sh) for the docker socket. Do not reinstall Docker.

## Safety

`make test APP=hello` is the USB Pico on the Dev-Host, not permission to drive a live CRT. Isolation notes stay in the README.

## More detail

[`docs/toolchains.md`](../../../docs/toolchains.md)
