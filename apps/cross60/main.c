/* Four-mode measure (60/78 Hz × 80/132-col). HIL-settled on this MC5;
 * analog pots stay with 60 Hz. Next: fold timings into apps/pattern.
 * Record: docs/test-pattern-design.md (standalone measure).
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"

#include "video_pixel.pio.h"
#include "hsync60.pio.h"
#include "hsync60_132.pio.h"
#include "vsync60.pio.h"
#include "vsync.pio.h"

#define PIN_V0      0
#define PIN_V1      1
#define PIN_HSYNC   2
#define PIN_VSYNC   3
#define PIN_VSIZE78 4                   /* high in 78 Hz: U6 pin 5 / VR301 select */

#define SM_PIXEL    0
#define SM_HSYNC    1
#define SM_VSYNC    2

#define CRT_SYS_CLK_KHZ 128400
#define PIO_CLKDIV_80   4.0f
#define PIO_CLKDIV_132  (128400.0f / 48000.0f)  /* 2.675 → ~47.988 MHz */
#define PIO_IRQ_REWIND  2

#define WIDTH_80        800
#define WIDTH_132       1188
#define HEIGHT_60       416                 /* 26 × 16 */
#define HEIGHT_78       377                 /* 13 × 29; 390 left retrace unblanked */
#define FB_HEIGHT       HEIGHT_60
#define V_SYNC_LINES    6
#define V_BLANK_60      107                 /* 523 - 416 */
#define V_BLANK_78      25                  /* 402 - 377; 6 sync + 19 porch */
#define LINES_MAX       (HEIGHT_60 + V_BLANK_60)        /* 523 */
#define CELL_W_80       40                  /* 20 boxes; 4 × 10-dot cells */
#define CELL_W_132      54                  /* 22 boxes; 6 × 9-dot cells */
#define CELL_H_60       26                  /* 16 rows */
#define CELL_H_78       29                  /* 13 rows; 13 × 29 = 377 */
#define H_PAD_WORDS     5                   /* 80 px */
#define H_PAD_BYTES     (H_PAD_WORDS * 4)
#define PIXEL_STORE     300                 /* 1188/4 = 297, 32-bit padded */
#define TAIL_BYTES      4
#define STORE_STRIDE    (H_PAD_BYTES + PIXEL_STORE + TAIL_BYTES)
#define DMA_BYTES_80    204                 /* 200 px + 4 off */
#define DMA_BYTES_132   304                 /* 300 store + 4 off */
#define H_DELAY_MAX     H_PAD_WORDS
#define V_BP_DEFAULT_60 51
#define V_BP_DEFAULT_78 8               /* 12 was 1.8 cm top / 1.5 cm bottom */
#define V_BP_MIN        1

#define BANNER_MS       250
#define KEY_POLL_US     10000

typedef enum {
    PIXEL_OFF    = 0b00,
    PIXEL_NORMAL = 0b10,
    PIXEL_BOLD   = 0b11
} PixelColor;

typedef enum {
    PAT_PLUS = 0,
    PAT_BOX,
    PAT_GRID,
    PAT_MEAS,
    PAT_COUNT
} PatternId;

typedef enum {
    MODE_60_80 = 0,
    MODE_60_132,
    MODE_78_80,
    MODE_78_132
} VideoMode;

static uint8_t frame_buffer[FB_HEIGHT][STORE_STRIDE] __attribute__((aligned(4)));
static uint32_t blank_line[STORE_STRIDE / 4] __attribute__((aligned(4)));
static const uint32_t *line_ptrs[LINES_MAX];
static uint32_t dma_rx_dummy;
static int data_chan;
static int drain_chan;
static int kick_chan;

static PIO pio_crt;
static uint offset_pixel;
static uint offset_hsync;
static uint offset_vsync;
static const pio_program_t *hsync_prog;
static const pio_program_t *vsync_prog;

static volatile uint8_t h_delay_words;
static volatile uint8_t v_back_porch = V_BP_DEFAULT_60;
static volatile bool timing_dirty;
static PatternId current_pat = PAT_MEAS;
static const char *pat_name = "meas";
static VideoMode video_mode = MODE_60_80;

static bool mode_132(void) {
    return video_mode == MODE_60_132 || video_mode == MODE_78_132;
}

static bool mode_78(void) {
    return video_mode == MODE_78_80 || video_mode == MODE_78_132;
}

static uint16_t mode_width(void) {
    return mode_132() ? WIDTH_132 : WIDTH_80;
}

