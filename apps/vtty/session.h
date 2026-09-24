#ifndef CRT_VTTY_SESSION_H
#define CRT_VTTY_SESSION_H

#include <stdint.h>

void session_rx(uint8_t byte);
/* Drop a half-read frame so the next command can sync. */
void session_reset(void);
/* True while a frame is still being collected. */
bool session_busy(void);

#endif
