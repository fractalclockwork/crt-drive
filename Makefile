# crt-drive — Dev-Host Pico SDK container (no hardware gateway).
# Docker targets live in docker/Makefile; this file is a thin wrapper.

.PHONY: help image smoke build rebuild shell shell-usb flash serial serial-check picotool-info \
	pico-discover hello-build hello-flash hello-serial hello-test \
	term-build term-flash term-serial term-test \
	demos-build demos-flash demos-serial demos-test

APP ?= patterns

help:
	@echo "crt-drive RP2040 toolchain (Docker on this Dev-Host)"
	@echo "  make image         — build pico-dev image"
	@echo "  make smoke         — check ARM GCC, CMake, Ninja, Pico SDK, picotool USB"
	@echo "  make pico-discover — USB Pico on this host (lsusb, by-id, picotool)"
	@echo "  make hello-test    — build/flash/ping hello_pico (unique digest)"
	@echo "  make hello-build / hello-flash / hello-serial"
	@echo "  make build         — analog-setup patterns (apps/patterns, crt_drive)"
	@echo "  make flash / serial / serial-check"
	@echo "  make APP=term build / flash — glass TTY (apps/term, crt_term)"
	@echo "  make term-build / term-flash / term-serial / term-test"
	@echo "  make APP=demos build / flash — phosphor reel (apps/demos, crt_demos)"
	@echo "  make demos-build / demos-flash / demos-serial / demos-test"
	@echo "  make shell / shell-usb / picotool-info"
	@echo "See docs/toolchains.md (Docker image and how to use it)"

image:
	$(MAKE) -C docker build-images

smoke:
	$(MAKE) -C docker smoke

build:
	$(MAKE) -C docker build APP=$(APP)

rebuild:
	$(MAKE) -C docker rebuild APP=$(APP)

shell:
	$(MAKE) -C docker shell

shell-usb:
	$(MAKE) -C docker shell-usb

flash:
	$(MAKE) -C docker flash APP=$(APP)

picotool-info:
	$(MAKE) -C docker picotool-info

serial:
	$(MAKE) -C docker serial

serial-check:
	$(MAKE) -C docker serial-check

pico-discover:
	$(MAKE) -C docker pico-discover

hello-build:
	$(MAKE) -C docker hello-build

hello-flash:
	$(MAKE) -C docker hello-flash

hello-serial:
	$(MAKE) -C docker hello-serial

hello-test:
	$(MAKE) -C docker hello-test

term-build:
	$(MAKE) -C docker term-build

term-flash:
	$(MAKE) -C docker term-flash

term-serial:
	$(MAKE) -C docker term-serial

TERM_TEST_ID ?= $(shell date +%s)-$(shell git rev-parse --short=8 HEAD 2>/dev/null || echo unknown)
term-test:
	$(MAKE) pico-discover
	@if ! lsusb -d 2e8a:000a >/dev/null 2>&1 && ! lsusb -d 2e8a:0003 >/dev/null 2>&1; then \
		echo "HIL skip: no Pico on USB (2e8a:000a / 2e8a:0003)"; \
	else \
		$(MAKE) -C docker term-test TERM_TEST_ID="$(TERM_TEST_ID)"; \
	fi

demos-build:
	$(MAKE) -C docker demos-build

demos-flash:
	$(MAKE) -C docker demos-flash

demos-serial:
	$(MAKE) -C docker demos-serial

DEMO_TEST_ID ?= $(shell date +%s)-$(shell git rev-parse --short=8 HEAD 2>/dev/null || echo unknown)
demos-test:
	$(MAKE) pico-discover
	@if ! lsusb -d 2e8a:000a >/dev/null 2>&1 && ! lsusb -d 2e8a:0003 >/dev/null 2>&1; then \
		echo "HIL skip: no Pico on USB (2e8a:000a / 2e8a:0003)"; \
	else \
		$(MAKE) -C docker demos-test DEMO_TEST_ID="$(DEMO_TEST_ID)"; \
	fi