static uint16_t mode_height(void) {
    return mode_78() ? HEIGHT_78 : HEIGHT_60;
}

static uint16_t mode_cell_w(void) {
    return mode_132() ? CELL_W_132 : CELL_W_80;
}

static uint16_t mode_cell_h(void) {
    return mode_78() ? CELL_H_78 : CELL_H_60;
}

static uint8_t mode_v_blank(void) {
    return mode_78() ? V_BLANK_78 : V_BLANK_60;
}

static uint8_t mode_v_bp_default(void) {
    return mode_78() ? V_BP_DEFAULT_78 : V_BP_DEFAULT_60;
}

static uint8_t mode_v_bp_max(void) {
    return (uint8_t)(mode_v_blank() - V_SYNC_LINES);
}

static uint mode_dma_bytes(void) {
    return mode_132() ? DMA_BYTES_132 : DMA_BYTES_80;
}

static float mode_clkdiv(void) {
    return mode_132() ? PIO_CLKDIV_132 : PIO_CLKDIV_80;
}

static const char *mode_hz_name(void) {
    return mode_78() ? "78Hz" : "60Hz";
}

static const char *mode_col_name(void) {
    return mode_132() ? "132col" : "80col";
}

static VideoMode toggle_cols(VideoMode m) {
    switch (m) {
    case MODE_60_80:
        return MODE_60_132;
    case MODE_60_132:
        return MODE_60_80;
    case MODE_78_80:
        return MODE_78_132;
    default:
        return MODE_78_80;
    }
}

static VideoMode toggle_hz(VideoMode m) {
    switch (m) {
    case MODE_60_80:
        return MODE_78_80;
    case MODE_78_80:
        return MODE_60_80;
    case MODE_60_132:
        return MODE_78_132;
    default:
        return MODE_60_132;
    }
}

static uint dma_words(uint8_t delay_words) {
    return delay_words + mode_dma_bytes() / 4;
}

static uint8_t *pixel_base(uint16_t y) {
    return &frame_buffer[y][H_PAD_BYTES];
}

static void set_pixel(uint16_t x, uint16_t y, PixelColor color) {
    if (x >= mode_width() || y >= mode_height()) {
        return;
    }

    uint16_t byte_idx = (uint16_t)((x / 4) ^ 3); /* 32-bit LE DMA; PIO shift left */
    uint8_t shift = (uint8_t)((3 - (x % 4)) * 2);
    uint8_t *row = pixel_base(y);

    row[byte_idx] &= (uint8_t)~(0b11 << shift);
    row[byte_idx] |= (uint8_t)((color & 0b11) << shift);
}

static void clear_pixels(void) {
    for (uint16_t y = 0; y < FB_HEIGHT; y++) {
        memset(pixel_base(y), 0, PIXEL_STORE + TAIL_BYTES);
    }
}

static void draw_hline(uint16_t y, PixelColor color) {
    uint16_t w = mode_width();
    for (uint16_t x = 0; x < w; x++) {
        set_pixel(x, y, color);
    }
}

static void draw_vline(uint16_t x, PixelColor color) {
    uint16_t h = mode_height();
    for (uint16_t y = 0; y < h; y++) {
        set_pixel(x, y, color);
    }
}

static void draw_box(PixelColor color) {
    draw_hline(0, color);
    draw_hline((uint16_t)(mode_height() - 1), color);
    draw_vline(0, color);
    draw_vline((uint16_t)(mode_width() - 1), color);
}

static void draw_grid(PixelColor color) {
    uint16_t w = mode_width();
    uint16_t h = mode_height();
    uint16_t cw = mode_cell_w();
    uint16_t ch = mode_cell_h();
    for (uint16_t x = cw; x < w; x += cw) {
        draw_vline(x, color);
    }
    for (uint16_t y = ch; y < h; y += ch) {
        draw_hline(y, color);
    }
}

static void draw_plus(PixelColor color) {
    draw_vline((uint16_t)(mode_width() / 2), color);
    draw_hline((uint16_t)(mode_height() / 2), color);
}

