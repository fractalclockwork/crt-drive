#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"

#include "video_pixel.pio.h"
#include "hsync.pio.h"
#include "vsync.pio.h"

#define PIN_V0      0
#define PIN_V1      1
#define PIN_HSYNC   2
#define PIN_VSYNC   3

#define SM_PIXEL    0
#define SM_HSYNC    1
#define SM_VSYNC    2

#define CRT_SYS_CLK_KHZ 144000
#define PIO_CLKDIV      3.0f

#define FRAME_WIDTH     800
#define FRAME_HEIGHT    338
#define LINES_PER_FRAME 402
#define BYTES_PER_LINE  (FRAME_WIDTH / 4)   /* 200 */
#define LINE_STRIDE     204                 /* 200 + 4 trailing off pixels */
#define WORDS_PER_LINE  (LINE_STRIDE / 4)   /* 51 */

#define PIO_IRQ_REWIND  2

typedef enum {
    PIXEL_OFF    = 0b00,
    PIXEL_DIM    = 0b01,
    PIXEL_NORMAL = 0b10,
    PIXEL_BOLD   = 0b11
} PixelColor;

static uint8_t frame_buffer[FRAME_HEIGHT][LINE_STRIDE] __attribute__((aligned(4)));
static uint32_t blank_line[WORDS_PER_LINE] __attribute__((aligned(4)));
static const uint32_t *line_ptrs[LINES_PER_FRAME];
static uint32_t dma_rx_dummy;

static int data_chan;
static int drain_chan;
static int kick_chan;

void set_pixel(uint16_t x, uint16_t y, PixelColor color) {
    if (x >= FRAME_WIDTH || y >= FRAME_HEIGHT) {
        return;
    }

    uint16_t byte_idx = x / 4;
    uint8_t shift = (uint8_t)((3 - (x % 4)) * 2);

    frame_buffer[y][byte_idx] &= (uint8_t)~(0b11 << shift);
    frame_buffer[y][byte_idx] |= (uint8_t)((color & 0b11) << shift);
}

void clear_buffer(PixelColor color) {
    uint8_t packed_byte = (uint8_t)((color << 6) | (color << 4) | (color << 2) | color);
    memset(frame_buffer, packed_byte, sizeof(frame_buffer));
    if (color != PIXEL_OFF) {
        for (uint16_t y = 0; y < FRAME_HEIGHT; y++) {
            memset(&frame_buffer[y][BYTES_PER_LINE], 0, LINE_STRIDE - BYTES_PER_LINE);
        }
    }
}

void generate_crosshatch_pattern(void) {
    clear_buffer(PIXEL_OFF);

    for (uint16_t x = 80; x < FRAME_WIDTH - 1; x += 80) {
        for (uint16_t y = 0; y < FRAME_HEIGHT; y++) {
            set_pixel(x, y, PIXEL_NORMAL);
        }
    }
    for (uint16_t y = 13; y < FRAME_HEIGHT - 1; y += 13) {
        for (uint16_t x = 0; x < FRAME_WIDTH; x++) {
            set_pixel(x, y, PIXEL_NORMAL);
        }
    }

    for (uint16_t x = 0; x < FRAME_WIDTH; x++) {
        set_pixel(x, 0, PIXEL_BOLD);
        set_pixel(x, FRAME_HEIGHT - 1, PIXEL_BOLD);
    }
    for (uint16_t y = 0; y < FRAME_HEIGHT; y++) {
        set_pixel(0, y, PIXEL_BOLD);
        set_pixel(FRAME_WIDTH - 1, y, PIXEL_BOLD);
    }

    const uint16_t cx = FRAME_WIDTH / 2;   /* 400 */
    const uint16_t cy = FRAME_HEIGHT / 2;  /* 169 */
    for (uint16_t y = 0; y < FRAME_HEIGHT; y++) {
        set_pixel(cx - 1, y, PIXEL_BOLD);
        set_pixel(cx,     y, PIXEL_BOLD);
        set_pixel(cx + 1, y, PIXEL_BOLD);
    }
    for (uint16_t x = 0; x < FRAME_WIDTH; x++) {
        set_pixel(x, cy - 1, PIXEL_BOLD);
        set_pixel(x, cy,     PIXEL_BOLD);
        set_pixel(x, cy + 1, PIXEL_BOLD);
    }
}

static void pio_rewind_irq(void) {
    if (pio_interrupt_get(pio0, PIO_IRQ_REWIND)) {
        pio_interrupt_clear(pio0, PIO_IRQ_REWIND);
        dma_channel_set_read_addr(kick_chan, line_ptrs, false);
    }
}

static void init_line_table(void) {
    memset(blank_line, 0, sizeof(blank_line));
    for (int y = 0; y < FRAME_HEIGHT; y++) {
        line_ptrs[y] = (const uint32_t *)frame_buffer[y];
    }
    for (int y = FRAME_HEIGHT; y < LINES_PER_FRAME; y++) {
        line_ptrs[y] = blank_line;
    }
}

