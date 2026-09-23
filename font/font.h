#ifndef CRT_FONT_H
#define CRT_FONT_H

#include <stdint.h>
#include "scanout.h"

void font_init(void);
void font_set_size(float px_height);
int font_utf8_next(const char **s, uint32_t *cp);
int font_draw_codepoint(int x, int y, uint32_t cp, PixelColor color);
int font_draw_utf8(int x, int y, const char *utf8, PixelColor color);
int font_text_width(const char *utf8);
float font_line_height(void);
/* Capital H at the current size. Ascent is pixels from the top of H to the baseline. */
int font_cap_height(void);
int font_cap_ascent(void);

#endif
