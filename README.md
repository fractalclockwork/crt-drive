# Link MC5 / Wyse WY-120 CRT Drive Replacement

Replace the Link MC5 terminal CRT drive (Wyse 211009-02 ASIC U4) with a Raspberry Pi Pico (RP2040). The Pico synthesizes active-low `/HSYNC` and `/VSYNC` plus active-high dual video (V0 dim, V1 normal) and drives the existing deflection and neck boards. The original CPU, EPROM, and VRAM bus do not need to run.

**Status:** Planning. [`CMakeLists.txt`](CMakeLists.txt) sketches a Pico SDK build (`main.c`, three `.pio` files). Those sources are not in the tree yet.

## Docs

| Doc | Role |
| --- | --- |
| [Hardware design](docs/hardware-design.md) | Board identity, signals, level shift, injection points, 78 Hz timings |
| [Firmware plan](docs/firmware-plan.md) | PIO / DMA / framebuffer phases, test patterns, open design choices |
| [PCB drawing](docs/PCB.svg) | Board artwork referenced from the hardware guide |

**78 Hz** is the first target (48.000 MHz dots, `/HSYNC` 31.356 kHz). **60 Hz** is a later optional mode; timings are TBD.

## Safety

CRT anode and flyback voltages are lethal. Isolate U4 video and sync outputs before injecting RP2040 signals. Do not work on a powered chassis until you know the discharge path for the CRT anode cap.
# crt-drive
