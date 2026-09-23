#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "scanout.h"
#include "beam_build_id.h"

/* Dim field and a black band placed ahead of the beam. The band stays in
 * SRAM until a later sample shows the beam has passed it.
 *   comet   black band on the dim field
 *   behind  eight bold lines that track the beam above the band, dim gap between
 *   ahead   eight bold lines just below the band
 *   vblank  blanking paints bold once and then holds; vblank= is how many
 *           lines the last blank finished
 *   tear    bold lines follow the beam, including the bottom line, then hold
 * m/r applies one mode change after the keys pause and returns to comet,
 * so a repeat cannot leave a rewriting test on the glass. Boots 78 Hz 80-col. */

#define BANNER_MS           250
#define BOOTSEL_POLL_US     10000
#define BOOTSEL_DEBOUNCE_MS 50
#define BAR_LEAD            32
#define BAR_THICK           16
#define INK_DIM             0x55
#define INK_BOLD            0xFF
#define INK_BLACK           0x00
#define SHADE_LINES         8
#define MODE_SETTLE_MS      150
#define ACTIVE_MAX          523

typedef enum {
    TEST_COMET = 0,
    TEST_BEHIND,
    TEST_AHEAD,
    TEST_VBLANK,
    TEST_TEAR
} TestId;

static const char *const test_name[] = {
    "comet", "behind", "ahead", "vblank", "tear"
};

static TestId test_id = TEST_COMET;
static bool host_spoke;
static uint8_t line_ink[ACTIVE_MAX];
static int band_y = -1;
static int shade_y = -1;
static int seen_y = -10000;
static uint16_t tear_y;
static uint16_t vblank_fill_y;
static uint16_t vblank_n;
static uint16_t vblank_done;
static bool in_blank;

static const VideoMode k_modes[] = {
    MODE_60_80,
    MODE_60_132,
    MODE_78_80,
    MODE_78_132,
};
static uint8_t mode_i = 2; /* MODE_78_80 */
static VideoMode pending_mode = MODE_78_80;
static bool mode_arm;
static absolute_time_t mode_at;

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

static int active_y_of(uint16_t beam) {
    uint16_t vbp = scanout_vbp();
    uint16_t h = scanout_height();

    if (beam < vbp || beam >= (uint16_t)(vbp + h)) {
        return -1;
    }
    return (int)(beam - vbp);
}

static void poke(uint16_t y, uint8_t ink, bool remember) {
    uint16_t h = scanout_height();
    if (y >= h || y >= ACTIVE_MAX) {
        return;
    }
    memset(scanout_row(y), ink, scanout_store_bytes());
    if (remember) {
        line_ink[y] = ink;
    }
}

static void lay_base(void) {
    scanout_clear(PIXEL_DIM);
    memset(line_ink, INK_DIM, sizeof(line_ink));
}

static void reset_picture(void) {
    band_y = -1;
    shade_y = -1;
    seen_y = -10000;
    tear_y = 0;
    vblank_n = 0;
    vblank_done = 0;
    vblank_fill_y = 0;
    in_blank = false;
    lay_base();
}

static void restore_band(void) {
    int h = (int)scanout_height();

    if (band_y < 0) {
        return;
    }
    for (int i = 0; i < BAR_THICK; i++) {
        int row = band_y + i;
        if (row >= 0 && row < h) {
            poke((uint16_t)row, line_ink[row], false);
        }
    }
    band_y = -1;
}

static void paint_band(int y0) {
    int h = (int)scanout_height();

    for (int i = 0; i < BAR_THICK; i++) {
        int row = y0 + i;
        if (row >= 0 && row < h) {
            poke((uint16_t)row, INK_BLACK, false);
        }
    }
    band_y = y0;
}

static void print_status(void) {
    uint16_t line = scanout_beam_line();
    uint16_t then = line;
    absolute_time_t deadline = make_timeout_time_ms(40);

    do {
        then = scanout_beam_line();
    } while (then == line && !time_reached(deadline));

    uint16_t sm[3];
    scanout_sm_instr(sm);
    printf("crt-beam digest=%s %s %ux%u test=%s line=%u y=%d then=%u y2=%d vblank=%u sm=%04x,%04x,%04x\n",
           BEAM_IMAGE_ID,
           scanout_mode_78() ? "78Hz" : "60Hz",
           (unsigned)scanout_width(), (unsigned)scanout_height(),
           test_name[test_id],
           (unsigned)line, active_y_of(line),
           (unsigned)then, active_y_of(then),
           (unsigned)vblank_done,
           sm[0], sm[1], sm[2]);
}

static void select_test(TestId id) {
    test_id = id;
    reset_picture();
    print_status();
}

static void request_mode(VideoMode mode) {
    for (uint8_t i = 0; i < (uint8_t)(sizeof(k_modes) / sizeof(k_modes[0])); i++) {
        if (k_modes[i] == mode) {
            mode_i = i;
            break;
        }
    }
    pending_mode = mode;
    mode_arm = true;
    mode_at = make_timeout_time_ms(MODE_SETTLE_MS);
}

static void apply_mode_if_settled(void) {
    if (!mode_arm || !time_reached(mode_at)) {
        return;
    }
    mode_arm = false;
    test_id = TEST_COMET;
    if (pending_mode != scanout_mode()) {
        scanout_set_mode(pending_mode);
    }
    reset_picture();
    print_status();
}

