#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "scanout.h"
#include "assets/indian_head/indian_head_pattern.h"

#define PATTERN_COUNT       6
#define BOOTSEL_POLL_US     10000
#define BOOTSEL_DEBOUNCE_MS 50
#define BANNER_MS           250
#define SYNC_SQUARE_SIZE    80
#define SYNC_CROSS_ARM      12
#define IH_WIDTH            800
#define IH_HEIGHT           338
/* 4:3 card inside the pack. Side columns are the retrace stimulus, not the picture. */
#define IH_CARD_X           176
#define IH_CARD_Y           1
#define IH_CARD_W           448
#define IH_CARD_H           336
/* Vertical pitch / horizontal pitch on this MC5: (154 mm / 504) / (214 mm / 1968). */
#define IH_PITCH_NUM        281
#define IH_PITCH_DEN        100
#define IH_BURST_X          632
#define IH_BURST_W          160
#define IH_BURST_Y          56
#define CHAR_ROWS           26
#define GLYPH_WIDTH         7
#define GLYPH_HEIGHT        10
#define GLYPH_ORIGIN_X      1
#define GLYPH_ORIGIN_Y      1

typedef enum {
    PATTERN_CROSSHATCH = 0,
    PATTERN_INTENSITY,
    PATTERN_FOCUS,
    PATTERN_INDIAN_HEAD,
    PATTERN_FULL_ON,
    PATTERN_SYNC_SQUARES
} PatternId;

static const char *pattern_name = "sync-squares";
static PatternId current_pattern = PATTERN_SYNC_SQUARES;

static const uint8_t glyph_h[GLYPH_HEIGHT] = {
    0b1000001,
    0b1000001,
    0b1000001,
    0b1000001,
    0b1111111,
    0b1000001,
    0b1000001,
    0b1000001,
    0b1000001,
    0b1000001,
};

static uint16_t mm100_w(void) {
    uint16_t fill_mm = scanout_mode_78() ? 226 : 225;
    return (uint16_t)((100u * (uint32_t)scanout_width() + fill_mm / 2u) / fill_mm);
}

static uint16_t mm100_h(void) {
    uint16_t fill_mm = scanout_mode_78() ? 157 : 170;
    return (uint16_t)((100u * (uint32_t)scanout_height() + fill_mm / 2u) / fill_mm);
}

static void draw_rect_outline(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                              PixelColor color) {
    for (uint16_t x = x0; x <= x1; x++) {
        scanout_set_pixel(x, y0, color);
        scanout_set_pixel(x, y1, color);
    }
    for (uint16_t y = y0; y <= y1; y++) {
        scanout_set_pixel(x0, y, color);
        scanout_set_pixel(x1, y, color);
    }
}

static void draw_centered_rect(uint16_t cx, uint16_t cy, uint16_t w, uint16_t h,
                               PixelColor color) {
    uint16_t x0 = (uint16_t)(cx - w / 2);
    uint16_t y0 = (uint16_t)(cy - h / 2);
    uint16_t x1 = (uint16_t)(cx + w / 2 - 1);
    uint16_t y1 = (uint16_t)(cy + h / 2 - 1);
    draw_rect_outline(x0, y0, x1, y1, color);
}

static void draw_cross(uint16_t cx, uint16_t cy, uint16_t arm, PixelColor color) {
    for (uint16_t x = (uint16_t)(cx - arm); x <= (uint16_t)(cx + arm); x++) {
        scanout_set_pixel(x, (uint16_t)(cy - 1), color);
        scanout_set_pixel(x, cy, color);
        scanout_set_pixel(x, (uint16_t)(cy + 1), color);
    }
    for (uint16_t y = (uint16_t)(cy - arm); y <= (uint16_t)(cy + arm); y++) {
        scanout_set_pixel((uint16_t)(cx - 1), y, color);
        scanout_set_pixel(cx, y, color);
        scanout_set_pixel((uint16_t)(cx + 1), y, color);
    }
}

static void pack_pixel(uint8_t *row, uint16_t x, PixelColor color);

/* One pixel at the factory 32.1 MHz dot clock. Neighbors stay dark so the
 * stars do not chain into strokes. */
static uint32_t mc_state;

static uint32_t mc_next(void) {
    uint32_t x = mc_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    mc_state = x;
    return x;
}

