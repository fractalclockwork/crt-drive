#include <stdio.h>
#include <string.h>
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"

#include "scanout.h"
#include "video_pixel.pio.h"
#include "hsync60.pio.h"
#include "hsync60_132.pio.h"
#include "vsync60.pio.h"
#ifdef CRT_VSYNC_PUSH
#include "vsync_push.pio.h"
#else
#include "vsync.pio.h"
#endif

#define SM_PIXEL        0
#define SM_HSYNC        1
#define SM_VSYNC        2

#define CRT_SYS_CLK_KHZ 128400
/* Factory basis. 80-col: clkdiv 4 → 32.1 MHz, 1024-dot line.
 * 132-col: clkdiv 2.675 → ~48 MHz, 1530-dot line. Same fH, so L201 stays. */
#define PIO_CLKDIV_80   4.0f
#define PIO_CLKDIV_132  (128400.0f / 48000.0f)
#define PIO_IRQ_REWIND  2

#define WIDTH_80        800
#define WIDTH_132       1188
#ifndef CRT_HEIGHT_60
#define HEIGHT_60       416                 /* 26 × 16 */
#else
#define HEIGHT_60       CRT_HEIGHT_60
#endif
#define HEIGHT_78       377                 /* 13 × 29 */
/* Longest frame. 78 Hz uses the first 402 lines. Porch lines live here so a
 * code can be clocked in vertical blanking without a second DMA. */
#define FB_HEIGHT       V_LINES_60
#define V_SYNC_LINES    6
#define V_LINES_60      523
#ifndef CRT_VBLANK_60
#define V_BLANK_60      107                 /* 50 FP + 6 sync + 51 BP */
#else
#define V_BLANK_60      CRT_VBLANK_60
#endif
#define V_BLANK_78      25                  /* 11 FP + 6 sync + 8 BP; 402-line frame */
#define LINES_MAX       V_LINES_60
#define CELL_W_80       40
#define CELL_W_132      54
#define CELL_H_60       26
#define CELL_H_78       29
#define CHAR_W_80       10
#define CHAR_W_132      9
#define CHAR_H_60       16
#define CHAR_H_78       14
#define H_PAD_WORDS     5
#define H_PAD_BYTES     (H_PAD_WORDS * 4)
#define PIXEL_STORE     300                 /* 1188/4 = 297, 32-bit padded */
#define TAIL_BYTES      4
#define STORE_STRIDE    (H_PAD_BYTES + PIXEL_STORE + TAIL_BYTES)
/* Active pixels plus 16 black dots. The porch is a FIFO stall holding black.
 * A longer DMA runs into the next line and paints vertical streaks. */
#define DMA_BYTES_80    204                 /* 200 px + 4 off */
#define DMA_BYTES_132   304                 /* 300 store + 4 off */
_Static_assert(DMA_BYTES_80 / 4 * 16 < 1024, "80-col DMA fits the 1024-dot line");
_Static_assert(DMA_BYTES_132 / 4 * 16 < 1530, "132-col DMA fits the 1530-dot line");
_Static_assert(HEIGHT_60 + V_BLANK_60 == V_LINES_60, "60 Hz active + blank is one 523-line frame");
#define H_DELAY_MAX     H_PAD_WORDS
#ifndef CRT_VBP_60
#define V_BP_DEFAULT_60 51
#else
#define V_BP_DEFAULT_60 CRT_VBP_60
#endif
#define V_BP_DEFAULT_78 8
#define V_BP_MIN        1
/* Kept for the retrace-mark API. Not chained onto the factory line. */
#define STIM_WORDS      17
#define SUFFIX_WORDS    17

static uint8_t frame_buffer[FB_HEIGHT][STORE_STRIDE] __attribute__((aligned(4)));
static uint32_t blank_line[STORE_STRIDE / 4] __attribute__((aligned(4)));
static uint32_t vblank_line[STORE_STRIDE / 4] __attribute__((aligned(4)));
static uint32_t suffix[SUFFIX_WORDS] __attribute__((aligned(4)));
static const uint32_t *line_ptrs[LINES_MAX];
static uint32_t dma_rx_dummy;
static int data_chan;
static int suffix_chan;
static int reload_chan;
static int drain_chan;
static int kick_chan;
/* Suffix DMA increments its read address. The reload channel writes this
 * pointer back at the end of every line so the margin ticks repeat. */
