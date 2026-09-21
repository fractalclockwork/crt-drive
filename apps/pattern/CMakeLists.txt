cmake_minimum_required(VERSION 3.13)

# Analog-setup patterns at all four Appendix B rates (USB m columns, r refresh).
#   cmake -S apps/pattern -B build/pattern
# Or from the repo root: make build APP=pattern

set(PICO_BOARD pico CACHE STRING "RP2040 board type")
get_filename_component(CRT_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${CRT_REPO_ROOT}/pico_sdk_import.cmake")

project(crt_pattern C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(crt_pattern
    main.c
    "${CRT_REPO_ROOT}/video/scanout.c"
)
pico_generate_pio_header(crt_pattern "${CRT_REPO_ROOT}/video/video_pixel.pio")
pico_generate_pio_header(crt_pattern "${CRT_REPO_ROOT}/video/hsync60.pio")
pico_generate_pio_header(crt_pattern "${CRT_REPO_ROOT}/video/hsync60_132.pio")
pico_generate_pio_header(crt_pattern "${CRT_REPO_ROOT}/video/vsync60.pio")
pico_generate_pio_header(crt_pattern "${CRT_REPO_ROOT}/video/vsync.pio")
target_include_directories(crt_pattern PRIVATE
    "${CRT_REPO_ROOT}"
    "${CRT_REPO_ROOT}/video"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(crt_pattern
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_clocks
    hardware_irq
    hardware_sync
)
pico_enable_stdio_usb(crt_pattern 1)
pico_enable_stdio_uart(crt_pattern 0)
pico_add_extra_outputs(crt_pattern)
