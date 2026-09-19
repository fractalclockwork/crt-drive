# syntax=docker/dockerfile:1
# Dev-Host Pico SDK toolchain (template: fractalclockwork/cede lab/docker/pico-dev).
# Firmware is built from a bind-mounted repo at /workspace. Flash via USB on this host
# (no Raspberry Pi gateway).

ARG BASE_IMAGE=debian:bookworm-slim
FROM ${BASE_IMAGE}

ARG PICO_SDK_VERSION=2.1.1
ARG PICOTOOL_VERSION=2.1.1
LABEL org.opencontainers.image.title="crt-drive-pico-dev"
LABEL crt-drive.pico-sdk-version="${PICO_SDK_VERSION}"
LABEL crt-drive.picotool-version="${PICOTOOL_VERSION}"

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    python3 \
    python3-venv \
    git \
    ca-certificates \
    pkg-config \
    libusb-1.0-0 \
    libusb-1.0-0-dev \
    libudev-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 --branch "${PICO_SDK_VERSION}" --recursive \
    https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk

ENV PICO_SDK_PATH=/opt/pico-sdk

# Install picotool so the SDK's find_package(picotool) uses this copy (ELF→UF2)
# and USB commands (load/reboot) are compiled in via libusb.
RUN git clone --depth 1 --branch "${PICOTOOL_VERSION}" \
        https://github.com/raspberrypi/picotool.git /tmp/picotool \
    && cmake -S /tmp/picotool -B /tmp/picotool/build -GNinja \
        -DPICO_SDK_PATH=/opt/pico-sdk \
    && cmake --build /tmp/picotool/build \
    && cmake --install /tmp/picotool/build \
    && rm -rf /tmp/picotool

COPY 99-pico.rules /etc/udev/rules.d/99-pico.rules

# Compose runs as the Dev-Host uid (no passwd entry). Keep CMake/Python home writable.
ENV HOME=/tmp

WORKDIR /workspace
