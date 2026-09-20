#include "gfx.h"

#include <math.h>
#include <stdint.h>

static uint8_t fade_lut[256];
static int16_t sin_tab[256];

int16_t gfx_sin(uint8_t angle) {
    return sin_tab[angle];
}

int16_t gfx_cos(uint8_t angle) {
    return sin_tab[(uint8_t)(angle + 64u)];
}

void gfx_init(void) {
    for (int i = 0; i < 256; i++) {
        uint8_t out = 0;
        for (int p = 0; p < 4; p++) {
            unsigned shift = (unsigned)(6 - 2 * p);
            uint8_t pair = (uint8_t)((i >> shift) & 3);
            if (pair) {
                pair--;
            }
            out |= (uint8_t)(pair << shift);
        }
        fade_lut[i] = out;
        sin_tab[i] = (int16_t)(sinf((float)i * 6.2831853f / 256.0f) * 32767.0f);
    }
}

void gfx_fade_active(void) {
    for (uint16_t y = 0; y < FRAME_HEIGHT; y++) {
        uint8_t *line = frame_buffer[y];
        for (uint16_t i = 0; i < BYTES_PER_LINE; i++) {
            line[i] = fade_lut[line[i]];
        }
    }
}

void gfx_line(int x0, int y0, int x1, int y1, PixelColor color) {
    int dx = x1 - x0;
    if (dx < 0) {
        dx = -dx;
    }
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 - y0;
    if (dy < 0) {
        dy = -dy;
    }
    dy = -dy;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        if (x0 >= 0 && y0 >= 0) {
            set_pixel((uint16_t)x0, (uint16_t)y0, color);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_circle(int cx, int cy, int radius, PixelColor color) {
    if (radius < 0) {
        return;
    }
    int x = radius;
    int y = 0;
    int err = 1 - radius;

    while (x >= y) {
        set_pixel((uint16_t)(cx + x), (uint16_t)(cy + y), color);
        set_pixel((uint16_t)(cx + y), (uint16_t)(cy + x), color);
        set_pixel((uint16_t)(cx - y), (uint16_t)(cy + x), color);
        set_pixel((uint16_t)(cx - x), (uint16_t)(cy + y), color);
        set_pixel((uint16_t)(cx - x), (uint16_t)(cy - y), color);
        set_pixel((uint16_t)(cx - y), (uint16_t)(cy - x), color);
        set_pixel((uint16_t)(cx + y), (uint16_t)(cy - x), color);
        set_pixel((uint16_t)(cx + x), (uint16_t)(cy - y), color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}