static void draw_pattern(PatternId id) {
    current_pat = id;
    clear_pixels();
    switch (id) {
    case PAT_PLUS:
        draw_plus(PIXEL_BOLD);
        pat_name = "plus";
        break;
    case PAT_BOX:
        draw_box(PIXEL_BOLD);
        pat_name = "box";
        break;
    case PAT_GRID:
        draw_grid(PIXEL_NORMAL);
        draw_plus(PIXEL_BOLD);
        pat_name = "grid";
        break;
    case PAT_MEAS:
    default:
        current_pat = PAT_MEAS;
        draw_grid(PIXEL_NORMAL);
        draw_box(PIXEL_BOLD);
        draw_plus(PIXEL_BOLD);
        pat_name = "meas";
        break;
    }
}

static void rebuild_line_table(uint8_t delay_words, uint8_t v_bp) {
    uint16_t h = mode_height();
    uint8_t v_fp = (uint8_t)(mode_v_blank() - V_SYNC_LINES - v_bp);
    uint byte_off = H_PAD_BYTES - (uint)delay_words * 4u;

    for (int n = 0; n < v_bp; n++) {
        line_ptrs[n] = (const uint32_t *)((const uint8_t *)blank_line + byte_off);
    }
    for (int y = 0; y < h; y++) {
        line_ptrs[v_bp + y] = (const uint32_t *)(frame_buffer[y] + byte_off);
    }
    const int tail = v_bp + h;
    for (int n = 0; n < v_fp + V_SYNC_LINES; n++) {
        line_ptrs[tail + n] = (const uint32_t *)((const uint8_t *)blank_line + byte_off);
    }
    const int used = tail + v_fp + V_SYNC_LINES;
    for (int n = used; n < LINES_MAX; n++) {
        line_ptrs[n] = (const uint32_t *)((const uint8_t *)blank_line + byte_off);
    }
}

static void apply_timing(uint8_t delay_words, uint8_t v_bp) {
    rebuild_line_table(delay_words, v_bp);
    dma_channel_set_trans_count(data_chan, dma_words(delay_words), false);
}

static void pio_rewind_irq(void) {
    if (pio_interrupt_get(pio_crt, PIO_IRQ_REWIND)) {
        pio_interrupt_clear(pio_crt, PIO_IRQ_REWIND);
        if (timing_dirty) {
            apply_timing(h_delay_words, v_back_porch);
            timing_dirty = false;
        }
        dma_channel_set_read_addr(kick_chan, line_ptrs, false);
    }
}

static void request_timing(uint8_t delay_words, uint8_t v_bp) {
    if (delay_words > H_DELAY_MAX) {
        delay_words = H_DELAY_MAX;
    }
    if (v_bp < V_BP_MIN) {
        v_bp = V_BP_MIN;
    }
    if (v_bp > mode_v_bp_max()) {
        v_bp = mode_v_bp_max();
    }
    h_delay_words = delay_words;
    v_back_porch = v_bp;
    timing_dirty = true;
}

static void print_status(void) {
    uint8_t v_fp = (uint8_t)(mode_v_blank() - V_SYNC_LINES - v_back_porch);
    printf("crt-cross60 %s %s %s hpad=%u vbp=%u vfp=%u vsize78=%u\n",
           mode_hz_name(), mode_col_name(), pat_name,
           (unsigned)h_delay_words * 16u, (unsigned)v_back_porch, (unsigned)v_fp,
           mode_78() ? 1u : 0u);
}

static void print_help(void) {
    printf("crt-cross60: 1 plus  2 box  3 grid  4 meas  m 80/132  r 60/78\n");
    printf("  a/d H 16px left/right  A/D 64px  w/s V 1 line up/down  W/S 5 lines  0 reset  ? help\n");
    printf("  r also drives GP4 high (78 Hz V-size select, U6 pin 5 / VR301)\n");
    print_status();
}

static void load_timing_programs(void) {
    if (hsync_prog != NULL) {
        pio_remove_program(pio_crt, hsync_prog, offset_hsync);
    }
    if (vsync_prog != NULL) {
        pio_remove_program(pio_crt, vsync_prog, offset_vsync);
    }
    hsync_prog = mode_132() ? &hsync60_132_program : &hsync60_program;
    vsync_prog = mode_78() ? &vsync_program : &vsync60_program;
    offset_hsync = pio_add_program(pio_crt, hsync_prog);
    offset_vsync = pio_add_program(pio_crt, vsync_prog);
}