static void generate_monte_carlo(void) {
    uint16_t w = scanout_width();
    uint16_t h = scanout_height();
    const uint16_t step = 18;

    scanout_clear(PIXEL_OFF);
    mc_state = 0xA341316Cu;
    for (uint16_t y = 2; y + 2 < h; y = (uint16_t)(y + step)) {
        for (uint16_t x = 2; x + 2 < w; x = (uint16_t)(x + step)) {
            uint16_t jx = (uint16_t)(mc_next() % (step - 4));
            uint16_t jy = (uint16_t)(mc_next() % (step - 4));
            scanout_set_pixel((uint16_t)(x + jx), (uint16_t)(y + jy), PIXEL_BOLD);
        }
    }
    printf("monte carlo %ux%u  1-dot stars\n", (unsigned)w, (unsigned)h);
}

void generate_crosshatch_pattern(void) {
    generate_monte_carlo();
}

void generate_intensity_bars(void) {
    static const PixelColor bands[4] = {
        PIXEL_OFF, PIXEL_DIM, PIXEL_NORMAL, PIXEL_BOLD
    };
    uint16_t height = scanout_height();
    uint16_t base = (uint16_t)(height / 4);
    uint16_t rem = (uint16_t)(height % 4);
    uint16_t y = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t bh = (uint16_t)(base + (i >= (4 - (int)rem) ? 1 : 0));
        uint16_t y_end = (uint16_t)(y + bh);
        for (; y < y_end; y++) {
            scanout_fill_line(y, bands[i]);
        }
    }
}

static void draw_glyph_h(uint16_t cell_col, uint16_t cell_row, PixelColor color) {
    uint16_t cw = scanout_char_w();
    uint16_t ch = scanout_char_h();
    uint16_t ox = (uint16_t)(cell_col * cw + GLYPH_ORIGIN_X);
    uint16_t oy = (uint16_t)(cell_row * ch + GLYPH_ORIGIN_Y);

    for (uint16_t row = 0; row < GLYPH_HEIGHT; row++) {
        uint8_t bits = glyph_h[row];
        for (uint16_t col = 0; col < GLYPH_WIDTH; col++) {
            if (bits & (uint8_t)(1u << (GLYPH_WIDTH - 1 - col))) {
                scanout_set_pixel((uint16_t)(ox + col), (uint16_t)(oy + row), color);
            }
        }
    }
}

void generate_focus_matrix(void) {
    uint16_t cw = scanout_char_w();
    uint16_t cols = (uint16_t)(scanout_width() / cw);
    uint16_t cx = (uint16_t)((cols - 1) / 2);
    uint16_t cy = (CHAR_ROWS - 1) / 2;

    scanout_clear(PIXEL_OFF);
    for (uint16_t row = 0; row < CHAR_ROWS; row++) {
        for (uint16_t col = 0; col < cols; col++) {
            PixelColor color = (col == cx && row == cy) ? PIXEL_BOLD : PIXEL_NORMAL;
            draw_glyph_h(col, row, color);
        }
    }
}

void generate_full_on_box(void) {
    scanout_clear(PIXEL_BOLD);
}

void generate_sync_squares_pattern(void) {
    uint16_t w = scanout_width();
    uint16_t h = scanout_height();
    uint16_t cx = (uint16_t)(w / 2);
    uint16_t cy = (uint16_t)(h / 2);

    scanout_clear(PIXEL_OFF);
    draw_rect_outline(0, 0, (uint16_t)(w - 1), (uint16_t)(h - 1), PIXEL_BOLD);
    draw_centered_rect(cx, cy, mm100_w(), mm100_h(), PIXEL_BOLD);
    draw_centered_rect(cx, cy, SYNC_SQUARE_SIZE, SYNC_SQUARE_SIZE, PIXEL_NORMAL);
    draw_cross(cx, cy, SYNC_CROSS_ARM, PIXEL_BOLD);
}

static PixelColor ih_pixel(const uint8_t *row, uint16_t x) {
    uint8_t shift = (uint8_t)((3 - (x % 4)) * 2);
    return (PixelColor)((row[x / 4] >> shift) & 0b11);
}

