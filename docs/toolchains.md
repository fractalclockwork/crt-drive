# Dev-Host Pico SDK container

Edit, Git, and Make run on the Dev-Host. pico-dev is called into from that host only; the container is the compiler and `picotool`, not the editor. There is no Raspberry Pi hardware gateway: plug the Pico into the Dev-Host USB port for `picotool` and USB CDC serial.

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
| `make shell` | Call into the image (repo at `/workspace`, `PICO_SDK_PATH` set). `exit` returns to the Dev-Host |
| `make shell-usb` | Same call-in, privileged, host `/dev` for picotool and ACM. `exit` returns to the Dev-Host |
| `make pico-discover` | Host `lsusb` / `/dev/serial/by-id` / tty; skip picotool unless BOOTSEL |
| `make hello-test` | Alias for `make test APP=hello`: build, flash, USB ping `hello_pico` (unique `digest=`) |
| `make hello-build` / `hello-flash` / `hello-monitor` | Steps of `hello-test` |
| `make build` | Default CRT app: 60 Hz 80-col plus ([`apps/cross60`](../apps/cross60/) → `build/cross60/crt_cross60.uf2`) |
| `make rebuild` | Wipe `build/<app>` then build that app (`APP=cross60` default) |
| `make flash` | Load the current `APP` UF2 (`crt_cross60` unless `APP=…`) |
| `make monitor` | USB CDC for **whatever app is running** (banner detect). Optional `APP=` forces a dialect. |
| `make test` | Unique id + flash + HIL for `APP` (default `cross60` → `crt-cross60` banner; skip if no Pico) |
| `make build APP=pattern` / `flash` / `test` | 78 Hz analog-setup patterns ([`apps/pattern`](../apps/pattern/) → `build/pattern/crt_pattern.uf2`) |
| `make build APP=term` / `flash` / `test` | Glass TTY ([`apps/term`](../apps/term/) → `build/term/crt_term.uf2`) |
| `make term-build` / `term-flash` / `term-monitor` / `term-test` | Aliases for the term app |
| `make build APP=demos` / `flash` / `test` | Phosphor reel, four-mode scanout ([`apps/demos`](../apps/demos/) → `build/demos/crt_demos.uf2`) |
| `make build APP=beam` / `flash` / `test` | Beam-line update tests ([`apps/beam`](../apps/beam/) → `build/beam/crt_beam.uf2`). CDC `?` must show `line=` and `then=` differ |
| `make build APP=vtty` / `flash` / `test` | Video TTY ([`apps/vtty`](../apps/vtty/) → `build/vtty/crt_vtty.uf2`). Banner, then a framed `Hello` and `SHOW` of the linked 128×64 card. Host tool: `uv run python tools/vtty.py` (`uv sync` installs Pillow into `.venv`) |
| `make demos-build` / `demos-flash` / `demos-monitor` / `demos-test` | Aliases; `demos-test` is unique `digest=` then CDC `2` → `scene=radar`, or HIL skip |
| `make build APP=cross60` / `flash` / `test` | Same as default; 60 Hz 1-pixel plus |
| `make picotool-info` | `picotool info` (needs BOOTSEL) |
| `make camera` | Live Logitech C310 view on this host (not in the Pico container). Selects crosshatch on `crt-pattern`, or the measure box on `crt-cross60`. Overlay: yellow glass, green phosphor, both in camera pixels |
| `make camera-check` | Same view. Exits 0 only after the raster stays fully inside the picture for one second. Every camera HIL starts here. Edge readings against the blanking border are a dark-room session; see [project-writeup.md](project-writeup.md) |

Build artifacts land on the bind mount (`hello_pico/build/`, `build/<app>/`), owned as your uid for non-USB `compose run`.

Overrides: `PICO_BOARD=pico` (or `pico_w`), `APP=cross60` (or `pattern`, `term`, `demos`, `beam`, `vtty`, `hello`), `CMAKE_BUILD_TYPE=Release`, `IMAGE_ID=…`, `PICO_PORT=/dev/ttyACM0`.

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

The firmware prints that banner on USB CDC @ 115200, blinks the onboard LED, and replies `pong` to `p`. Host check: [`tools/monitor.py`](../tools/monitor.py) (`make monitor` / `make test APP=hello`). Flash: [`hello_pico/tools/flash.sh`](../hello_pico/tools/flash.sh).

### USB modes