static uint32_t suffix_reload_addr;
static bool retrace_test;
static bool margin_blips;
static bool code_blanking;

static PIO pio_crt;
static uint offset_pixel;
static uint offset_hsync;
static uint offset_vsync;
static const pio_program_t *hsync_prog;
static const pio_program_t *vsync_prog;

static volatile uint8_t h_delay_words;
static volatile uint8_t v_back_porch = V_BP_DEFAULT_60;
static volatile bool timing_dirty;
static VideoMode video_mode = MODE_60_80;

static bool mode_132(void) {
    return video_mode == MODE_60_132 || video_mode == MODE_78_132;
}

static bool mode_78(void) {
    return video_mode == MODE_78_80 || video_mode == MODE_78_132;
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

static uint dma_words(uint8_t delay_words) {
    /* 63 words = 1008 dots on the 1024-dot line. The last 16 dots stall black
     * so the transfer finishes before the next line is kicked. */
    if (code_blanking && !mode_132()) {
        return 63;
    }
    return delay_words + mode_dma_bytes() / 4;
}

static void rebuild_line_table(uint8_t delay_words, uint8_t v_bp);

static uint8_t *pixel_base(uint16_t y) {
    return &frame_buffer[y][H_PAD_BYTES];
}

static uint8_t packed_color(PixelColor color) {
    return (uint8_t)((color << 6) | (color << 4) | (color << 2) | color);
}

VideoMode scanout_mode(void) {
    return video_mode;
}

bool scanout_mode_78(void) {
    return mode_78();
}

bool scanout_mode_132(void) {
    return mode_132();
}

uint16_t scanout_width(void) {
    return mode_132() ? WIDTH_132 : WIDTH_80;
}

uint16_t scanout_height(void) {
    return mode_78() ? HEIGHT_78 : HEIGHT_60;
}

uint16_t scanout_signal_width(void) {
    return mode_132() ? WIDTH_132 : 1008;
}

uint16_t scanout_frame_lines(void) {
    return mode_78() ? (uint16_t)(HEIGHT_78 + V_BLANK_78) : (uint16_t)V_LINES_60;
}

uint16_t scanout_beam_line(void) {
    uint32_t addr = dma_channel_hw_addr((uint)kick_chan)->read_addr;
    uint32_t base = (uint32_t)line_ptrs;

    if (addr <= base) {
        return 0;
    }
    uint32_t next = (addr - base) / sizeof(uint32_t);
    if (next > LINES_MAX) {
        next = LINES_MAX;
    }
    return (uint16_t)(next - 1u);
}

uint16_t scanout_cell_w(void) {
    return mode_132() ? CELL_W_132 : CELL_W_80;
}

uint16_t scanout_cell_h(void) {
    return mode_78() ? CELL_H_78 : CELL_H_60;
}

uint16_t scanout_char_w(void) {
    return mode_132() ? CHAR_W_132 : CHAR_W_80;
}

uint16_t scanout_char_h(void) {
    return mode_78() ? CHAR_H_78 : CHAR_H_60;
}

static void write_pixel(uint8_t *row, uint16_t x, PixelColor color) {
    uint16_t byte_idx = (uint16_t)((x / 4) ^ 3);
    uint8_t shift = (uint8_t)((3 - (x % 4)) * 2);

    row[byte_idx] &= (uint8_t)~(0b11 << shift);
    row[byte_idx] |= (uint8_t)((color & 0b11) << shift);
}

void scanout_set_pixel(uint16_t x, uint16_t y, PixelColor color) {
    if (x >= scanout_width() || y >= scanout_height()) {
        return;
    }
    write_pixel(pixel_base(y), x, color);
}

void scanout_set_frame_pixel(uint16_t line, uint16_t x, PixelColor color) {
    if (line >= FB_HEIGHT || x >= scanout_signal_width()) {
        return;
    }
    write_pixel(pixel_base(line), x, color);
}

void scanout_code_blanking(bool on) {
    code_blanking = on;
    rebuild_line_table(h_delay_words, v_back_porch);
    dma_channel_set_trans_count(data_chan, dma_words(h_delay_words), false);
}

void scanout_clear(PixelColor color) {
    uint8_t packed = packed_color(color);
    uint store = mode_dma_bytes() - TAIL_BYTES;

    for (uint16_t y = 0; y < FB_HEIGHT; y++) {
        memset(pixel_base(y), packed, store);
        memset(pixel_base(y) + store, 0, PIXEL_STORE + TAIL_BYTES - store);
    }
}

void scanout_fill_line(uint16_t y, PixelColor color) {
    if (y >= scanout_height()) {
        return;
    }
    uint store = mode_dma_bytes() - TAIL_BYTES;
    memset(pixel_base(y), packed_color(color), store);
    memset(pixel_base(y) + store, 0, PIXEL_STORE + TAIL_BYTES - store);
}

void scanout_scroll(uint16_t lines) {
    uint16_t h = scanout_height();
    if (lines == 0 || lines >= h) {
        scanout_clear(PIXEL_OFF);
        return;
    }
    memmove(frame_buffer[0], frame_buffer[lines], (size_t)(h - lines) * STORE_STRIDE);
    for (uint16_t y = (uint16_t)(h - lines); y < h; y++) {
        memset(frame_buffer[y], 0, STORE_STRIDE);
    }
}

uint8_t *scanout_row(uint16_t y) {
    if (y >= FB_HEIGHT) {
        return pixel_base(0);
    }
    return pixel_base(y);
}

uint16_t scanout_store_bytes(void) {
    return (uint16_t)(mode_dma_bytes() - TAIL_BYTES);
}

uint8_t scanout_hpad(void) {
    return (uint8_t)(h_delay_words * 16u);
}

uint8_t scanout_vbp(void) {
    return v_back_porch;
}

uint8_t scanout_vfp(void) {
    return (uint8_t)(mode_v_blank() - V_SYNC_LINES - v_back_porch);
}

uint8_t *scanout_retrace_hsync(void) {
    return (uint8_t *)(suffix + (SUFFIX_WORDS - STIM_WORDS));
}

uint8_t *scanout_retrace_vblank(void) {
    return (uint8_t *)vblank_line + H_PAD_BYTES;
}

void scanout_retrace_test(bool on) {
    retrace_test = on;
    if (!on) {
        memset(suffix, 0, sizeof(suffix));
        memset(vblank_line, 0, sizeof(vblank_line));
    }
    rebuild_line_table(h_delay_words, v_back_porch);
}

uint8_t *scanout_margin_right(void) {
    return (uint8_t *)suffix;
}

uint16_t scanout_margin_right_dots(void) {
    return (uint16_t)((SUFFIX_WORDS - STIM_WORDS) * 16u);
}

void scanout_margin_blips(bool on) {
    margin_blips = on;
    if (!on) {
        memset(suffix, 0, sizeof(suffix));
        memset(vblank_line, 0, sizeof(vblank_line));
    }
    rebuild_line_table(h_delay_words, v_back_porch);
}

static const uint32_t *blank_src(uint byte_off) {
    const uint32_t *row = retrace_test ? vblank_line : blank_line;
    return (const uint32_t *)((const uint8_t *)row + byte_off);
}

static void rebuild_line_table(uint8_t delay_words, uint8_t v_bp) {
    uint16_t h = scanout_height();
    uint8_t v_fp = (uint8_t)(mode_v_blank() - V_SYNC_LINES - v_bp);
    uint byte_off = H_PAD_BYTES - (uint)delay_words * 4u;

    if (code_blanking) {
        uint16_t nlines = scanout_frame_lines();
        for (int n = 0; n < LINES_MAX; n++) {
            if (n < (int)nlines) {
                line_ptrs[n] = (const uint32_t *)(frame_buffer[n] + byte_off);
            } else {
                line_ptrs[n] = blank_src(byte_off);
            }
        }
        return;
    }

    const uint32_t *porch = margin_blips
        ? (const uint32_t *)((const uint8_t *)vblank_line + byte_off)
        : blank_src(byte_off);

    for (int n = 0; n < v_bp; n++) {
        line_ptrs[n] = porch;
    }
    for (int y = 0; y < h; y++) {
        line_ptrs[v_bp + y] = (const uint32_t *)(frame_buffer[y] + byte_off);
    }
    const int tail = v_bp + h;
    for (int n = 0; n < v_fp; n++) {
        line_ptrs[tail + n] = porch;
    }
    for (int n = 0; n < V_SYNC_LINES; n++) {
        line_ptrs[tail + v_fp + n] = blank_src(byte_off);
    }
    const int used = tail + v_fp + V_SYNC_LINES;
    for (int n = used; n < LINES_MAX; n++) {
        line_ptrs[n] = blank_src(byte_off);
    }
}

static void apply_suffix(uint8_t delay_words) {
    (void)delay_words;
    suffix_reload_addr = (uint32_t)suffix;
    dma_channel_set_read_addr(suffix_chan, suffix, false);
    dma_channel_set_trans_count(suffix_chan, 0, false);
}

static void apply_timing(uint8_t delay_words, uint8_t v_bp) {
    rebuild_line_table(delay_words, v_bp);
    dma_channel_set_trans_count(data_chan, dma_words(delay_words), false);
    apply_suffix(delay_words);
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

    pio_sm_clear_fifos(pio, SM_HSYNC);
    pio_sm_clear_fifos(pio, SM_VSYNC);
}

/* FJOIN_TX is set on the pixel SM. pio_sm_clear_fifos toggles FJOIN_RX,
 * and both join bits set at once leaves the TX FIFO unable to drain.
 * out then stalls with the pins held at the forced black written above. */
static void flush_pixel_fifo(PIO pio) {
    uint32_t shift = pio->sm[SM_PIXEL].shiftctrl;

    hw_clear_bits(&pio->sm[SM_PIXEL].shiftctrl,
                  PIO_SM0_SHIFTCTRL_FJOIN_TX_BITS | PIO_SM0_SHIFTCTRL_AUTOPULL_BITS);
    pio_sm_restart(pio, SM_PIXEL);
    hw_xor_bits(&pio->sm[SM_PIXEL].shiftctrl, PIO_SM0_SHIFTCTRL_FJOIN_RX_BITS);
    hw_xor_bits(&pio->sm[SM_PIXEL].shiftctrl, PIO_SM0_SHIFTCTRL_FJOIN_RX_BITS);
    pio->sm[SM_PIXEL].shiftctrl = shift;
    pio_sm_restart(pio, SM_PIXEL);
}

/* Last instruction written to each SM. pio_sm_init's jmp is not blocking,
 * and a later exec replaces it. Restart clears a latched jmp, so this
 * jump is not followed by another restart. */
static void jump_sm(PIO pio, uint sm, uint pc) {
    pio_sm_exec(pio, sm, pio_encode_jmp(pc));
    for (uint32_t n = 0; n < 256u && pio_sm_is_exec_stalled(pio, sm); n++) {
        tight_loop_contents();
    }
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

    suffix_reload_addr = (uint32_t)suffix;
    dma_channel_config c_suf = dma_channel_get_default_config(suffix_chan);
    channel_config_set_transfer_data_size(&c_suf, DMA_SIZE_32);
    channel_config_set_read_increment(&c_suf, false);
    channel_config_set_write_increment(&c_suf, false);
    dma_channel_configure(
        suffix_chan,
        &c_suf,
        &dma_rx_dummy,
        suffix,
        1,
        false
    );

    dma_channel_config c_reload = dma_channel_get_default_config(reload_chan);
    channel_config_set_transfer_data_size(&c_reload, DMA_SIZE_32);
    channel_config_set_read_increment(&c_reload, false);
    channel_config_set_write_increment(&c_reload, false);
    dma_channel_configure(
        reload_chan,
        &c_reload,
        &dma_hw->ch[suffix_chan].read_addr,
        &suffix_reload_addr,
        1,
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

static void clear_pio_irqs(void) {
    pio_interrupt_clear(pio_crt, 0);
    pio_interrupt_clear(pio_crt, 1);
    pio_interrupt_clear(pio_crt, 2);
    pio_interrupt_clear(pio_crt, 3);
}

/* dma_channel_abort already waits until BUSY drops. Writing abort again
 * sticks on an idle channel (RP2040-E13): the bit never clears, and
 * scanout_set_mode stays here with the state machines disabled. */
static void abort_dma(void) {
    dma_channel_abort(kick_chan);
    dma_channel_abort(drain_chan);
    dma_channel_abort(reload_chan);
    dma_channel_abort(suffix_chan);
    dma_channel_abort(data_chan);
}

void scanout_enable(bool on) {
    uint32_t mask = (1u << SM_PIXEL) | (1u << SM_HSYNC) | (1u << SM_VSYNC);
    if (on) {
        pio_enable_sm_mask_in_sync(pio_crt, mask);
    } else {
        pio_set_sm_mask_enabled(pio_crt, mask, false);
    }
}

void scanout_set_mode(VideoMode mode) {
    irq_set_enabled(PIO0_IRQ_0, false);
    scanout_enable(false);
    /* Release an hsync that was stopped inside `irq` with the flag still
     * set. Restart does not clear PIO IRQ flags, and a stalled SM ignores
     * the jmp that pio_sm_init uses to put the PC back at the wrap. */
    clear_pio_irqs();
    pio_sm_restart(pio_crt, SM_PIXEL);
    pio_sm_restart(pio_crt, SM_HSYNC);
    pio_sm_restart(pio_crt, SM_VSYNC);
    abort_dma();

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
    apply_timing(h_delay_words, v_back_porch);
    configure_dma();

    clear_pio_irqs();
    flush_pixel_fifo(pio_crt);
    pio_sm_put(pio_crt, SM_PIXEL, 0);
    jump_sm(pio_crt, SM_PIXEL, offset_pixel);
    jump_sm(pio_crt, SM_HSYNC, offset_hsync);
    jump_sm(pio_crt, SM_VSYNC, offset_vsync);
    clear_pio_irqs();
    scanout_enable(true);
    irq_set_enabled(PIO0_IRQ_0, true);
}

void scanout_sm_instr(uint16_t instr[3]) {
    instr[0] = (uint16_t)pio_crt->sm[SM_PIXEL].instr;
    instr[1] = (uint16_t)pio_crt->sm[SM_HSYNC].instr;
    instr[2] = (uint16_t)pio_crt->sm[SM_VSYNC].instr;
}

void scanout_reset_pio(void) {
    scanout_set_mode(video_mode);
}

void scanout_init(PIO pio) {
    set_sys_clock_khz(CRT_SYS_CLK_KHZ, true);

    gpio_init(PIN_VSIZE78);
    gpio_set_dir(PIN_VSIZE78, GPIO_OUT);
    gpio_put(PIN_VSIZE78, 0);

    memset(frame_buffer, 0, sizeof(frame_buffer));
    memset(blank_line, 0, sizeof(blank_line));
    memset(vblank_line, 0, sizeof(vblank_line));
    memset(suffix, 0, sizeof(suffix));
    retrace_test = false;
    h_delay_words = 0;
    v_back_porch = V_BP_DEFAULT_60;
    video_mode = MODE_60_80;

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

    data_chan = dma_claim_unused_channel(true);
    suffix_chan = dma_claim_unused_channel(true);
    reload_chan = dma_claim_unused_channel(true);
    drain_chan = dma_claim_unused_channel(true);
    kick_chan = dma_claim_unused_channel(true);
    rebuild_line_table(0, V_BP_DEFAULT_60);
    configure_dma();
    scanout_enable(true);
}

void scanout_nudge_h(int delta_words) {
    int next = (int)h_delay_words + delta_words;
    if (next < 0) {
        next = 0;
    }
    if (next > (int)H_DELAY_MAX) {
        next = H_DELAY_MAX;
    }
    request_timing((uint8_t)next, v_back_porch);
}

void scanout_nudge_v(int delta_lines) {
    int next = (int)v_back_porch + delta_lines;
    if (next < V_BP_MIN) {
        next = V_BP_MIN;
    }
    if (next > (int)mode_v_bp_max()) {
        next = mode_v_bp_max();
    }
    request_timing(h_delay_words, (uint8_t)next);
}

void scanout_reset_timing(void) {
    request_timing(0, mode_v_bp_default());
}

void scanout_print_status(const char *pat) {
    uint8_t v_fp = (uint8_t)(mode_v_blank() - V_SYNC_LINES - v_back_porch);
    printf("crt-pattern %s %ux%u %s hpad=%u vbp=%u vfp=%u vsize78=%u\n",
           mode_78() ? "78Hz" : "60Hz",
           (unsigned)scanout_width(), (unsigned)scanout_height(),
           pat,
           (unsigned)h_delay_words * 16u, (unsigned)v_back_porch, (unsigned)v_fp,
           mode_78() ? 1u : 0u);
}
