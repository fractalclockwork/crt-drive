cmake_minimum_required(VERSION 3.13)

# 60/78 Hz 80/132-col measure (USB m columns, r refresh). Configure this directory only:
#   cmake -S apps/cross60 -B build/cross60
# Or from the repo root: make build APP=cross60

set(PICO_BOARD pico CACHE STRING "RP2040 board type")
get_filename_component(CRT_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${CRT_REPO_ROOT}/pico_sdk_import.cmake")

project(crt_cross60 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(crt_cross60 main.c)
pico_generate_pio_header(crt_cross60 "${CRT_REPO_ROOT}/video/video_pixel.pio")
pico_generate_pio_header(crt_cross60 "${CMAKE_CURRENT_LIST_DIR}/hsync60.pio")
pico_generate_pio_header(crt_cross60 "${CMAKE_CURRENT_LIST_DIR}/hsync60_132.pio")
pico_generate_pio_header(crt_cross60 "${CMAKE_CURRENT_LIST_DIR}/vsync60.pio")
pico_generate_pio_header(crt_cross60 "${CRT_REPO_ROOT}/video/vsync.pio")
target_include_directories(crt_cross60 PRIVATE
    "${CRT_REPO_ROOT}/video"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(crt_cross60
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_clocks
    hardware_irq
)
pico_enable_stdio_usb(crt_cross60 1)
pico_enable_stdio_uart(crt_cross60 0)
pico_add_extra_outputs(crt_cross60)