static void pack_pixel(uint8_t *row, uint16_t x, PixelColor color) {
    uint16_t byte_idx = (uint16_t)((x / 4) ^ 3);
    uint8_t shift = (uint8_t)((3 - (x % 4)) * 2);

    row[byte_idx] &= (uint8_t)~(0b11 << shift);
    row[byte_idx] |= (uint8_t)((color & 0b11) << shift);
}

/* Three levels in the /HSYNC window. A leak is vertical bars, not the card. */
static PixelColor retrace_bar(uint16_t x) {
    if (x < 80) {
        return PIXEL_DIM;
    }
    if (x < 96) {
        return PIXEL_OFF;
    }
    if (x < 176) {
        return PIXEL_NORMAL;
    }
    if (x < 192) {
        return PIXEL_OFF;
    }
    return PIXEL_BOLD;
}

void generate_indian_head_pattern(void) {
    uint16_t w = scanout_width();
    uint16_t h = scanout_height();
    uint32_t dst_w32 = (uint32_t)IH_CARD_W * h * IH_PITCH_NUM / (IH_CARD_H * IH_PITCH_DEN);
    uint16_t dst_h = h;
    uint16_t dst_w = dst_w32 > w ? w : (uint16_t)dst_w32;
    uint16_t ox = (uint16_t)((w - dst_w) / 2);

    scanout_clear(PIXEL_OFF);
    for (uint16_t y = 0; y < dst_h; y++) {
        uint16_t sy = (uint16_t)(IH_CARD_Y + (uint32_t)y * IH_CARD_H / dst_h);
        const uint8_t *src = indian_head_packed[sy];
        for (uint16_t x = 0; x < dst_w; x++) {
            uint16_t sx = (uint16_t)(IH_CARD_X + (uint32_t)x * IH_CARD_W / dst_w);
            scanout_set_pixel((uint16_t)(ox + x), y, ih_pixel(src, sx));
        }
    }

    /* Outside the frame: vertical bars during /HSYNC, horizontal bursts on
     * the vertical porch and sync. Both stay dark if blanking holds. */
    uint8_t *hsync = scanout_retrace_hsync();
    memset(hsync, 0, SCANOUT_RETRACE_BYTES);
    for (uint16_t x = 0; x < SCANOUT_RETRACE_BYTES * 4; x++) {
        pack_pixel(hsync, x, retrace_bar(x));
    }
    uint8_t *vblank = scanout_retrace_vblank();
    memset(vblank, 0, scanout_store_bytes());
    const uint8_t *burst = indian_head_packed[IH_BURST_Y];
    for (uint16_t x = 0; x < w; x++) {
        uint16_t sx = (uint16_t)(IH_BURST_X + (x % IH_BURST_W));
        pack_pixel(vblank, x, ih_pixel(burst, sx));
    }
    scanout_retrace_test(true);
}

