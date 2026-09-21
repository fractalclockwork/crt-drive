#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "scanout.h"
#include "scenes.h"
#include "demo_build_id.h"

#define BOOTSEL_POLL_US     10000
#define BOOTSEL_DEBOUNCE_MS 50
#define BANNER_MS           250
#define TICK_MS             25

static SceneId current_scene = SCENE_STARFIELD;

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
    printf("crt-demos digest=%s %s %s scene=%s hpad=%u vbp=%u vfp=%u vsize78=%u\n",
           DEMO_IMAGE_ID,
           scanout_mode_78() ? "78Hz" : "60Hz",
           scanout_mode_132() ? "132col" : "80col",
           scene_name(current_scene),
           (unsigned)scanout_hpad(),
           (unsigned)scanout_vbp(),
           (unsigned)scanout_vfp(),
           scanout_mode_78() ? 1u : 0u);
}

static void print_help(void) {
    printf("crt-demos: BOOTSEL/n cycles  1 starfield  2 radar  3/l lissajous  4/x xor  5 wireframe\n");
    printf("  m 80/132  r 60/78  a/d H 16px  A/D 64px  w/s V 1 line  W/S 5 lines  0 reset  ? help\n");
    print_banner();
}

static void apply_scene(SceneId id) {
    current_scene = id;
    scene_reset(id);
    print_banner();
}

static void cycle_scene(void) {
    apply_scene((SceneId)(((unsigned)current_scene + 1u) % SCENE_COUNT));
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
    apply_scene(current_scene);
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
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    scanout_init(pio0);
    stdio_init_all();
    scenes_init();
    apply_scene(SCENE_STARFIELD);
    print_help();

    absolute_time_t next_banner = make_timeout_time_ms(BANNER_MS);
    absolute_time_t next_tick = make_timeout_time_ms(TICK_MS);

    while (true) {
        int c = getchar_timeout_us(BOOTSEL_POLL_US);
        poll_bootsel();

        if (c >= 0) {
            switch (c) {
            case '1':
                apply_scene(SCENE_STARFIELD);
                break;
            case '2':
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
                apply_scene(SCENE_WIREFRAME);
                break;
            case 'n':
            case 'N':
                cycle_scene();
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
                print_banner();
                break;
            case 'd':
                scanout_nudge_h(1);
                print_banner();
                break;
            case 'A':
                scanout_nudge_h(-4);
                print_banner();
                break;
            case 'D':
                scanout_nudge_h(4);
                print_banner();
                break;
            case 'w':
                scanout_nudge_v(-1);
                print_banner();
                break;
            case 's':
                scanout_nudge_v(1);
                print_banner();
                break;
            case 'W':
                scanout_nudge_v(-5);
                print_banner();
                break;
            case 'S':
                scanout_nudge_v(5);
                print_banner();
                break;
            case '0':
                scanout_reset_timing();
                print_banner();
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

        if (time_reached(next_banner)) {
            next_banner = make_timeout_time_ms(BANNER_MS);
#ifdef PICO_DEFAULT_LED_PIN
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
#endif
            print_banner();
        }
    }
}
