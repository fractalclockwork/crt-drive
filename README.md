# crt-drive

Programmable CRT drive: sync and video from a microcontroller for analog CRTs and TVs.

The first target is the Link MC5 terminal (Wyse WY-120 architecture). A Raspberry Pi Pico (RP2040) replaces the CRT drive ASIC (Wyse 211009-02, U4): it synthesizes active-low `/HSYNC` and `/VSYNC` plus active-high dual video (V0 dim, V1 normal) and drives the existing deflection and neck boards. The original CPU, EPROM, and VRAM bus do not need to run.

**Status:** Injection pads and the Pico + 74AHCT125 carrier (KiCad protoboard) are decided. The Dev-Host Docker toolchain is validated (`make smoke` / `make test APP=hello`). **Default firmware is the 60 Hz 80-col measure pattern** (`make build` / `make flash` / `make test` / `make monitor`): GPIO 0–3, box+grid+plus; USB `m` 80/132-col, `r` 60/78 Hz; `a`/`d`/`w`/`s` nudge H/V phase. Four-mode analog drawings: `make build APP=pattern`. Phosphor reel: `make build APP=demos` (same `m`/`r`). Glass TTY: `make build APP=term`. CRT HIL is in progress after isolation and the 74AHCT125.

## Docs

| Doc | Role |
| --- | --- |
| [Project writeup](docs/project-writeup.md) | Methodology, hardware, software, HIL, camera on the glass |
| [Hardware design](docs/hardware-design.md) | Board identity, labeled pads V0/V1/V/H/GND, level shift, 60 Hz then 78 Hz timings |
| [Firmware plan](docs/firmware-plan.md) | Phased PIO / DMA / framebuffer build, open choices |
| [Test patterns](docs/test-pattern-design.md) | `cross60` HIL record (keep); six 78 Hz drawings still on `APP=pattern` |
| [Terminal emulator](docs/terminal-plan.md) | Glass TTY (`apps/term`, `make build APP=term`) |
| [Phosphor demos](docs/demo-plan.md) | Attract reel (`apps/demos`, `make build APP=demos`) |
| [Firmware apps](apps/) | Separate CMake projects: cross60 (default), pattern, term, demos |
| [Toolchains](docs/toolchains.md) | Dev-Host Docker image, volume mount, USB flash (no gateway) |
| [KiCad project](docs/kicad/crt-drive/) | Pico carrier / level-shift protoboard (schematic + jumper layout) |
| [Injector power](docs/kicad/power_supply.md) | +5 V, VSYS Schottky, AHCT buffer rail |
| [PCB drawing](docs/PCB.svg) | Board artwork referenced from the hardware guide |
| [Signal pads](docs/signals_pcb.png) | Solder-side taps next to U4: V0, V1, V, H, GND |

## Dev-Host toolchain (Docker)

Firmware is compiled in the `pico-dev` image with this repository bind-mounted at `/workspace`. The container runs on the Dev-Host; plug the Pico into **this** machine (no Raspberry Pi gateway). Modeled on [fractalclockwork/cede](https://github.com/fractalclockwork/cede) `lab/docker/pico-dev`.

**Full guide:** [docs/toolchains.md](docs/toolchains.md) (setup, USB modes, `hello_pico`, troubleshooting). Short path: [`docker/README.md`](docker/README.md).

```bash
make image          # build crt-drive/pico-dev:local
make smoke          # ARM GCC, CMake, Ninja, Pico SDK, picotool USB
make pico-discover  # identify the Pico on this host's USB
make test APP=hello # build, flash, USB CDC ping (hello_pico)
make build          # 60 Hz 80-col plus (default CRT UF2)
make flash          # load crt_cross60
make monitor        # USB CDC; follows whichever app is running
make build APP=pattern
make build APP=term # glass TTY
make build APP=demos
make help           # all targets
```

Reopen the folder in a container via [`.devcontainer/devcontainer.json`](.devcontainer/devcontainer.json) for the same image inside Cursor/VS Code.

## Phased development

Hardware is ready to drive: lift harness at pads **V0**, **V1**, **V**, **H**, **GND**; level-shift on the Pico carrier (74AHCT125); power from logic-board +5 V / GND. First video mode is **60 Hz 80-col** (32.1 MHz dots, 1024-dot `/HSYNC`, 523 lines); USB `m` is **60 Hz 132-col** (1188×416). **78 Hz** patterns come after that raster is on-glass.

Firmware order (detail in [firmware-plan.md](docs/firmware-plan.md)):

1. Clock + PIO load — Pico PLL; 60 Hz in [`apps/cross60`](apps/cross60/), 78 Hz in [`video/`](video/)
2. Stable `/HSYNC` and `/VSYNC` — 60 Hz wraps 1024 dots / 523 lines (HIL); 78 Hz 1530 / 402 on a scope
3. Pixel SM + packing — 800×416 @ 2 bpp (60 Hz), 800×338 (78 Hz); FIFO stall, trailing off word
4. DMA loop into PIO TX — per-line kick from hsync RX
5. 60 Hz plus — default UF2; center and size on the live CRT (`make test`)
6. 78 Hz test patterns — `make test APP=pattern` after 60 Hz fills
7. Other apps — glass TTY in [`apps/term/`](apps/term/); phosphor reel in [`apps/demos/`](apps/demos/)

## Safety

CRT anode and flyback voltages are lethal. Isolate U4 by lifting the harness wires at the labeled V0, V1, V, H, and GND pads before splicing in RP2040 signals. Do not work on a powered chassis until you know the discharge path for the CRT anode cap.
