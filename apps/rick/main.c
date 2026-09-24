#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "scanout.h"
#include "frames.h"
#include "rick_build_id.h"

/* Playback copies the next GIF frame from flash into a spare SRAM picture
 * while the beam is in the active raster. The spare is blitted into the
 * framebuffer only in the front porch, so the swap lands before the next
 * active line. Hold time is the GIF delay (RICK_DELAY_MS) in CRT scans. */

#define BANNER_MS           250
#define BOOTSEL_DEBOUNCE_MS 50
#define RICK_PIX_BYTES      ((RICK_LEFT + RICK_W) / 4)
#define PREFETCH_LINES      8
/* 128.4 MHz / 4, 1024-dot line. Same fH at 60 and 78. */
#define LINE_HZ_NUM         32100000ull
#define LINE_DOTS           1024ull

extern const uint8_t _binary_rick_frames_pack_start[];
extern const uint8_t _binary_rick_frames_pack_end[];

_Static_assert(RICK_STRIDE == SCANOUT_H_PAD_BYTES + RICK_DMA_BYTES, "row matches the DMA pad");
_Static_assert(RICK_PIX_BYTES + 4 == RICK_DMA_BYTES, "black tail");
_Static_assert(RICK_PIX_BYTES <= 200, "fits an 80-col line");

static uint8_t spare[RICK_H][RICK_PIX_BYTES];
static uint32_t shown;
static uint32_t spare_frame;
static uint16_t spare_y;
static uint16_t origin_y;
static uint32_t last_rewind;
static uint32_t elapsed;
static bool spare_ready;
static bool step;
static bool host_spoke;
static bool frames_ready;

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

static bool picture_lines_ok(void) {
    size_t n = (size_t)(_binary_rick_frames_pack_end - _binary_rick_frames_pack_start);
    return n == (size_t)RICK_STRIDE * RICK_H * RICK_FRAMES;
}

static const uint8_t *frame_bytes(uint32_t frame) {
    return _binary_rick_frames_pack_start + (size_t)frame * RICK_STRIDE * RICK_H;
}

static void place_origin(void) {
    uint16_t h = scanout_height();
    origin_y = (h > RICK_H) ? (uint16_t)((h - RICK_H) / 2u) : 0;
}

/* GIF delay expressed as CRT scans at the current frame rate. */
static uint32_t hold_scans(void) {
    uint32_t lines = scanout_frame_lines();
    uint64_t den = LINE_DOTS * 1000ull * lines;
    uint64_t num = (uint64_t)RICK_DELAY_MS * LINE_HZ_NUM + den / 2ull;
    uint32_t n = (uint32_t)(num / den);
    return n < 1u ? 1u : n;
}

static void blit_rows(const uint8_t *src) {
    uint16_t h = scanout_height();
    for (uint16_t y = 0; y < RICK_H; y++) {
        uint16_t row = (uint16_t)(origin_y + y);
        if (row >= h) {
            break;
        }
        memcpy(scanout_row(row), src + (size_t)y * RICK_PIX_BYTES, RICK_PIX_BYTES);
    }
}

static void show_frame(uint32_t frame) {
    const uint8_t *src;
    uint16_t h;
    uint16_t y;

    if (frame >= RICK_FRAMES || !frames_ready) {
        return;
    }
    src = frame_bytes(frame);
    h = scanout_height();
    scanout_clear(PIXEL_OFF);
    for (y = 0; y < RICK_H; y++) {
        uint16_t row = (uint16_t)(origin_y + y);
        if (row >= h) {
            break;
        }
        memcpy(scanout_row(row), src + (size_t)y * RICK_STRIDE + SCANOUT_H_PAD_BYTES,
               RICK_PIX_BYTES);
    }
}

static void arm_spare(uint32_t frame) {
    spare_frame = frame % RICK_FRAMES;
    spare_y = 0;
    spare_ready = false;
}

/* Front porch and sync: the next active line has not started.
 * Back porch is also blank, but the picture follows it immediately. */
static bool in_front_blank(void) {
    uint16_t beam = scanout_beam_line();
    uint16_t vbp = scanout_vbp();
    uint16_t h = scanout_height();

    if (beam < vbp) {
        return false;
    }
    return (uint16_t)(beam - vbp) >= h;
}