static void configure_pio_sms(void) {
    PIO pio = pio_crt;
    float div = mode_clkdiv();

    pio_sm_config c_pixel = video_pixel_program_get_default_config(offset_pixel);
    sm_config_set_out_pins(&c_pixel, PIN_V0, 2);
    sm_config_set_out_shift(&c_pixel, false, true, 32);
    sm_config_set_fifo_join(&c_pixel, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c_pixel, div);
    pio_sm_set_consecutive_pindirs(pio, SM_PIXEL, PIN_V0, 2, true);
    pio_sm_init(pio, SM_PIXEL, offset_pixel, &c_pixel);
    pio_sm_set_pins_with_mask(pio, SM_PIXEL, 0, (1u << PIN_V0) | (1u << PIN_V1));

    pio_sm_config c_hsync;
    if (mode_132()) {
        c_hsync = hsync60_132_program_get_default_config(offset_hsync);
    } else {
        c_hsync = hsync60_program_get_default_config(offset_hsync);
    }
    sm_config_set_set_pins(&c_hsync, PIN_HSYNC, 1);
    sm_config_set_clkdiv(&c_hsync, div);
    pio_sm_set_consecutive_pindirs(pio, SM_HSYNC, PIN_HSYNC, 1, true);
    pio_sm_init(pio, SM_HSYNC, offset_hsync, &c_hsync);
    pio_sm_exec(pio, SM_HSYNC, pio_encode_set(pio_pins, 1));

    pio_sm_config c_vsync;
    if (mode_78()) {
        c_vsync = vsync_program_get_default_config(offset_vsync);
    } else {
        c_vsync = vsync60_program_get_default_config(offset_vsync);
    }
    sm_config_set_set_pins(&c_vsync, PIN_VSYNC, 1);
    sm_config_set_clkdiv(&c_vsync, div);
    pio_sm_set_consecutive_pindirs(pio, SM_VSYNC, PIN_VSYNC, 1, true);
    pio_sm_init(pio, SM_VSYNC, offset_vsync, &c_vsync);
    pio_sm_exec(pio, SM_VSYNC, pio_encode_set(pio_pins, 1));

    pio_sm_clear_fifos(pio, SM_PIXEL);
    pio_sm_clear_fifos(pio, SM_HSYNC);
    pio_sm_clear_fifos(pio, SM_VSYNC);
}

