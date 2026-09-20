cmake_minimum_required(VERSION 3.13)

# Default RP2040 Pico; override with -DPICO_BOARD=pico_w (docker/Makefile PICO_BOARD).
set(PICO_BOARD pico CACHE STRING "RP2040 board type")

# Include the standard Raspberry Pi Pico SDK setup (PICO_SDK_PATH from pico-dev).
include(pico_sdk_import.cmake)

project(crt_drive C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

# Shared 78 Hz 80-col scanout (PIO + DMA + packing).
add_library(crt_video video/video.c)
pico_generate_pio_header(crt_video ${CMAKE_CURRENT_LIST_DIR}/video/video_pixel.pio)
pico_generate_pio_header(crt_video ${CMAKE_CURRENT_LIST_DIR}/video/hsync.pio)
pico_generate_pio_header(crt_video ${CMAKE_CURRENT_LIST_DIR}/video/vsync.pio)
target_include_directories(crt_video PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/video
)
target_link_libraries(crt_video PUBLIC
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_clocks
)

# Analog-setup patterns (service tool).
add_executable(crt_drive
    pattern/main.c
)
target_link_libraries(crt_drive crt_video)
pico_enable_stdio_usb(crt_drive 1)
pico_enable_stdio_uart(crt_drive 0)
pico_add_extra_outputs(crt_drive)