static void prefetch_lines(void) {
    const uint8_t *src;
    unsigned n;

    if (spare_ready || !frames_ready) {
        return;
    }
    src = frame_bytes(spare_frame) + SCANOUT_H_PAD_BYTES;
    n = 0;
    while (spare_y < RICK_H && n < PREFETCH_LINES) {
        memcpy(spare[spare_y], src + (size_t)spare_y * RICK_STRIDE, RICK_PIX_BYTES);
        spare_y++;
        n++;
    }
    if (spare_y >= RICK_H) {
        spare_ready = true;
    }
}

static void present_spare(void) {
    blit_rows(&spare[0][0]);
    shown = spare_frame;
    elapsed = 0;
    step = false;
    arm_spare(shown + 1u);
}

static void service_play(void) {
    uint32_t now = scanout_rewinds();
    uint32_t n = now - last_rewind;

    if (n != 0) {
        last_rewind = now;
        elapsed += n;
    }
    if (!spare_ready) {
        return;
    }
    if (!step && elapsed < hold_scans()) {
        return;
    }
    present_spare();
}

static void print_banner(void) {
    printf("crt-rick digest=%s %s 80col frame=%u/%u play delay=%ums hold=%u hpad=%u vbp=%u vfp=%u\n",
           RICK_IMAGE_ID,
           scanout_mode_78() ? "78Hz" : "60Hz",
           (unsigned)(shown + 1u), (unsigned)RICK_FRAMES,
           (unsigned)RICK_DELAY_MS,
           (unsigned)hold_scans(),
           (unsigned)scanout_hpad(),
           (unsigned)scanout_vbp(),
           (unsigned)scanout_vfp());
}

static void print_help(void) {
    print_banner();
    printf("crt-rick: plays at the gif delay, swaps in vertical blank\n");
    printf("  n next  r 60/78  a/d H 16px  A/D 64px  w/s V 1 line  W/S 5 lines\n");
    printf("  0 reset  BOOTSEL frame 1  ? status\n");
}

static void poll_bootsel(void) {
    static bool latched;
    static absolute_time_t ignore_until;

    if (!time_reached(ignore_until)) {
        return;
    }
    bool pressed = get_bootsel_button();
    if (pressed && !latched) {
        if (shown != 0) {
            arm_spare(0);
            step = true;
        }
        latched = true;
        ignore_until = make_timeout_time_ms(BOOTSEL_DEBOUNCE_MS);
    } else if (!pressed && latched) {
        latched = false;
        ignore_until = make_timeout_time_ms(BOOTSEL_DEBOUNCE_MS);
    }
}

static void handle_key(int c) {
    switch (c) {
    case 'n':
    case 'N':
        if (!spare_ready && spare_frame != (shown + 1u) % RICK_FRAMES) {
            arm_spare(shown + 1u);
        }
        step = true;
        break;
    case 'r':
    case 'R':
        scanout_set_mode(scanout_mode_78() ? MODE_60_80 : MODE_78_80);
        place_origin();
        show_frame(shown);
        arm_spare(shown + 1u);
        elapsed = 0;
        last_rewind = scanout_rewinds();
        print_banner();
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

int main(void) {
    scanout_init(pio0);
    frames_ready = picture_lines_ok();
    place_origin();
    show_frame(0);
    arm_spare(1);
    stdio_init_all();
    if (!frames_ready) {
        printf("crt-rick digest=%s frames missing\n", RICK_IMAGE_ID);
    }
    print_help();
    last_rewind = scanout_rewinds();

    absolute_time_t next_banner = make_timeout_time_ms(BANNER_MS);
    while (true) {
        if (in_front_blank()) {
            service_play();
        } else {
            prefetch_lines();
        }
        int c = getchar_timeout_us(0);
        if (in_front_blank()) {
            poll_bootsel();
        }
        if (c >= 0) {
            host_spoke = true;
            handle_key(c);
        }
        if (!host_spoke && time_reached(next_banner)) {
            print_banner();
            next_banner = make_timeout_time_ms(BANNER_MS);
        }
    }
}
