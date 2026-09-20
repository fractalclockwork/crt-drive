#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "video.h"
#include "scenes.h"
#include "demo_build_id.h"

#define BOOTSEL_POLL_US     10000
#define BOOTSEL_DEBOUNCE_MS 50
#define BANNER_MS           250
#define TICK_MS             25
#define ATTRACT_MS          12000

static SceneId current_scene = SCENE_STARFIELD;
static bool host_spoke;
static absolute_time_t next_attract;

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

static void print_banner(void) {
    printf("crt-demos digest=%s 78Hz scene=%s\n", DEMO_IMAGE_ID, scene_name(current_scene));
}

static void print_help(void) {
    printf("crt-demos 78Hz: BOOTSEL cycles  1/s starfield  2/r radar  3/l lissajous  4/x xor  5/w wireframe\n");
    print_banner();
}

static void apply_scene(SceneId id) {
    current_scene = id;
    scene_reset(id);
    next_attract = make_timeout_time_ms(ATTRACT_MS);
    print_banner();
}

static void cycle_scene(void) {
    apply_scene((SceneId)(((unsigned)current_scene + 1u) % SCENE_COUNT));
}

static void poll_bootsel(void) {
    static bool latched;
    static absolute_time_t ignore_until;

    if (!time_reached(ignore_until)) {
        return;
    }

    bool pressed = get_bootsel_button();
    if (pressed && !latched) {
        cycle_scene();
        latched = true;
        ignore_until = make_timeout_time_ms(BOOTSEL_DEBOUNCE_MS);
    } else if (!pressed && latched) {
        latched = false;
        ignore_until = make_timeout_time_ms(BOOTSEL_DEBOUNCE_MS);
    }
}

int main(void) {
    video_set_sys_clock();
    stdio_init_all();

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    scenes_init();
    apply_scene(SCENE_STARFIELD);
    video_start(pio0);
    print_help();

    absolute_time_t next_banner = make_timeout_time_ms(BANNER_MS);
    absolute_time_t next_tick = make_timeout_time_ms(TICK_MS);
    host_spoke = false;

    while (true) {
        int c = getchar_timeout_us(BOOTSEL_POLL_US);
        poll_bootsel();

        if (c >= 0) {
            host_spoke = true;
            switch (c) {
            case '1':
            case 's':
            case 'S':
                apply_scene(SCENE_STARFIELD);
                break;
            case '2':
            case 'r':
            case 'R':
                apply_scene(SCENE_RADAR);
                break;
            case '3':
            case 'l':
            case 'L':
                apply_scene(SCENE_LISSAJOUS);
                break;
            case '4':
            case 'x':
            case 'X':
                apply_scene(SCENE_XOR);
                break;
            case '5':
            case 'w':
            case 'W':
                apply_scene(SCENE_WIREFRAME);
                break;
            case 'a':
            case 'A':
                cycle_scene();
                break;
            case '?':
                print_help();
                break;
            default:
                break;
            }
        }

        if (time_reached(next_tick)) {
            next_tick = make_timeout_time_ms(TICK_MS);
            scene_tick(current_scene);
        }

        if (time_reached(next_attract)) {
            cycle_scene();
        }

        if (time_reached(next_banner)) {
            next_banner = make_timeout_time_ms(BANNER_MS);
#ifdef PICO_DEFAULT_LED_PIN
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
#endif
            print_banner();
        }
    }
}