static void init_crt_pio(PIO pio) {
    uint offset_pixel = pio_add_program(pio, &video_pixel_program);
    uint offset_hsync = pio_add_program(pio, &hsync_program);
    uint offset_vsync = pio_add_program(pio, &vsync_program);

    pio_gpio_init(pio, PIN_V0);
    pio_gpio_init(pio, PIN_V1);
    pio_gpio_init(pio, PIN_HSYNC);
    pio_gpio_init(pio, PIN_VSYNC);

    pio_sm_config c_pixel = video_pixel_program_get_default_config(offset_pixel);
    sm_config_set_out_pins(&c_pixel, PIN_V0, 2);
    sm_config_set_out_shift(&c_pixel, false, true, 32);
    sm_config_set_fifo_join(&c_pixel, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c_pixel, PIO_CLKDIV);
    pio_sm_set_consecutive_pindirs(pio, SM_PIXEL, PIN_V0, 2, true);
    pio_sm_init(pio, SM_PIXEL, offset_pixel, &c_pixel);
    pio_sm_set_pins_with_mask(pio, SM_PIXEL, 0, (1u << PIN_V0) | (1u << PIN_V1));

    pio_sm_config c_hsync = hsync_program_get_default_config(offset_hsync);
    sm_config_set_set_pins(&c_hsync, PIN_HSYNC, 1);
    sm_config_set_clkdiv(&c_hsync, PIO_CLKDIV);
    pio_sm_set_consecutive_pindirs(pio, SM_HSYNC, PIN_HSYNC, 1, true);
    pio_sm_init(pio, SM_HSYNC, offset_hsync, &c_hsync);
    pio_sm_exec(pio, SM_HSYNC, pio_encode_set(pio_pins, 1));

    pio_sm_config c_vsync = vsync_program_get_default_config(offset_vsync);
    sm_config_set_set_pins(&c_vsync, PIN_VSYNC, 1);
    sm_config_set_clkdiv(&c_vsync, PIO_CLKDIV);
    pio_sm_set_consecutive_pindirs(pio, SM_VSYNC, PIN_VSYNC, 1, true);
    pio_sm_init(pio, SM_VSYNC, offset_vsync, &c_vsync);
    pio_sm_exec(pio, SM_VSYNC, pio_encode_set(pio_pins, 1));

    pio_set_irq0_source_enabled(pio, pis_interrupt2, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio_rewind_irq);
    irq_set_priority(PIO0_IRQ_0, 0);
    irq_set_enabled(PIO0_IRQ_0, true);
}

static void setup_framebuffer_dma(PIO pio) {
    data_chan = dma_claim_unused_channel(true);
    drain_chan = dma_claim_unused_channel(true);
    kick_chan = dma_claim_unused_channel(true);

    dma_channel_config c_data = dma_channel_get_default_config(data_chan);
    channel_config_set_transfer_data_size(&c_data, DMA_SIZE_32);
    channel_config_set_read_increment(&c_data, true);
    channel_config_set_write_increment(&c_data, false);
    channel_config_set_dreq(&c_data, pio_get_dreq(pio, SM_PIXEL, true));
    dma_channel_configure(
        data_chan,
        &c_data,
        &pio->txf[SM_PIXEL],
        frame_buffer,
        WORDS_PER_LINE,
        false
    );

    dma_channel_config c_drain = dma_channel_get_default_config(drain_chan);
    channel_config_set_transfer_data_size(&c_drain, DMA_SIZE_32);
    channel_config_set_read_increment(&c_drain, false);
    channel_config_set_write_increment(&c_drain, false);
    channel_config_set_dreq(&c_drain, pio_get_dreq(pio, SM_HSYNC, false));
    channel_config_set_chain_to(&c_drain, (uint)kick_chan);
    dma_channel_configure(
        drain_chan,
        &c_drain,
        &dma_rx_dummy,
        &pio->rxf[SM_HSYNC],
        1,
        false
    );

    dma_channel_config c_kick = dma_channel_get_default_config(kick_chan);
    channel_config_set_transfer_data_size(&c_kick, DMA_SIZE_32);
    channel_config_set_read_increment(&c_kick, true);
    channel_config_set_write_increment(&c_kick, false);
    channel_config_set_chain_to(&c_kick, (uint)drain_chan);
    dma_channel_configure(
        kick_chan,
        &c_kick,
        &dma_hw->ch[data_chan].al3_read_addr_trig,
        line_ptrs,
        1,
        false
    );

    dma_channel_start(drain_chan);
}

int main(void) {
    set_sys_clock_khz(CRT_SYS_CLK_KHZ, true);
    stdio_init_all();

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    generate_crosshatch_pattern();
    init_line_table();
    init_crt_pio(pio0);
    setup_framebuffer_dma(pio0);
    pio_enable_sm_mask_in_sync(pio0, (1u << SM_PIXEL) | (1u << SM_HSYNC) | (1u << SM_VSYNC));

    while (true) {
#ifdef PICO_DEFAULT_LED_PIN
        gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
#endif
        printf("crt-drive crosshatch 78Hz\n");
        sleep_ms(250);
    }
}
