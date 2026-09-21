#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "video.h"
#include "assets/indian_head/indian_head_pattern.h"

#define PATTERN_COUNT       6
#define BOOTSEL_POLL_US     10000
#define BOOTSEL_DEBOUNCE_MS 50
#define BANNER_MS           250
#define SYNC_SQUARE_SIZE    80   /* equal pixel sides; mm/px */
#define SYNC_MM_SQUARE_W    320  /* 100 mm on 250 mm bezel: 100/250 * 800 */
#define SYNC_MM_SQUARE_H    178  /* 100 mm on 190 mm bezel: 100/190 * 338 */
#define SYNC_CROSS_ARM      12

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

/* 7x10 H; bit 6 is the left column of the inner cell. */
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

void generate_intensity_bars(void) {
    static const PixelColor bands[4] = {
        PIXEL_OFF, PIXEL_DIM, PIXEL_NORMAL, PIXEL_BOLD
    };
    const uint16_t base = FRAME_HEIGHT / 4;
    const uint16_t rem = FRAME_HEIGHT % 4;
    uint16_t y = 0;

    for (int i = 0; i < 4; i++) {
        uint16_t h = (uint16_t)(base + (i >= (4 - (int)rem) ? 1 : 0));
        uint16_t y_end = (uint16_t)(y + h);
        for (; y < y_end; y++) {
            fill_active_line(y, bands[i]);
        }
    }
}

static void draw_glyph_h(uint16_t cell_col, uint16_t cell_row, PixelColor color) {
    uint16_t ox = (uint16_t)(cell_col * CELL_WIDTH + GLYPH_ORIGIN_X);
    uint16_t oy = (uint16_t)(cell_row * CELL_HEIGHT + GLYPH_ORIGIN_Y);

    for (uint16_t row = 0; row < GLYPH_HEIGHT; row++) {
        uint8_t bits = glyph_h[row];
        for (uint16_t col = 0; col < GLYPH_WIDTH; col++) {
            if (bits & (uint8_t)(1u << (GLYPH_WIDTH - 1 - col))) {
                set_pixel((uint16_t)(ox + col), (uint16_t)(oy + row), color);
            }
        }
    }
}

void generate_focus_matrix(void) {
    const uint16_t cx = (CHAR_COLS - 1) / 2; /* 39 */
    const uint16_t cy = (CHAR_ROWS - 1) / 2; /* 12 */

    clear_buffer(PIXEL_OFF);
    for (uint16_t row = 0; row < CHAR_ROWS; row++) {
        for (uint16_t col = 0; col < CHAR_COLS; col++) {
            PixelColor color = (col == cx && row == cy) ? PIXEL_BOLD : PIXEL_NORMAL;
            draw_glyph_h(col, row, color);
        }
    }
}

void generate_full_on_box(void) {
    clear_buffer(PIXEL_BOLD);
}

static void draw_rect_outline(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                              PixelColor color) {
    for (uint16_t x = x0; x <= x1; x++) {
        set_pixel(x, y0, color);
        set_pixel(x, y1, color);
    }
    for (uint16_t y = y0; y <= y1; y++) {
        set_pixel(x0, y, color);
        set_pixel(x1, y, color);
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
        set_pixel(x, (uint16_t)(cy - 1), color);
        set_pixel(x, cy, color);
        set_pixel(x, (uint16_t)(cy + 1), color);
    }
    for (uint16_t y = (uint16_t)(cy - arm); y <= (uint16_t)(cy + arm); y++) {
        set_pixel((uint16_t)(cx - 1), y, color);
        set_pixel(cx, y, color);
        set_pixel((uint16_t)(cx + 1), y, color);
    }
}

void generate_sync_squares_pattern(void) {
    const uint16_t cx = FRAME_WIDTH / 2;   /* 400 */
    const uint16_t cy = FRAME_HEIGHT / 2;  /* 169 */

    clear_buffer(PIXEL_OFF);
    draw_rect_outline(0, 0, FRAME_WIDTH - 1, FRAME_HEIGHT - 1, PIXEL_BOLD);
    draw_centered_rect(cx, cy, SYNC_MM_SQUARE_W, SYNC_MM_SQUARE_H, PIXEL_BOLD);
    draw_centered_rect(cx, cy, SYNC_SQUARE_SIZE, SYNC_SQUARE_SIZE, PIXEL_NORMAL);
    draw_cross(cx, cy, SYNC_CROSS_ARM, PIXEL_BOLD);
}

void generate_indian_head_pattern(void) {
    for (uint16_t y = 0; y < FRAME_HEIGHT; y++) {
        memcpy(frame_buffer[y], indian_head_packed[y], BYTES_PER_LINE);
        memset(&frame_buffer[y][BYTES_PER_LINE], 0, LINE_STRIDE - BYTES_PER_LINE);
    }
}

/* BOOTSEL is flash CS. Sample from RAM with XIP Hi-Z (pico-examples picoboard/button). */
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
        pattern_name = "crosshatch";
        break;
    }
    printf("crt-pattern pattern=%s 78Hz\n", pattern_name);
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

static void print_pattern_help(void) {
    printf("crt-pattern 78Hz: BOOTSEL cycles  1/c crosshatch  2/i intensity  3/f focus  4/n indian-head  5/o full-on  6/s sync-squares\n");
}

int main(void) {
    video_set_sys_clock();
    stdio_init_all();

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    apply_pattern(PATTERN_SYNC_SQUARES);
    video_start(pio0);
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
            case 's':
            case 'S':
                apply_pattern(PATTERN_SYNC_SQUARES);
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
            printf("crt-pattern pattern=%s 78Hz\n", pattern_name);
        }
    }
}
