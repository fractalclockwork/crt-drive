cmake_minimum_required(VERSION 3.13)

# Include the standard Raspberry Pi Pico SDK setup
include(pico_sdk_import.cmake)

project(crt_drive C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

# Define the target executable and source files
add_executable(crt_drive
    main.c
)

# Generate C header files (.h) from PIO source files (.pio)
pico_generate_pio_header(crt_drive ${CMAKE_CURRENT_LIST_DIR}/video_pixel.pio)
pico_generate_pio_header(crt_drive ${CMAKE_CURRENT_LIST_DIR}/hsync.pio)
pico_generate_pio_header(crt_drive ${CMAKE_CURRENT_LIST_DIR}/vsync.pio)

# Include directories so generated headers are found
target_include_directories(crt_drive PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Link required RP2040 hardware drivers
target_link_libraries(crt_drive
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_clocks
)

# Enable USB CDC terminal output for serial debugging
pico_enable_stdio_usb(crt_drive 1)
pico_enable_stdio_uart(crt_drive 0)

# Generate UF2, HEX, and BIN build outputs
pico_add_extra_outputs(crt_drive)
