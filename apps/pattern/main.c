#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "scanout.h"
#include "font.h"
#include "indian_head.h"

#define PATTERN_COUNT       7
/* Pixel-code. Keep in lockstep with tools/glass_code.py.
 * A group is five 20-dot columns. The dot's place in its column is a nibble:
 * line[3:0], line[7:4], line[11:8], group, parity. Picture groups are staggered
 * so a column is dark for 7 lines and the dot does not stack into a bar.
 * Groups that start at or past the active width, and every group on a vertical
 * porch or sync line, are drawn on every line — the blanking-interval code. */
#define CODE_COL_W          20
#define CODE_NIBBLES        5
#define CODE_GROUP_W        (CODE_NIBBLES * CODE_COL_W)
#define CODE_STRIDE         8
#define BOOTSEL_POLL_US     10000
#define BOOTSEL_DEBOUNCE_MS 50
#define BANNER_MS           250
#define SYNC_SQUARE_SIZE    80
#define SYNC_CROSS_ARM      12
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
    PATTERN_SYNC_SQUARES,
    PATTERN_PIXEL_CODE
} PatternId;

static const char *pattern_name = "indian-head";
static PatternId current_pattern = PATTERN_INDIAN_HEAD;

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
    for (uint16_t x = 8; x < w; x = (uint16_t)(x + step * 3u)) {
        scanout_set_pixel(x, 0, PIXEL_BOLD);
        scanout_set_pixel(x, (uint16_t)(h - 1), PIXEL_BOLD);
    }
    printf("monte carlo %ux%u  1-dot stars\n", (unsigned)w, (unsigned)h);
}

void generate_crosshatch_pattern(void) {
    generate_monte_carlo();
}

static uint8_t code_nibble(uint16_t line, uint16_t group, int n) {
    uint8_t a = (uint8_t)(line & 15u);
    uint8_t b = (uint8_t)((line >> 4) & 15u);
    uint8_t c = (uint8_t)((line >> 8) & 15u);
    uint8_t d = (uint8_t)(group & 15u);

    switch (n) {
    case 0:
        return a;
    case 1:
        return b;
    case 2:
        return c;
    case 3:
        return d;
    default:
        return (uint8_t)(a ^ b ^ c ^ d);
    }
}

static int code_emit_group(uint16_t line, uint16_t group, uint16_t x0,
                           uint16_t active0, uint16_t active1, uint16_t picture_x) {
    if (line < active0 || line >= active1 || x0 >= picture_x) {
        return 1;
    }
    return (line % CODE_STRIDE) == (group % CODE_STRIDE);
}

static void generate_pixel_code(void) {
    uint16_t picture_x = scanout_width();
    uint16_t signal_w = scanout_signal_width();
    uint16_t nlines = scanout_frame_lines();
    uint16_t active0 = scanout_vbp();
    uint16_t active1 = (uint16_t)(active0 + scanout_height());
    uint16_t groups = (uint16_t)(signal_w / CODE_GROUP_W);

    scanout_clear(PIXEL_OFF);
    scanout_code_blanking(true);
    for (uint16_t line = 0; line < nlines; line++) {
        for (uint16_t group = 0; group < groups; group++) {
            uint16_t x0 = (uint16_t)(group * CODE_GROUP_W);
            if (!code_emit_group(line, group, x0, active0, active1, picture_x)) {
                continue;
            }
            for (int n = 0; n < CODE_NIBBLES; n++) {
                uint16_t x = (uint16_t)(x0 + n * CODE_COL_W + code_nibble(line, group, n));
                scanout_set_frame_pixel(line, x, PIXEL_BOLD);
            }
        }
    }
    printf("pixel-code frame %u  signal %u  groups %u  stride %u\n",
           (unsigned)nlines, (unsigned)signal_w, (unsigned)groups, CODE_STRIDE);
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
    scanout_code_blanking(false);
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
        indian_head_standby();
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
    case PATTERN_PIXEL_CODE:
        generate_pixel_code();
        pattern_name = "pixel-code";
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
    printf("  4/n indian-head  5/o full-on  6 sync-squares  7/g pixel-code\n");
    printf("  r 60/78\n");
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
    font_init();
    apply_pattern(current_pattern);
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
            case '7':
            case 'g':
            case 'G':
                apply_pattern(PATTERN_PIXEL_CODE);
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
