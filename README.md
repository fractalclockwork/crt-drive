# crt-drive

Programmable CRT drive: sync and video from a microcontroller for analog CRTs and TVs.

The first target is the Link MC5 terminal (Wyse WY-120 architecture). A Raspberry Pi Pico (RP2040) replaces the CRT drive ASIC (Wyse 211009-02, U4): it synthesizes active-low `/HSYNC` and `/VSYNC` plus active-high dual video (V0 dim, V1 normal) and drives the existing deflection and neck boards. The original CPU, EPROM, and VRAM bus do not need to run.

**Status:** Injection pads and the Pico + 74AHCT125 carrier (KiCad protoboard) are decided. The Dev-Host Docker toolchain is validated (`make smoke` / `make hello-test`). CRT firmware is started: 78 Hz crosshatch on GPIO 0–3 (`make build` / `make flash`). `/HSYNC`, `/VSYNC`, and V0/V1 checked out on a scope. Other patterns are later; do not drive a CRT until isolation.

## Docs

| Doc | Role |
| --- | --- |
| [Hardware design](docs/hardware-design.md) | Board identity, labeled pads V0/V1/V/H/GND, level shift, 78 Hz timings |
| [Firmware plan](docs/firmware-plan.md) | Phased PIO / DMA / framebuffer build, open choices |
| [Test patterns](docs/test-pattern-design.md) | Crosshatch, intensity, focus, full-on geometry (phase 5) |
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
make hello-test     # build, flash, USB CDC ping (hello_pico)
make help           # all targets
```

Reopen the folder in a container via [`.devcontainer/devcontainer.json`](.devcontainer/devcontainer.json) for the same image inside Cursor/VS Code.

## Phased development

Hardware is ready to drive: lift harness at pads **V0**, **V1**, **V**, **H**, **GND**; level-shift on the Pico carrier (74AHCT125); power from logic-board +5 V / GND. First video mode is **78 Hz** (48.000 MHz dots, `/HSYNC` 31.373 kHz, 78.041 Hz). **60 Hz** is optional later.

Firmware order (detail in [firmware-plan.md](docs/firmware-plan.md)):

1. Clock + PIO load — done (`main.c`, three `.pio` files)
2. Stable `/HSYNC` and `/VSYNC` — PIO wraps are 1530 dots / 402 lines; rates confirmed on a scope
3. Pixel SM + packing — done (active 800×338 @ 2 bpp, FIFO stall, trailing off word)
4. DMA loop into PIO TX — done (per-line kick from hsync RX)
5. Test patterns — crosshatch on the wire; intensity / focus / full-on later; CRT after isolation
6. Optional 60 Hz

Three PIO SMs and stall blanking are decided. Porch *widths* stay a working hypothesis until a WY-120 capture.

## Safety

CRT anode and flyback voltages are lethal. Isolate U4 by lifting the harness wires at the labeled V0, V1, V, H, and GND pads before splicing in RP2040 signals. Do not work on a powered chassis until you know the discharge path for the CRT anode cap.
