add_library(crt_video video.c)

pico_generate_pio_header(crt_video ${CMAKE_CURRENT_LIST_DIR}/video_pixel.pio)
pico_generate_pio_header(crt_video ${CMAKE_CURRENT_LIST_DIR}/hsync.pio)
pico_generate_pio_header(crt_video ${CMAKE_CURRENT_LIST_DIR}/vsync.pio)

target_include_directories(crt_video PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(crt_video PUBLIC
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_clocks
    hardware_irq
)
