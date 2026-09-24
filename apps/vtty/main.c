#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/gpio.h"

#include "catalog.h"
#include "recover.h"
#include "../../font/font.h"
#include "indian_head.h"
#include "scanout.h"
#include "session.h"
#include "tty.h"
#include "vtty_build_id.h"

#define BANNER_MS 250

static bool host_spoke;
static bool link_up;
static absolute_time_t link_down_at;

static void print_banner(void) {
    const char *hz = scanout_mode_78() ? "78Hz" : "60Hz";

    printf("crt-vtty digest=%s %s %ux%u slots=%u\n",
           VTTY_IMAGE_ID, hz, scanout_width(), scanout_height(),
           catalog_slots_used());
}

/* Indian Head until a host paints, and again after that host closes the port. */
static void show_home(void) {
    session_reset();
    host_spoke = false;
    tty_init();
    indian_head_standby();
    link_down_at = nil_time;
}

int main(void) {
    /* Parse Noto before the state machines run. A flash walk while DMA
     * is feeding the beam stalls the pixel FIFO and leaves retrace lit. */
    font_init();
    scanout_init(pio0);
    scanout_set_mode(MODE_78_80);
    catalog_init();
    show_home();

    stdio_init_all();
    stdio_set_translate_crlf(&stdio_usb, false);

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    print_banner();
    absolute_time_t next_banner = make_timeout_time_ms(BANNER_MS);
    absolute_time_t last_rx = get_absolute_time();
    /* If the main loop or a draw stalls, drop USB long enough for the
     * hub to notice, then reboot. No cable pull. */
    vtty_recover_init();

    while (true) {
        vtty_checkpoint();
        tty_cursor_poll();
        /* A short wait on the first byte, then gather a burst. Timeout 0
         * is not used: on this SDK it can block instead of polling. */
        for (int n = 0; n < 128; n++) {
            int c = getchar_timeout_us(n == 0 ? 5000 : 1000);
            if (c < 0) {
                break;
            }
            if (!host_spoke) {
                scanout_retrace_test(false);
            }
            host_spoke = true;
            last_rx = get_absolute_time();
            session_rx((uint8_t)c);
        }

        if (session_busy() &&
            absolute_time_diff_us(last_rx, get_absolute_time()) > 200000) {
            session_reset();
        }

        /* DTR drop is the host letting go. Wait out a one-sample glitch. */
        bool up = stdio_usb_connected();
        if (up) {
            link_up = true;
            link_down_at = nil_time;
        } else if (link_up && host_spoke) {
            if (is_nil_time(link_down_at)) {
                link_down_at = get_absolute_time();
            } else if (absolute_time_diff_us(link_down_at, get_absolute_time()) > 300000) {
                show_home();
                link_up = false;
                next_banner = get_absolute_time();
            }
        } else {
            link_up = false;
            link_down_at = nil_time;
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
            next_banner = make_timeout_time_ms(500);
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
        }
#endif
    }
}