static VideoMode toggle_col(VideoMode mode) {
    switch (mode) {
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

static VideoMode toggle_hz(VideoMode mode) {
    switch (mode) {
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

static void restore_shade(void) {
    int h = (int)scanout_height();

    if (shade_y <= -SHADE_LINES) {
        shade_y = -1;
        return;
    }
    for (int i = 0; i < SHADE_LINES; i++) {
        int row = shade_y + i;
        if (row >= 0 && row < h) {
            poke((uint16_t)row, line_ink[row], false);
        }
    }
    shade_y = -1;
}

static void paint_shade(int y0) {
    int h = (int)scanout_height();

    shade_y = y0;
    for (int i = 0; i < SHADE_LINES; i++) {
        int row = y0 + i;
        if (row >= 0 && row < h) {
            poke((uint16_t)row, INK_BOLD, false);
        }
    }
}

static void step_vblank(void) {
    uint16_t h = scanout_height();
    int y = active_y_of(scanout_beam_line());

    if (y >= 0) {
        if (in_blank) {
            vblank_done = vblank_n;
            in_blank = false;
        }
        return;
    }
    if (vblank_fill_y >= h) {
        return;
    }
    if (!in_blank) {
        in_blank = true;
        vblank_n = 0;
    }
    while (vblank_n < h && vblank_fill_y < h &&
           active_y_of(scanout_beam_line()) < 0) {
        poke(vblank_fill_y, INK_BOLD, true);
        vblank_fill_y++;
        vblank_n++;
    }
}

/* Sample the beam and park a black band where it has not arrived yet.
 * The previous band is put back only after a later sample shows the beam
 * has scanned past it. */
static void step_bar(void) {
    uint16_t beam = scanout_beam_line();
    int h = (int)scanout_height();
    int y = (int)beam - (int)scanout_vbp();
    int prev = seen_y;
    int next;
    bool wrapped;

    if (y < 0 || y >= h) {
        return;
    }
    seen_y = y;
    wrapped = prev >= 0 && y + 40 < prev;
    if (band_y >= 0 && (y > band_y + BAR_THICK || wrapped)) {
        restore_band();
    }
    next = y + BAR_LEAD;
    if (band_y < 0 && next >= 0 && next + BAR_THICK <= h) {
        paint_band(next);
    }
    /* Move the bold lines every sample. Parking them until the beam
     * arrives leaves a phosphor ghost at each stop, so the glass shows
     * several bars at once. */
    if (test_id == TEST_BEHIND || test_id == TEST_AHEAD) {
        int want = test_id == TEST_BEHIND
            ? next - SHADE_LINES - 4
            : next + BAR_THICK;
        if (wrapped || band_y < 0) {
            restore_shade();
        } else if (want != shade_y) {
            restore_shade();
            paint_shade(want);
        }
    }
}

static void step_tear(void) {
    uint16_t h = scanout_height();
    int y = active_y_of(scanout_beam_line());

    if (tear_y >= h) {
        return;
    }
    /* The beam index never goes past the last active line, so that line
     * is painted once the scan leaves the picture. The opening blank has
     * not seen an active line yet and must not fill the frame. */
    if (y < 0) {
        if (seen_y < 0) {
            return;
        }
        while (tear_y < h) {
            poke(tear_y, INK_BOLD, true);
            tear_y++;
        }
        return;
    }
    while ((int)tear_y < y) {
        poke(tear_y, INK_BOLD, true);
        tear_y++;
    }
}

static void step(void) {
    if (test_id == TEST_TEAR) {
        step_tear();
    } else if (test_id == TEST_VBLANK) {
        step_vblank();
    }
    step_bar();
}

static void print_help(void) {
    print_status();
    printf("keys: 1 comet  2 behind  3 ahead  4 vblank  5 tear\n");
    printf("  m 80/132  r 60/78  (applies when the keys pause)  p reset pio  ? status\n");
}

static void handle_key(int c) {
    host_spoke = true;
    switch (c) {
    case '1':
        select_test(TEST_COMET);
        break;
    case '2':
        select_test(TEST_BEHIND);
        break;
    case '3':
        select_test(TEST_AHEAD);
        break;
    case '4':
        select_test(TEST_VBLANK);
        break;
    case '5':
        select_test(TEST_TEAR);
        break;
    case 'm':
    case 'M':
        request_mode(toggle_col(pending_mode));
        break;
    case 'r':
    case 'R':
        request_mode(toggle_hz(pending_mode));
        break;
    case 'p':
    case 'P':
        scanout_reset_pio();
        print_status();
        sleep_ms(5);
        printf("later ");
        print_status();
        break;
    case '?':
        print_help();
        break;
    default:
        break;
    }
}

static void poll_bootsel(void) {
    static bool latched;
    static absolute_time_t ignore_until;
    static absolute_time_t next_poll;

    if (!time_reached(next_poll)) {
        return;
    }
    next_poll = make_timeout_time_us(BOOTSEL_POLL_US);
    if (!time_reached(ignore_until)) {
        return;
    }

    bool pressed = get_bootsel_button();
    if (pressed && !latched) {
        select_test((TestId)((test_id + 1u) % (sizeof(test_name) / sizeof(test_name[0]))));
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
    reset_picture();
    stdio_init_all();
    print_help();

    absolute_time_t next_banner = make_timeout_time_ms(BANNER_MS);
    while (true) {
        step();
        apply_mode_if_settled();
        int c = getchar_timeout_us(0);
        if (c >= 0) {
            handle_key(c);
        }
        poll_bootsel();
        if (!host_spoke && time_reached(next_banner)) {
            print_status();
            next_banner = make_timeout_time_ms(BANNER_MS);
        }
    }
}
