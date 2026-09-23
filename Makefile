# crt-drive — Dev-Host Pico SDK container (no hardware gateway).
# Docker targets live in docker/Makefile; this file is a thin wrapper.

.PHONY: help image smoke build rebuild shell shell-usb flash monitor test picotool-info \
	pico-discover \
	pattern-build pattern-flash pattern-monitor pattern-test \
	term-build term-flash term-monitor term-test \
	demos-build demos-flash demos-monitor demos-test \
	cross60-build cross60-flash cross60-monitor cross60-test \
	hello-build hello-flash hello-monitor hello-test \
	camera camera-check \
	serial serial-check term-serial demos-serial hello-serial

APP ?= cross60

SERIAL_GONE = use 'make monitor' (CDC) or 'make test' (HIL); serial targets are retired

help:
	@echo "crt-drive RP2040 toolchain (Docker on this Dev-Host)"
	@echo "  Verbs (default APP=cross60 → apps/cross60, crt_cross60):"
	@echo "    make build / rebuild / flash / test"
	@echo "    make monitor          — USB CDC; banner-detects cross60, pattern, term, demos, hello"
	@echo "  Other apps: make flash APP=pattern | APP=term | APP=demos   make test APP=hello"
	@echo "  Aliases: pattern-*  term-*  demos-*  cross60-*  hello-*   (build, flash, monitor, test)"
	@echo "  make camera           — live C310 view and framing-pattern overlay"
	@echo "  make camera-check    — same view; exit 0 only when the raster is fully framed"
	@echo "  make image / smoke / pico-discover / shell / shell-usb / picotool-info"
	@echo "See docs/toolchains.md"

image:
	$(MAKE) -C docker build-images

smoke:
	$(MAKE) -C docker smoke

build:
	$(MAKE) -C docker build APP=$(APP) IMAGE_ID="$(IMAGE_ID)"

rebuild:
	$(MAKE) -C docker rebuild APP=$(APP)

shell:
	$(MAKE) -C docker shell

shell-usb:
	$(MAKE) -C docker shell-usb

flash:
	$(MAKE) -C docker flash APP=$(APP)

# Bare monitor does not pass APP, so the host script reads the running firmware banner.
# Command-line APP=term (etc.) forces that dialect.
ifeq ($(origin APP),command line)
MONITOR_APP := $(APP)
else
MONITOR_APP :=
endif

monitor:
	$(MAKE) -C docker monitor APP="$(MONITOR_APP)" IMAGE_ID="$(IMAGE_ID)"

picotool-info:
	$(MAKE) -C docker picotool-info

pico-discover:
	$(MAKE) -C docker pico-discover

IMAGE_ID ?=
TEST_ID ?= $(shell date +%s)-$(shell git rev-parse --short=8 HEAD 2>/dev/null || echo unknown)

test:
	$(MAKE) pico-discover
	@if ! lsusb -d 2e8a:000a >/dev/null 2>&1 && ! lsusb -d 2e8a:0003 >/dev/null 2>&1; then \
		echo "HIL skip: no Pico on USB (2e8a:000a / 2e8a:0003)"; \
	else \
		$(MAKE) -C docker test APP=$(APP) IMAGE_ID="$(TEST_ID)"; \
	fi

pattern-build:
	$(MAKE) build APP=pattern

pattern-flash:
	$(MAKE) flash APP=pattern

pattern-monitor:
	$(MAKE) monitor APP=pattern

pattern-test:
	$(MAKE) test APP=pattern

term-build:
	$(MAKE) build APP=term

term-flash:
	$(MAKE) flash APP=term

term-monitor:
	$(MAKE) monitor APP=term

term-test:
	$(MAKE) test APP=term

demos-build:
	$(MAKE) build APP=demos

demos-flash:
	$(MAKE) flash APP=demos

demos-monitor:
	$(MAKE) monitor APP=demos

demos-test:
	$(MAKE) test APP=demos

cross60-build:
	$(MAKE) build APP=cross60

cross60-flash:
	$(MAKE) flash APP=cross60

cross60-monitor:
	$(MAKE) monitor APP=cross60

cross60-test:
	$(MAKE) test APP=cross60

hello-build:
	$(MAKE) build APP=hello IMAGE_ID="$(IMAGE_ID)"

hello-flash:
	$(MAKE) flash APP=hello

hello-monitor:
	$(MAKE) monitor APP=hello

hello-test:
	$(MAKE) test APP=hello

# Host-side. The C310 and the X11 view are not inside the Pico container.
camera:
	python3 tools/camera.py

camera-check:
	python3 tools/camera.py --check

serial serial-check term-serial demos-serial hello-serial:
	@echo "$(SERIAL_GONE)" >&2
	@exit 2
