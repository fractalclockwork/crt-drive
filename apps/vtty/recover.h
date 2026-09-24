#ifndef CRT_VTTY_RECOVER_H
#define CRT_VTTY_RECOVER_H

/* Call from the main loop and from any long draw. If calls stop, the
 * Pico drops the USB pull-up long enough for the hub to see an unplug,
 * then reboots to the boot card. A bare watchdog reset is too short for
 * this hub, which is why the cable had to come out. */
void vtty_recover_init(void);
void vtty_checkpoint(void);

#endif
