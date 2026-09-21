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
# CRT firmware app under apps/ (cross60 is the 60 Hz 80-col foundation).
APP ?= cross60
APP_SRC ?= /workspace/apps/$(APP)
APP_BUILD ?= /workspace/build/$(APP)
APP_TARGET := crt_$(APP)
UF2 ?= $(APP_BUILD)/$(APP_TARGET).uf2
HELLO_BUILD_DIR ?= /workspace/hello_pico/build
HELLO_UF2 ?= $(HELLO_BUILD_DIR)/hello_pico.uf2
IMAGE_ID ?=
CMAKE_BUILD_TYPE ?= Release

ifeq ($(APP),term)
  CMAKE_IMAGE_ID := $(if $(strip $(IMAGE_ID)),-DTERM_IMAGE_ID=$(IMAGE_ID),)
else ifeq ($(APP),demos)
  CMAKE_IMAGE_ID := $(if $(strip $(IMAGE_ID)),-DDEMO_IMAGE_ID=$(IMAGE_ID),)
else
  CMAKE_IMAGE_ID :=
endif

MONITOR_PY := python3 /workspace/tools/monitor.py
MONITOR_FLAGS :=
ifneq ($(strip $(APP)),)
  MONITOR_FLAGS += --app $(APP)
endif
ifneq ($(strip $(IMAGE_ID)),)
  MONITOR_FLAGS += --digest $(IMAGE_ID)
endif

SERIAL_GONE = use 'make monitor' (CDC) or 'make test' (HIL); serial targets are retired

.PHONY: help print-host-platform build-images build-images-host smoke build rebuild \
	shell shell-usb flash monitor test picotool-info setup-binfmt pico-discover \
	serial serial-check

help:
	@echo "crt-drive pico-dev (Dev-Host, no gateway)"
	@echo "  make build          — cmake/ninja apps/$(APP) → $(APP_TARGET) (default APP=cross60)"
	@echo "  make rebuild        — wipe build/$(APP) then build that app"
	@echo "  make flash          — picotool load $(UF2)"
	@echo "  make monitor        — USB CDC; omit APP to follow the running firmware banner"
	@echo "  make test           — unique digest + flash + HIL (APP=cross60 default; APP=hello for bring-up)"
	@echo "  make APP=pattern|term|demos build / flash / test"
	@echo "  make smoke / build-images / pico-discover / shell / shell-usb / picotool-info"
	@echo "  PICO_BOARD=$(PICO_BOARD)  APP=$(APP)  CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)"
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
ifeq ($(APP),hello)
	$(COMPOSE) run --rm --no-deps \
		$(if $(strip $(IMAGE_ID)),-e HELLO_IMAGE_ID="$(IMAGE_ID)",) \
		pico-dev bash -lc 'set -euo pipefail; cmake -S /workspace/hello_pico -B $(HELLO_BUILD_DIR) -GNinja -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DPICO_BOARD=$(PICO_BOARD); ninja -C $(HELLO_BUILD_DIR)'
else
	@test -d "$(REPO_ROOT)/apps/$(APP)" || { echo "unknown APP=$(APP) — add apps/$(APP)/CMakeLists.txt" >&2; exit 1; }
	$(COMPOSE) run --rm --no-deps \
		$(if $(strip $(IMAGE_ID)),-e IMAGE_ID="$(IMAGE_ID)",) \
		$(if $(filter term,$(APP)),-e TERM_IMAGE_ID="$(IMAGE_ID)",) \
		$(if $(filter demos,$(APP)),-e DEMO_IMAGE_ID="$(IMAGE_ID)",) \
		pico-dev bash -lc 'set -euo pipefail; cmake -S $(APP_SRC) -B $(APP_BUILD) -GNinja -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DPICO_BOARD=$(PICO_BOARD) $(CMAKE_IMAGE_ID); ninja -C $(APP_BUILD) $(APP_TARGET)'
endif

rebuild: build-images
ifeq ($(APP),hello)
	$(COMPOSE) run --rm --no-deps pico-dev bash -lc 'set -euo pipefail; rm -rf $(HELLO_BUILD_DIR); cmake -S /workspace/hello_pico -B $(HELLO_BUILD_DIR) -GNinja -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DPICO_BOARD=$(PICO_BOARD); ninja -C $(HELLO_BUILD_DIR)'
else
	@test -d "$(REPO_ROOT)/apps/$(APP)" || { echo "unknown APP=$(APP) — add apps/$(APP)/CMakeLists.txt" >&2; exit 1; }
	$(COMPOSE) run --rm --no-deps pico-dev bash -lc 'set -euo pipefail; rm -rf $(APP_BUILD); cmake -S $(APP_SRC) -B $(APP_BUILD) -GNinja -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DPICO_BOARD=$(PICO_BOARD); ninja -C $(APP_BUILD) $(APP_TARGET)'
endif

shell:
	$(COMPOSE) run --rm --no-deps -it pico-dev bash

shell-usb:
	$(COMPOSE_USB) run --rm --no-deps -it pico-dev bash

flash:
ifeq ($(APP),hello)
	$(COMPOSE_USB) run --rm --no-deps pico-dev bash -lc 'set -euo pipefail; test -f $(HELLO_UF2) || { echo "missing $(HELLO_UF2) — run make build APP=hello first" >&2; exit 1; }; bash /workspace/hello_pico/tools/flash.sh $(HELLO_UF2)'
else
	$(COMPOSE_USB) run --rm --no-deps pico-dev bash -lc 'set -euo pipefail; test -f $(UF2) || { echo "missing $(UF2) — run make build APP=$(APP) first" >&2; exit 1; }; bash /workspace/hello_pico/tools/flash.sh $(UF2)'
endif

monitor:
	$(COMPOSE_USB) run --rm --no-deps -it \
		$(if $(strip $(PICO_PORT)),-e PICO_PORT="$(PICO_PORT)",) \
		$(if $(strip $(IMAGE_ID)),-e IMAGE_ID="$(IMAGE_ID)",) \
		pico-dev bash -lc '$(MONITOR_PY) $(MONITOR_FLAGS)'

test:
ifeq ($(APP),hello)
	$(MAKE) build APP=hello IMAGE_ID="$(IMAGE_ID)"
	$(MAKE) flash APP=hello
	$(COMPOSE_USB) run --rm --no-deps \
		$(if $(strip $(PICO_PORT)),-e PICO_PORT="$(PICO_PORT)",) \
		-e IMAGE_ID="$(IMAGE_ID)" \
		pico-dev bash -lc '$(MONITOR_PY) --hil --app hello --digest "$(IMAGE_ID)"'
else
	$(MAKE) build APP=$(APP) IMAGE_ID="$(IMAGE_ID)"
	$(MAKE) flash APP=$(APP)
	$(COMPOSE_USB) run --rm --no-deps \
		$(if $(strip $(PICO_PORT)),-e PICO_PORT="$(PICO_PORT)",) \
		-e IMAGE_ID="$(IMAGE_ID)" \
		pico-dev bash -lc '$(MONITOR_PY) --hil --app $(APP) --digest "$(IMAGE_ID)"'
endif

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

serial serial-check:
	@echo "$(SERIAL_GONE)" >&2
	@exit 2
