# Run from repo root: make -C docker <target>
# Or from docker/: make <target>
#
# Template: fractalclockwork/cede lab/docker/Makefile (pico-dev / Dev-Host only).
# No Raspberry Pi gateway: flash with `make flash` while the Pico is on this machine.

DOCKER_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
REPO_ROOT := $(abspath $(DOCKER_DIR)/..)

include $(DOCKER_DIR)TOOLCHAIN_VERSIONS
export BASE_IMAGE_DEBIAN PICO_SDK_VERSION PICOTOOL_VERSION

DEV_UID ?= $(shell id -u)
DEV_GID ?= $(shell id -g)
export DEV_UID DEV_GID

# usermod -aG docker does not update already-running shells; sg when needed.
WITH_DOCKER := $(DOCKER_DIR)with-docker.sh

COMPOSE := $(WITH_DOCKER) docker compose --env-file $(DOCKER_DIR)TOOLCHAIN_VERSIONS -f $(DOCKER_DIR)docker-compose.yml
COMPOSE_USB := $(COMPOSE) -f $(DOCKER_DIR)docker-compose.usb.yml
COMPOSE_PLATFORM = $(WITH_DOCKER) docker compose --env-file $(DOCKER_DIR)TOOLCHAIN_VERSIONS \
	-f $(DOCKER_DIR)docker-compose.yml \
	-f $(DOCKER_DIR)docker-compose.platform-$(PLATFORM_TAG).yml

UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),x86_64)
  HOST_PLATFORM := linux/amd64
else ifeq ($(UNAME_M),aarch64)
  HOST_PLATFORM := linux/arm64
else ifeq ($(UNAME_M),arm64)
  HOST_PLATFORM := linux/arm64
else
  HOST_PLATFORM := linux/$(UNAME_M)
endif

ifeq ($(HOST_PLATFORM),linux/amd64)
  PLATFORM_TAG := amd64
else ifeq ($(HOST_PLATFORM),linux/arm64)
  PLATFORM_TAG := arm64
else
  PLATFORM_TAG :=
endif

PICO_BOARD ?= pico
BUILD_DIR ?= /workspace/build
UF2 ?= $(BUILD_DIR)/crt_drive.uf2
HELLO_BUILD_DIR ?= /workspace/hello_pico/build
HELLO_UF2 ?= $(HELLO_BUILD_DIR)/hello_pico.uf2
HELLO_IMAGE_ID ?=
CMAKE_BUILD_TYPE ?= Release

.PHONY: help print-host-platform build-images build-images-host smoke build rebuild \
	shell shell-usb flash serial serial-check picotool-info setup-binfmt pico-discover \
	hello-build hello-flash hello-serial hello-test

help:
	@echo "crt-drive pico-dev (Dev-Host, no gateway)"
	@echo "  make build-images   — build crt-drive/pico-dev:local for this machine"
	@echo "  make smoke          — compilers, SDK path, picotool with USB support"
	@echo "  make build          — cmake/ninja CRT firmware (test patterns)"
	@echo "  make rebuild        — wipe build/ then build"
	@echo "  make shell          — interactive shell in pico-dev"
	@echo "  make shell-usb      — same, with /dev/bus/usb (privileged)"
	@echo "  make flash          — picotool load $(UF2) (Pico on this host)"
	@echo "  make serial         — USB CDC: type 1-5 to switch CRT patterns"
	@echo "  make serial-check   — wait for crt-drive pattern= banner"
	@echo "  make pico-discover  — lsusb / by-id on host, picotool info in container"
	@echo "  make hello-build    — USB bring-up firmware (hello_pico)"
	@echo "  make hello-flash    — picotool load $(HELLO_UF2)"
	@echo "  make hello-serial   — banner + ping/pong on USB CDC"
	@echo "  make hello-test     — build + flash + serial with a unique digest"
	@echo "  PICO_BOARD=$(PICO_BOARD)  CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)"
	@echo "See ../docs/toolchains.md"

print-host-platform:
	@echo $(HOST_PLATFORM)

build-images: build-images-host

build-images-host:
ifeq ($(PLATFORM_TAG),)
	@echo "=== docker compose build (host: $(HOST_PLATFORM); no amd64/arm64 override) ==="
	$(COMPOSE) build
else
	@echo "=== docker compose build (host: $(HOST_PLATFORM)) ==="
	$(COMPOSE_PLATFORM) build
endif

setup-binfmt:
	$(WITH_DOCKER) docker run --privileged --rm tonistiigi/binfmt --install all

