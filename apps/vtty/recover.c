#include "recover.h"

#include "hardware/regs/usb.h"
#include "hardware/structs/usb.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

/* A draw that stops calling vtty_checkpoint for this long is stuck. */
/* One Noto glyph is rasterized from flash with no checkpoint inside
 * stbtt. Give that call room to finish; a real stall still reboots. */
#define STALL_US 4000000
/* Hub (Huasheng) misses a short reset. Hold the pull-up off this long. */
#define DISCONNECT_US 400000

static volatile absolute_time_t checkpoint;
static volatile bool recovering;

static void __not_in_flash_func(vtty_recover)(void) {
    if (recovering) {
        return;
    }
    recovering = true;
    hw_clear_bits(&usb_hw->sie_ctrl, USB_SIE_CTRL_PULLUP_EN_BITS);
    uint32_t t0 = time_us_32();
    while ((uint32_t)(time_us_32() - t0) < DISCONNECT_US) {
        watchdog_update();
    }
    watchdog_reboot(0, 0, 10);
    while (true) {
        tight_loop_contents();
    }
}

static int64_t __not_in_flash_func(stall_alarm)(alarm_id_t id, void *user) {
    (void)id;
    (void)user;
    if (!recovering &&
        absolute_time_diff_us(checkpoint, get_absolute_time()) > STALL_US) {
        vtty_recover();
    }
    return 50000;
}

void isr_hardfault(void) {
    vtty_recover();
}

void vtty_checkpoint(void) {
    checkpoint = get_absolute_time();
    watchdog_update();
}

void vtty_recover_init(void) {
    checkpoint = get_absolute_time();
    watchdog_enable(8000, true);
    add_alarm_in_us(50000, stall_alarm, NULL, true);
}