static bool __no_inline_not_in_flash_func(get_bootsel_button)(void) {
    const uint cs_pin_index = 1;
    uint32_t flags = save_and_disable_interrupts();

    hw_write_masked(&ioqspi_hw->io[cs_pin_index].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 1000; ++i) {
    }

#if PICO_RP2040
    const uint32_t cs_bit = 1u << 1;
#else
    const uint32_t cs_bit = SIO_GPIO_HI_IN_QSPI_CSN_BITS;
#endif
    bool pressed = !(sio_hw->gpio_hi_in & cs_bit);

    hw_write_masked(&ioqspi_hw->io[cs_pin_index].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    restore_interrupts(flags);
    return pressed;
}

static void apply_pattern(PatternId id) {
    scanout_retrace_test(false);
    scanout_margin_blips(false);
    current_pattern = id;
    switch (id) {
    case PATTERN_INTENSITY:
        generate_intensity_bars();
        pattern_name = "intensity";
        break;
    case PATTERN_FOCUS:
        generate_focus_matrix();
        pattern_name = "focus";
        break;
    case PATTERN_INDIAN_HEAD:
        generate_indian_head_pattern();
        pattern_name = "indian-head";
        break;
    case PATTERN_FULL_ON:
        generate_full_on_box();
        pattern_name = "full-on";
        break;
    case PATTERN_SYNC_SQUARES:
        generate_sync_squares_pattern();
        pattern_name = "sync-squares";
        break;
    case PATTERN_CROSSHATCH:
    default:
        generate_crosshatch_pattern();
        pattern_name = "monte-carlo";
        break;
    }
    scanout_print_status(pattern_name);
    if (id == PATTERN_FULL_ON) {
        printf("full-on is a beam-current stress pattern; switch off when done\n");
    }
}

static void cycle_pattern(void) {
    apply_pattern((PatternId)(((unsigned)current_pattern + 1u) % PATTERN_COUNT));
}

static void poll_bootsel(void) {
    static bool latched;
    static absolute_time_t ignore_until;

    if (!time_reached(ignore_until)) {
        return;
    }

    bool pressed = get_bootsel_button();
    if (pressed && !latched) {
        cycle_pattern();
        latched = true;
        ignore_until = make_timeout_time_ms(BOOTSEL_DEBOUNCE_MS);
    } else if (!pressed && latched) {
        latched = false;
        ignore_until = make_timeout_time_ms(BOOTSEL_DEBOUNCE_MS);
    }
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

static void apply_mode(VideoMode mode) {
    scanout_set_mode(mode);
    apply_pattern(current_pattern);
}

static void print_pattern_help(void) {
    printf("crt-pattern: BOOTSEL cycles  1/c crosshatch  2/i intensity  3/f focus\n");
    printf("  4/n indian-head  5/o full-on  6 sync-squares  r 60/78\n");
    printf("  raster %u x %u  (factory max 1188 x 416)\n",
           (unsigned)scanout_width(), (unsigned)scanout_height());
    printf("  a/d H 16px  A/D 64px  w/s V 1 line  W/S 5 lines  0 reset  ? help\n");
    scanout_print_status(pattern_name);
}

int main(void) {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    scanout_init(pio0);
    stdio_init_all();
    apply_pattern(PATTERN_CROSSHATCH);
    print_pattern_help();

    absolute_time_t next_banner = make_timeout_time_ms(BANNER_MS);
    while (true) {
        int c = getchar_timeout_us(BOOTSEL_POLL_US);
        poll_bootsel();

        if (c >= 0) {
            switch (c) {
            case '1':
            case 'c':
            case 'C':
                apply_pattern(PATTERN_CROSSHATCH);
                break;
            case '2':
            case 'i':
            case 'I':
                apply_pattern(PATTERN_INTENSITY);
                break;
            case '3':
            case 'f':
            case 'F':
                apply_pattern(PATTERN_FOCUS);
                break;
            case '4':
            case 'n':
            case 'N':
                apply_pattern(PATTERN_INDIAN_HEAD);
                break;
            case '5':
            case 'o':
            case 'O':
                apply_pattern(PATTERN_FULL_ON);
                break;
            case '6':
                apply_pattern(PATTERN_SYNC_SQUARES);
                break;
            case 'm':
            case 'M':
                apply_mode(toggle_cols(scanout_mode()));
                break;
            case 'r':
            case 'R':
                apply_mode(toggle_hz(scanout_mode()));
                break;
            case 'a':
                scanout_nudge_h(-1);
                scanout_print_status(pattern_name);
                break;
            case 'd':
                scanout_nudge_h(1);
                scanout_print_status(pattern_name);
                break;
            case 'A':
                scanout_nudge_h(-4);
                scanout_print_status(pattern_name);
                break;
            case 'D':
                scanout_nudge_h(4);
                scanout_print_status(pattern_name);
                break;
            case 'w':
                scanout_nudge_v(-1);
                scanout_print_status(pattern_name);
                break;
            case 's':
                scanout_nudge_v(1);
                scanout_print_status(pattern_name);
                break;
            case 'W':
                scanout_nudge_v(-5);
                scanout_print_status(pattern_name);
                break;
            case 'S':
                scanout_nudge_v(5);
                scanout_print_status(pattern_name);
                break;
            case '0':
                scanout_reset_timing();
                scanout_print_status(pattern_name);
                break;
            case '?':
                print_pattern_help();
                break;
            default:
                break;
            }
        }

        if (time_reached(next_banner)) {
            next_banner = make_timeout_time_ms(BANNER_MS);
#ifdef PICO_DEFAULT_LED_PIN
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
#endif
            scanout_print_status(pattern_name);
        }
    }
}
