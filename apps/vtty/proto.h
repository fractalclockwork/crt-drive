#ifndef CRT_VTTY_PROTO_H
#define CRT_VTTY_PROTO_H

#include <stdint.h>

/* Host → Pico, then one ACK. Sync A5 5A, type u8, len u16 LE, payload.
 * Pixels are linear 2 bpp, four per byte, bits 7-6 the leftmost, width
 * a multiple of 4. The Pico applies the scanout byte swap when it draws. */

#define VTTY_SYNC0       0xA5u
#define VTTY_SYNC1       0x5Au
#define VTTY_MAX_PAYLOAD 1024u

#define VTTY_TEXT        0x01u
#define VTTY_GOTO        0x02u
#define VTTY_CLEAR       0x03u
#define VTTY_SHOW        0x04u
#define VTTY_PUT         0x05u
#define VTTY_DATA        0x06u
#define VTTY_BLIT        0x07u
#define VTTY_FILL        0x08u
#define VTTY_MODE        0x09u
#define VTTY_CAPS        0x0Au
#define VTTY_LABEL       0x0Bu
#define VTTY_AREA        0x0Cu
#define VTTY_ACK         0x81u

#define VTTY_OK          0u
#define VTTY_ERR_LEN     1u
#define VTTY_ERR_STATE   2u
#define VTTY_ERR_RANGE   3u
#define VTTY_ERR_FLASH   4u
#define VTTY_ERR_FULL    5u

#endif
