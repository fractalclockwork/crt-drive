#ifndef CRT_TERM_FONT_H
#define CRT_TERM_FONT_H

#include <stdint.h>
#include "video.h"

/* 10 row bytes; bit 6 is the left column of the 7-wide matrix. */
const uint8_t *font_glyph(uint16_t cp);

#endif
