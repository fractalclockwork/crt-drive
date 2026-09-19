# crt-drive

Programmable CRT drive: sync and video from a microcontroller for analog CRTs and TVs.

The first target is the Link MC5 terminal (Wyse WY-120 architecture). A Raspberry Pi Pico (RP2040) replaces the CRT drive ASIC (Wyse 211009-02, U4): it synthesizes active-low `/HSYNC` and `/VSYNC` plus active-high dual video (V0 dim, V1 normal) and drives the existing deflection and neck boards. The original CPU, EPROM, and VRAM bus do not need to run.

**Status:** Hardware injection path is decided. Firmware is phased and not started yet (`main.c` / `.pio` not in the tree). Next work is on a Pico SDK build host.

## Docs

| Doc | Role |
| --- | --- |
| [Hardware design](docs/hardware-design.md) | Board identity, labeled pads V0/V1/V/H/GND, level shift, 78 Hz timings |
| [Firmware plan](docs/firmware-plan.md) | Phased PIO / DMA / framebuffer build, test patterns, open choices |
| [PCB drawing](docs/PCB.svg) | Board artwork referenced from the hardware guide |
| [Signal pads](docs/signals_pcb.png) | Solder-side taps next to U4: V0, V1, V, H, GND |

## Phased development (next host)

Hardware is ready to drive: lift harness at pads **V0**, **V1**, **V**, **H**, **GND**; level-shift with a 74AHCT125; power from logic-board +5 V / GND. First video mode is **78 Hz** (48.000 MHz dots, `/HSYNC` 31.356 kHz). **60 Hz** is optional later.

Firmware order (detail and sketches in [firmware-plan.md](docs/firmware-plan.md)):

1. Clock + PIO load (build `main.c` / three `.pio` files against current CMake)
2. Stable `/HSYNC` and `/VSYNC` (fix draft cycle counts on a scope)
3. Pixel SM + framebuffer packing (choose blanking strategy)
4. DMA loop into PIO TX
5. Test patterns on the CRT
6. Optional 60 Hz

Open before phase 3 code freezes: three PIO SMs vs combined timing SM; stall vs padded-raster blanking; confirm 78 Hz porches on hardware.

## Safety

CRT anode and flyback voltages are lethal. Isolate U4 by lifting the harness wires at the labeled V0, V1, V, H, and GND pads before splicing in RP2040 signals. Do not work on a powered chassis until you know the discharge path for the CRT anode cap.
