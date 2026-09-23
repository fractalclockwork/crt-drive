# pico-dev Docker image

Dev-Host RP2040 toolchain for this repo (no Pi gateway). Edit on the Dev-Host. This image is called into from that host with `make`; `exit` leaves a `make shell` session.

**How to use it:** [docs/toolchains.md](../docs/toolchains.md)

Quick path from the repository root:

```bash
make image          # crt-drive/pico-dev:local
make smoke          # compilers + Pico SDK + picotool USB
make pico-discover  # Pico on this machine's USB
make test APP=hello # build, flash, CDC ping
make build          # 60 Hz 80-col plus (default)
make monitor        # USB CDC for whichever app is running
make help           # all targets
```

Sources: [`pico-dev/Dockerfile`](pico-dev/Dockerfile), pins in [`TOOLCHAIN_VERSIONS`](TOOLCHAIN_VERSIONS), Compose in this directory. Make wrappers live in [`Makefile`](Makefile) and the repo-root [`Makefile`](../Makefile).
