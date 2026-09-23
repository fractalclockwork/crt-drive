#ifndef CRT_TERM_FONT_H
#define CRT_TERM_FONT_H

#include <stdint.h>

#define GLYPH_WIDTH     7
#define GLYPH_HEIGHT    10

/* 10 row bytes; bit 6 is the left column of the 7-wide matrix.
   78 Hz blits these rows. 60 Hz expands them to the 7×12 matrix. */
const uint8_t *font_glyph(uint16_t cp);

#endif
