cmake_minimum_required(VERSION 3.13)

# Analog-setup pattern app (default CRT firmware). Configure this directory only:
#   cmake -S apps/pattern -B build/pattern
# Or from the repo root: make build

set(PICO_BOARD pico CACHE STRING "RP2040 board type")
get_filename_component(CRT_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${CRT_REPO_ROOT}/pico_sdk_import.cmake")

project(crt_pattern C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory("${CRT_REPO_ROOT}/video" "${CMAKE_BINARY_DIR}/crt_video")

add_executable(crt_pattern main.c)
target_include_directories(crt_pattern PRIVATE "${CRT_REPO_ROOT}")
target_link_libraries(crt_pattern crt_video)
pico_enable_stdio_usb(crt_pattern 1)
pico_enable_stdio_uart(crt_pattern 0)
pico_add_extra_outputs(crt_pattern)