| `lsusb` | Meaning | Serial node |
|---------|---------|-------------|
| `2e8a:000a` Raspberry Pi Pico | Application USB CDC (hello / Pico SDK stdio) | `/dev/serial/by-id/usb-Raspberry_Pi_Pico_<unique>-if00` → `ttyACM*` |
| `2e8a:0003` Raspberry Pi RP2 Boot | BOOTSEL | none (picotool load) |

`make pico-discover` treats CDC as success. `picotool info -a` only runs in BOOTSEL.

Do **not** use `picotool load -f` against CDC firmware: application unique ID (example bench board `E660C06213580D29`) does not match RP2 Boot after reboot, so picotool waits for the wrong serial. `flash.sh` uses `picotool reboot -u -f` (USB BOOTSEL), then `picotool load -x` once BOOTSEL is visible.

If force-reboot fails: hold **BOOTSEL**, plug USB, `make pico-discover` should show `2e8a:0003`, then `make flash APP=hello`.

`ttyACM*` is `root:dialout`. Flash/monitor Compose runs as root with `/dev` mounted so you do not need group `dialout` for `make test APP=hello`. Host tools do, including `uv run python tools/vtty.py`, `minicom`, and `cat /dev/ttyACM0`. Add the login with `sudo usermod -aG dialout "$USER"` and open a new shell.

## Image layout

| Path | Role |
|------|------|
| [`docker/pico-dev/Dockerfile`](../docker/pico-dev/Dockerfile) | Debian + ARM GCC + SDK clone + picotool install (libusb) |
| [`docker/docker-compose.yml`](../docker/docker-compose.yml) | Service `pico-dev`, volume `..:/workspace` |
| [`docker/docker-compose.usb.yml`](../docker/docker-compose.usb.yml) | Privileged `/dev`, root user — flash and serial only |
| [`docker/docker-compose.platform-amd64.yml`](../docker/docker-compose.platform-amd64.yml) / `…arm64.yml` | Explicit `platform:` for this host |
| [`docker/with-docker.sh`](../docker/with-docker.sh) | `sg docker` when the account is in the group but this shell is not |
| [`pico_sdk_import.cmake`](../pico_sdk_import.cmake) | Official SDK locator; container sets `PICO_SDK_PATH` |

| In the image | |
|--------------|--|
| Cross GCC | `gcc-arm-none-eabi`, newlib, libstdc++ |
| SDK | `/opt/pico-sdk` |
| picotool | `/usr/local` so CMake `find_package(picotool)` and USB load/reboot work |

USB Compose is **not** merged for `make smoke` / `make build APP=hello` so ordinary compiles do not need privileged devices.

## Agents

Firmware and USB work use the existing Make targets from the Dev-Host, not a host SDK and not an editor session inside pico-dev. Cursor: always-on rule [`.cursor/rules/pico-dev-toolchain.mdc`](../.cursor/rules/pico-dev-toolchain.mdc) and skill [`.cursor/skills/pico-dev-hil/SKILL.md`](../.cursor/skills/pico-dev-hil/SKILL.md) (`make help`, then `pico-discover` / `make test APP=hello`).

## Troubleshooting

| Symptom | Likely cause |
|---------|----------------|
| `permission denied … docker.sock` | Session missing group `docker`. Check `id`; log out/in, or rely on `with-docker.sh`. |
| `\ntcmake: command not found` (or `\ntset`) | Old `with-docker.sh` quoting through dash. Current script POSIX-quotes; pull/update that file. |
| `picotool info` “no BOOTSEL” but `2e8a:000a` | Firmware is running. Use `make monitor` / `make hello-monitor`, not `picotool info`. |
| `load -f` times out after reboot | CDC serial ≠ BOOTSEL serial. Use `make hello-flash` / `flash.sh`. |
| `Pico did not enter BOOTSEL` | Need `reboot -u -f`, or hold BOOTSEL on plug-in. |
| `make build` missing `apps/pattern` / `video/*.pio` | Unexpected now — those files are under `apps/pattern/` and `video/`. |

## Related

- [Firmware plan](firmware-plan.md) — PIO / DMA once the toolchain and USB path are trusted
- [Terminal emulator](terminal-plan.md) — glass TTY app (`make build APP=term`)
- [Phosphor demos](demo-plan.md) — attract reel (`make build APP=demos`)
- [Hardware design](hardware-design.md) — GPIO map and 78 Hz timings
- [KiCad carrier](kicad/crt-drive/) — Pico + 74AHCT125 protoboard
