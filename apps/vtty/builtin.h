#ifndef CRT_VTTY_BUILTIN_H
#define CRT_VTTY_BUILTIN_H

#include <stdint.h>

#define VTTY_BUILTIN_W 128
#define VTTY_BUILTIN_H 64

/* Linear 2 bpp. Bits 7-6 of each byte are the leftmost pixel. */
extern const uint8_t vtty_builtin_bits[VTTY_BUILTIN_H][VTTY_BUILTIN_W / 4];

#endif
