#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "video.h"
#include "tty.h"
#include "term_build_id.h"

#define BANNER_MS 250

static bool host_spoke;

static void print_banner(void) {
    uint16_t col = 0;
    uint16_t row = 0;
    tty_cursor(&col, &row);
    printf("crt-term digest=%s 78Hz 80x26 cursor=%u,%u\n", TERM_IMAGE_ID, col, row);
}

int main(void) {
    video_set_sys_clock();
    stdio_init_all();

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    tty_init();
    video_start(pio0);
    print_banner();
    host_spoke = false;

    absolute_time_t next_banner = make_timeout_time_ms(BANNER_MS);
    while (true) {
        int c = getchar_timeout_us(host_spoke ? 10000 : 250000);

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