smoke: build-images
	$(COMPOSE) run --rm --no-deps pico-dev bash -lc 'set -euo pipefail; arm-none-eabi-gcc --version; cmake --version; ninja --version; test -n "$$PICO_SDK_PATH"; test -f "$$PICO_SDK_PATH/pico_sdk_init.cmake"; echo "PICO_SDK_PATH=$$PICO_SDK_PATH"; picotool_out=$$(picotool version 2>&1); printf "%s\n" "$$picotool_out"; if printf "%s\n" "$$picotool_out" | grep -qi "without USB support"; then echo "picotool was built without libusb/USB support" >&2; exit 1; fi'

build: build-images
	$(COMPOSE) run --rm --no-deps pico-dev bash -lc 'set -euo pipefail; cmake -S /workspace -B $(BUILD_DIR) -GNinja -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DPICO_BOARD=$(PICO_BOARD); ninja -C $(BUILD_DIR)'

rebuild: build-images
	$(COMPOSE) run --rm --no-deps pico-dev bash -lc 'set -euo pipefail; rm -rf $(BUILD_DIR); cmake -S /workspace -B $(BUILD_DIR) -GNinja -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DPICO_BOARD=$(PICO_BOARD); ninja -C $(BUILD_DIR)'

shell:
	$(COMPOSE) run --rm --no-deps -it pico-dev bash

shell-usb:
	$(COMPOSE_USB) run --rm --no-deps -it pico-dev bash

flash:
	$(COMPOSE_USB) run --rm --no-deps pico-dev bash -lc 'set -euo pipefail; test -f $(UF2) || { echo "missing $(UF2) — run make build first" >&2; exit 1; }; bash /workspace/hello_pico/tools/flash.sh $(UF2)'

serial:
	$(COMPOSE_USB) run --rm --no-deps -it \
		$(if $(strip $(PICO_PORT)),-e PICO_PORT="$(PICO_PORT)",) \
		pico-dev bash -lc 'python3 /workspace/tools/crt_serial.py'

serial-check:
	$(COMPOSE_USB) run --rm --no-deps \
		$(if $(strip $(PICO_PORT)),-e PICO_PORT="$(PICO_PORT)",) \
		pico-dev bash -lc 'python3 /workspace/tools/crt_serial.py --check'

picotool-info:
	$(COMPOSE_USB) run --rm --no-deps pico-dev bash -lc 'picotool info -a || picotool info'

pico-discover:
	@bash $(REPO_ROOT)/hello_pico/tools/discover.sh
	@echo "=== picotool (container, USB) ==="
	@if lsusb -d 2e8a:0003 >/dev/null 2>&1; then \
		$(COMPOSE_USB) run --rm --no-deps pico-dev bash -lc 'picotool info -a'; \
	else \
		echo "skipping picotool info (board is not in BOOTSEL)."; \
	fi

hello-build: build-images
	$(COMPOSE) run --rm --no-deps $(if $(strip $(HELLO_IMAGE_ID)),-e HELLO_IMAGE_ID="$(HELLO_IMAGE_ID)",) pico-dev bash -lc 'set -euo pipefail; cmake -S /workspace/hello_pico -B $(HELLO_BUILD_DIR) -GNinja -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DPICO_BOARD=$(PICO_BOARD); ninja -C $(HELLO_BUILD_DIR)'

hello-flash:
	$(COMPOSE_USB) run --rm --no-deps pico-dev bash -lc 'set -euo pipefail; test -f $(HELLO_UF2) || { echo "missing $(HELLO_UF2) — run make hello-build first" >&2; exit 1; }; bash /workspace/hello_pico/tools/flash.sh $(HELLO_UF2)'

hello-serial:
	$(COMPOSE_USB) run --rm --no-deps $(if $(strip $(HELLO_IMAGE_ID)),-e HELLO_IMAGE_ID="$(HELLO_IMAGE_ID)",) \
		$(if $(strip $(PICO_PORT)),-e PICO_PORT="$(PICO_PORT)",) \
		pico-dev bash -lc 'python3 /workspace/hello_pico/tools/serial_ping.py'

# Unique digest so serial proves this run's UF2, not leftover firmware.
HELLO_TEST_ID ?= $(shell date +%s)-$(shell git -C $(REPO_ROOT) rev-parse --short=8 HEAD 2>/dev/null || echo unknown)
hello-test:
	$(MAKE) hello-build HELLO_IMAGE_ID="$(HELLO_TEST_ID)"
	$(MAKE) hello-flash
	$(MAKE) hello-serial HELLO_IMAGE_ID="$(HELLO_TEST_ID)"