static void configure_dma(void) {
    dma_channel_config c_data = dma_channel_get_default_config(data_chan);
    channel_config_set_transfer_data_size(&c_data, DMA_SIZE_32);
    channel_config_set_read_increment(&c_data, true);
    channel_config_set_write_increment(&c_data, false);
    channel_config_set_dreq(&c_data, pio_get_dreq(pio_crt, SM_PIXEL, true));
    dma_channel_configure(
        data_chan,
        &c_data,
        &pio_crt->txf[SM_PIXEL],
        frame_buffer[0] + H_PAD_BYTES,
        dma_words(h_delay_words),
        false
    );

    dma_channel_config c_drain = dma_channel_get_default_config(drain_chan);
    channel_config_set_transfer_data_size(&c_drain, DMA_SIZE_32);
    channel_config_set_read_increment(&c_drain, false);
    channel_config_set_write_increment(&c_drain, false);
    channel_config_set_dreq(&c_drain, pio_get_dreq(pio_crt, SM_HSYNC, false));
    channel_config_set_chain_to(&c_drain, (uint)kick_chan);
    dma_channel_configure(
        drain_chan,
        &c_drain,
        &dma_rx_dummy,
        &pio_crt->rxf[SM_HSYNC],
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

static void abort_dma(void) {
    dma_channel_abort(kick_chan);
    dma_channel_abort(drain_chan);
    dma_channel_abort(data_chan);
    dma_hw->abort = (1u << (uint)kick_chan) | (1u << (uint)drain_chan) | (1u << (uint)data_chan);
    while (dma_hw->abort) {
        tight_loop_contents();
    }
}

static void scanout_enable(bool on) {
    uint32_t mask = (1u << SM_PIXEL) | (1u << SM_HSYNC) | (1u << SM_VSYNC);
    if (on) {
        pio_enable_sm_mask_in_sync(pio_crt, mask);
    } else {
        pio_set_sm_mask_enabled(pio_crt, mask, false);
    }
}

static void apply_video_mode(VideoMode mode) {
    irq_set_enabled(PIO0_IRQ_0, false);
    scanout_enable(false);
    abort_dma();
    pio_interrupt_clear(pio_crt, PIO_IRQ_REWIND);

    bool hz_change = mode_78() != (mode == MODE_78_80 || mode == MODE_78_132);

    video_mode = mode;
    if (hz_change) {
        v_back_porch = mode_v_bp_default();
    } else if (v_back_porch > mode_v_bp_max()) {
        v_back_porch = mode_v_bp_max();
    }
    gpio_put(PIN_VSIZE78, mode_78() ? 1 : 0);
    load_timing_programs();
    configure_pio_sms();
    draw_pattern(current_pat);
    apply_timing(h_delay_words, v_back_porch);
    configure_dma();

    irq_set_enabled(PIO0_IRQ_0, true);
    scanout_enable(true);
}

static void init_crt_pio(PIO pio) {
    pio_crt = pio;
    offset_pixel = pio_add_program(pio, &video_pixel_program);
    hsync_prog = NULL;
    vsync_prog = NULL;
    load_timing_programs();

    pio_gpio_init(pio, PIN_V0);
    pio_gpio_init(pio, PIN_V1);
    pio_gpio_init(pio, PIN_HSYNC);
    pio_gpio_init(pio, PIN_VSYNC);

    configure_pio_sms();

    pio_set_irq0_source_enabled(pio, pis_interrupt2, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio_rewind_irq);
    irq_set_priority(PIO0_IRQ_0, 0);
    irq_set_enabled(PIO0_IRQ_0, true);
}

static void setup_framebuffer_dma(void) {
    data_chan = dma_claim_unused_channel(true);
    drain_chan = dma_claim_unused_channel(true);
    kick_chan = dma_claim_unused_channel(true);
    configure_dma();
}

static void nudge_h(int delta_words) {
    int next = (int)h_delay_words + delta_words;
    if (next < 0) {
        next = 0;
    }
    if (next > (int)H_DELAY_MAX) {
        next = H_DELAY_MAX;
    }
    request_timing((uint8_t)next, v_back_porch);
    print_status();
}

static void nudge_v(int delta_lines) {
    int next = (int)v_back_porch + delta_lines;
    if (next < V_BP_MIN) {
        next = V_BP_MIN;
    }
    if (next > (int)mode_v_bp_max()) {
        next = mode_v_bp_max();
    }
    request_timing(h_delay_words, (uint8_t)next);
    print_status();
}

static void handle_key(int c) {
    switch (c) {
    case '1':
        draw_pattern(PAT_PLUS);
        print_status();
        break;
    case '2':
        draw_pattern(PAT_BOX);
        print_status();
        break;
    case '3':
        draw_pattern(PAT_GRID);
        print_status();
        break;
    case '4':
        draw_pattern(PAT_MEAS);
        print_status();
        break;
    case 'm':
    case 'M':
        apply_video_mode(toggle_cols(video_mode));
        print_status();
        break;
    case 'r':
    case 'R':
        apply_video_mode(toggle_hz(video_mode));
        print_status();
        break;
    case 'a':
        nudge_h(-1);
        break;
    case 'd':
        nudge_h(1);
        break;
    case 'A':
        nudge_h(-4);
        break;
    case 'D':
        nudge_h(4);
        break;
    case 'w':
        nudge_v(-1);
        break;
    case 's':
        nudge_v(1);
        break;
    case 'W':
        nudge_v(-5);
        break;
    case 'S':
        nudge_v(5);
        break;
    case '0':
        request_timing(0, mode_v_bp_default());
        print_status();
        break;
    case '?':
        print_help();
        break;
    default:
        break;
    }
}

int main(void) {
    set_sys_clock_khz(CRT_SYS_CLK_KHZ, true);
    stdio_init_all();

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    gpio_init(PIN_VSIZE78);
    gpio_set_dir(PIN_VSIZE78, GPIO_OUT);
    gpio_put(PIN_VSIZE78, 0);

    memset(frame_buffer, 0, sizeof(frame_buffer));
    memset(blank_line, 0, sizeof(blank_line));
    h_delay_words = 0;
    v_back_porch = V_BP_DEFAULT_60;
    video_mode = MODE_60_80;
    draw_pattern(PAT_MEAS);
    rebuild_line_table(0, V_BP_DEFAULT_60);
    init_crt_pio(pio0);
    setup_framebuffer_dma();
    scanout_enable(true);

    print_help();

    absolute_time_t next_banner = make_timeout_time_ms(BANNER_MS);
    while (true) {
        int c = getchar_timeout_us(KEY_POLL_US);
        if (c >= 0) {
            handle_key(c);
        }
        if (time_reached(next_banner)) {
            next_banner = make_timeout_time_ms(BANNER_MS);
#ifdef PICO_DEFAULT_LED_PIN
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
#endif
            print_status();
        }
    }
}
