# crt-drive — Dev-Host Pico SDK container (no hardware gateway).
# Docker targets live in docker/Makefile; this file is a thin wrapper.

.PHONY: help image smoke build rebuild shell shell-usb flash serial serial-check picotool-info \
	pico-discover hello-build hello-flash hello-serial hello-test

help:
	@echo "crt-drive RP2040 toolchain (Docker on this Dev-Host)"
	@echo "  make image         — build pico-dev image"
	@echo "  make smoke         — check ARM GCC, CMake, Ninja, Pico SDK, picotool USB"
	@echo "  make pico-discover — USB Pico on this host (lsusb, by-id, picotool)"
	@echo "  make hello-test    — build/flash/ping hello_pico (unique digest)"
	@echo "  make hello-build / hello-flash / hello-serial"
	@echo "  make build         — CRT firmware (patterns, main.c / .pio)"
	@echo "  make shell / shell-usb / flash / serial / serial-check / picotool-info"
	@echo "See docs/toolchains.md (Docker image and how to use it)"

image:
	$(MAKE) -C docker build-images

smoke:
	$(MAKE) -C docker smoke

build:
	$(MAKE) -C docker build

rebuild:
	$(MAKE) -C docker rebuild

shell:
	$(MAKE) -C docker shell

shell-usb:
	$(MAKE) -C docker shell-usb

flash:
	$(MAKE) -C docker flash

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
