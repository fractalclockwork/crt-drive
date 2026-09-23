#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "scanout.h"
#include "screen.h"
#include "tty.h"
#include "term_build_id.h"

#define BANNER_MS           250
#define BOOTSEL_POLL_US     10000
#define BOOTSEL_DEBOUNCE_MS 50

static bool host_spoke;

static const VideoMode k_modes[] = {
    MODE_78_80,
    MODE_78_132,
    MODE_60_80,
    MODE_60_132,
};
static uint8_t mode_i;

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

static void print_banner(void) {
    uint16_t col = 0;
    uint16_t row = 0;
    const char *hz = scanout_mode_78() ? "78Hz" : "60Hz";
    unsigned cols = scanout_mode_132() ? 132u : 80u;

    tty_cursor(&col, &row);
    printf("crt-term digest=%s %s %ux26 cursor=%u,%u\n", TERM_IMAGE_ID, hz, cols, col, row);
}

static void paint_utf8(uint16_t row, const char *s) {
    uint16_t col = 0;
    const uint8_t *p = (const uint8_t *)s;

    while (*p && col < screen_cols() && row < screen_rows()) {
        uint32_t cp;
        uint8_t b = *p++;

        if (b < 0x80u) {
            cp = b;
        } else if ((b & 0xE0u) == 0xC0u && (p[0] & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(b & 0x1Fu) << 6) | (uint32_t)(p[0] & 0x3Fu);
            p += 1;
        } else {
            continue;
        }
        if (cp >= 0x20u && cp != 0x7Fu) {
            screen_put(col, row, (uint16_t)cp);
            col++;
        }
    }
}

/* Glass sample of the Appendix B cell. Cursor stays at 0,0 so a host
   line still lands on row 0. BOOTSEL redraws this in the next mode. */
static void paint_specimen(void) {
    char label[48];
    const char *hz = scanout_mode_78() ? "78Hz" : "60Hz";
    unsigned cols = scanout_mode_132() ? 132u : 80u;
    unsigned cw = scanout_mode_132() ? 9u : 10u;
    unsigned ch = scanout_mode_78() ? 13u : 16u;
    unsigned mh = scanout_mode_78() ? 10u : 12u;

    screen_clear();
    snprintf(label, sizeof(label), "%s %ucol  %ux%u cell  7x%u matrix", hz, cols, cw, ch, mh);
    paint_utf8(0, label);
    paint_utf8(2, "Hello café");
    paint_utf8(3, "mmm mmm mmm");
    paint_utf8(5, "abcdefghijklmnopqrstuvwxyz");
    paint_utf8(6, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    paint_utf8(7, "0123456789");
    paint_utf8(9, "éèêë áàóú ñç");
    tty_home();
}

static void show_mode(void) {
    scanout_set_mode(k_modes[mode_i]);
    paint_specimen();
    print_banner();
}

static void poll_bootsel(void) {
    static bool latched;
    static absolute_time_t ignore_until;

    if (!time_reached(ignore_until)) {
        return;
    }

    bool pressed = get_bootsel_button();
    if (pressed && !latched) {
        mode_i = (uint8_t)((mode_i + 1u) % (sizeof(k_modes) / sizeof(k_modes[0])));
        show_mode();
        latched = true;
        ignore_until = make_timeout_time_ms(BOOTSEL_DEBOUNCE_MS);
    } else if (!pressed && latched) {
        latched = false;
        ignore_until = make_timeout_time_ms(BOOTSEL_DEBOUNCE_MS);
    }
}

int main(void) {
    scanout_init(pio0);
    scanout_set_mode(MODE_78_80);
    stdio_init_all();

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    tty_init();
    paint_specimen();
    print_banner();
    host_spoke = false;

    absolute_time_t next_banner = make_timeout_time_ms(BANNER_MS);
    while (true) {
        int c = getchar_timeout_us(BOOTSEL_POLL_US);
        poll_bootsel();

        if (c >= 0) {
            host_spoke = true;
            if (c == '?') {
                print_banner();
            } else {
                tty_write_byte((uint8_t)c);
            }
        }

        if (!host_spoke && time_reached(next_banner)) {
            next_banner = make_timeout_time_ms(BANNER_MS);
#ifdef PICO_DEFAULT_LED_PIN
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
#endif
            print_banner();
        }
#ifdef PICO_DEFAULT_LED_PIN
        else if (host_spoke && time_reached(next_banner)) {
            next_banner = make_timeout_time_ms(BANNER_MS);
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
        }
#endif
    }
}
