#ifndef CRT_DEMOS_GFX_H
#define CRT_DEMOS_GFX_H

#include "scanout.h"

int16_t gfx_sin(uint8_t angle);
int16_t gfx_cos(uint8_t angle);

void gfx_init(void);
void gfx_fade_active(void);
void gfx_line(int x0, int y0, int x1, int y1, PixelColor color);
void gfx_circle(int cx, int cy, int radius, PixelColor color);
void gfx_ellipse(int cx, int cy, int rx, int ry, PixelColor color);

#endif
