# Dev-Host Pico SDK container

RP2040 firmware is built inside Docker **on this machine**. There is no Raspberry Pi hardware gateway: plug the Pico into the Dev-Host USB port for `picotool` and USB CDC serial.

The image follows [fractalclockwork/cede](https://github.com/fractalclockwork/cede) `lab/docker/pico-dev` (Pico only — no Arduino, orchestration, or Pi-gateway services).

**Pins:** [`docker/TOOLCHAIN_VERSIONS`](../docker/TOOLCHAIN_VERSIONS) (Pico SDK and picotool **2.1.1** on `debian:bookworm-slim`).

```text
Dev-Host (editor / Git / Make / Docker)
    │  bind-mount this repo → /workspace
    ▼
pico-dev  (crt-drive/pico-dev:local)
    ├── gcc-arm-none-eabi, cmake, ninja
    ├── Pico SDK  → /opt/pico-sdk
    └── picotool  → USB Pico on this host
```

## First-time setup

1. Install Docker Engine with Compose v2.
2. Put your user in the `docker` group, then **log out and back in** (a nested `bash` is not enough):

   ```bash
   sudo usermod -aG docker "$USER"
   ```

3. Check this **shell** (not only the account):

   ```bash
   getent group docker          # account: docker:x:…:plastic
   id                           # this session must list docker
   ls -l /var/run/docker.sock   # srw-rw---- root docker
   docker info >/dev/null && echo ok
   ```

   If `id plastic` shows `docker` but `id` / `docker info` do not, the terminal started before the group add. Make uses [`docker/with-docker.sh`](../docker/with-docker.sh) (`sg docker`) in that case. A new login still avoids the workaround.

4. From the repository root:

   ```bash
   make image    # build crt-drive/pico-dev:local (slow once)
   make smoke    # ARM GCC, CMake, Ninja, SDK path, picotool with USB
   ```

`make smoke` does not need a Pico attached.

## Daily use

All of these are run from the **repository root**. Equivalent: `make -C docker <target>`. `make help` lists them.

| Command | What it does |
|---------|----------------|
| `make image` | Build/refresh `crt-drive/pico-dev:local` for this CPU (`linux/amd64` or `linux/arm64`) |
| `make smoke` | Toolchain check inside the image (no board) |
| `make shell` | Interactive bash; repo at `/workspace`; `PICO_SDK_PATH` set |
| `make shell-usb` | Same, privileged, host `/dev` for picotool and ACM |
| `make pico-discover` | Host `lsusb` / `/dev/serial/by-id` / tty; skip picotool unless BOOTSEL |
| `make hello-test` | Build, flash, and USB ping `hello_pico` (unique `digest=`) |
| `make hello-build` / `hello-flash` / `hello-serial` | Steps of `hello-test` |
| `make build` | CRT firmware at repo root (`main.c` + `.pio` — not in tree yet) |
| `make rebuild` | Wipe `build/` then CRT cmake/ninja |
| `make flash` | Load `build/crt_drive.uf2` with the same flash helper as hello |
| `make picotool-info` | `picotool info` (needs BOOTSEL) |

Build artifacts land on the bind mount (`hello_pico/build/`, repo `build/`), owned as your uid for non-USB `compose run`.

Overrides: `PICO_BOARD=pico` (or `pico_w`), `CMAKE_BUILD_TYPE=Release`, `HELLO_IMAGE_ID=…`, `PICO_PORT=/dev/ttyACM0`.

## Hardware bring-up (`hello_pico`)

[`hello_pico/`](../hello_pico/) is USB-only firmware so the container can flash and talk to a Pico **without** CRT PIO sources.

```bash
make pico-discover   # board plugged into this host
make hello-test      # unique digest → UF2 → picotool → CDC banner + pong
```

Expected serial (digest changes every `hello-test`):

```text
crt-drive hello_pico ok digest=<id>
pong digest=<id>
```

The firmware prints that banner on USB CDC @ 115200, blinks the onboard LED, and replies `pong` to `p`. Host check: [`hello_pico/tools/serial_ping.py`](../hello_pico/tools/serial_ping.py). Flash: [`hello_pico/tools/flash.sh`](../hello_pico/tools/flash.sh).

### USB modes

| `lsusb` | Meaning | Serial node |
|---------|---------|-------------|
| `2e8a:000a` Raspberry Pi Pico | Application USB CDC (hello / Pico SDK stdio) | `/dev/serial/by-id/usb-Raspberry_Pi_Pico_<unique>-if00` → `ttyACM*` |
| `2e8a:0003` Raspberry Pi RP2 Boot | BOOTSEL | none (picotool load) |

`make pico-discover` treats CDC as success. `picotool info -a` only runs in BOOTSEL.

Do **not** use `picotool load -f` against CDC firmware: application unique ID (example bench board `E660C06213580D29`) does not match RP2 Boot after reboot, so picotool waits for the wrong serial. `flash.sh` uses `picotool reboot -u -f` (USB BOOTSEL), then `picotool load -x` once BOOTSEL is visible.

If force-reboot fails: hold **BOOTSEL**, plug USB, `make pico-discover` should show `2e8a:0003`, then `make hello-flash`.

`ttyACM*` is `root:dialout`. Flash/serial Compose runs as root with `/dev` mounted so you do not need group `dialout` for `make hello-test`. Host tools (`minicom`, `cat /dev/ttyACM0`) do.

## Image layout

| Path | Role |
|------|------|
| [`docker/pico-dev/Dockerfile`](../docker/pico-dev/Dockerfile) | Debian + ARM GCC + SDK clone + picotool install (libusb) |
| [`docker/docker-compose.yml`](../docker/docker-compose.yml) | Service `pico-dev`, volume `..:/workspace` |
| [`docker/docker-compose.usb.yml`](../docker/docker-compose.usb.yml) | Privileged `/dev`, root user — flash and serial only |
| [`docker/docker-compose.platform-amd64.yml`](../docker/docker-compose.platform-amd64.yml) / `…arm64.yml` | Explicit `platform:` for this host |
| [`docker/with-docker.sh`](../docker/with-docker.sh) | `sg docker` when the account is in the group but this shell is not |
| [`pico_sdk_import.cmake`](../pico_sdk_import.cmake) | Official SDK locator; container sets `PICO_SDK_PATH` |
| [`.devcontainer/devcontainer.json`](../.devcontainer/devcontainer.json) | Same Compose stack (including USB) for Cursor / VS Code |

| In the image | |
|--------------|--|
| Cross GCC | `gcc-arm-none-eabi`, newlib, libstdc++ |
| SDK | `/opt/pico-sdk` |
| picotool | `/usr/local` so CMake `find_package(picotool)` and USB load/reboot work |

USB Compose is **not** merged for `make smoke` / `make hello-build` so ordinary compiles do not need privileged devices.

## Agents

Firmware and USB work use the existing Make targets, not a host SDK. Cursor: always-on rule [`.cursor/rules/pico-dev-toolchain.mdc`](../.cursor/rules/pico-dev-toolchain.mdc) and skill [`.cursor/skills/pico-dev-hil/SKILL.md`](../.cursor/skills/pico-dev-hil/SKILL.md) (`make help`, then `pico-discover` / `hello-test`).

## Cursor / VS Code

Reopen the folder in a container via [`.devcontainer/devcontainer.json`](../.devcontainer/devcontainer.json). That uses `docker-compose.yml` + `docker-compose.usb.yml` and workspace `/workspace`.

## Troubleshooting

| Symptom | Likely cause |
|---------|----------------|
| `permission denied … docker.sock` | Session missing group `docker`. Check `id`; log out/in, or rely on `with-docker.sh`. |
| `\ntcmake: command not found` (or `\ntset`) | Old `with-docker.sh` quoting through dash. Current script POSIX-quotes; pull/update that file. |
| `picotool info` “no BOOTSEL” but `2e8a:000a` | Firmware is running. Use `make hello-serial`, not `picotool info`. |
| `load -f` times out after reboot | CDC serial ≠ BOOTSEL serial. Use `make hello-flash` / `flash.sh`. |
| `Pico did not enter BOOTSEL` | Need `reboot -u -f`, or hold BOOTSEL on plug-in. |
| `make build` missing `main.c` / `.pio` | CRT firmware not started; use `make hello-test` until phase 1. |

## Related

- [Firmware plan](firmware-plan.md) — PIO / DMA once the toolchain and USB path are trusted
- [Hardware design](hardware-design.md) — GPIO map and 78 Hz timings
- [KiCad carrier](kicad/crt-drive/) — Pico + 74AHCT125 protoboard
