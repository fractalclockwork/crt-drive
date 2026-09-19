#include <stdio.h>
#include "pico/stdlib.h"
#include "hello_build_id.h"

int main(void) {
    stdio_init_all();

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    while (true) {
#ifdef PICO_DEFAULT_LED_PIN
        gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
#endif
        printf("crt-drive hello_pico ok digest=%s\n", HELLO_IMAGE_ID);

        int c = getchar_timeout_us(250 * 1000);
        if (c == 'p' || c == 'P') {
            printf("pong digest=%s\n", HELLO_IMAGE_ID);
        }
    }
}
