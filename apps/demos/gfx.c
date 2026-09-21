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
    uint16_t height = scanout_height();
    uint16_t store = scanout_store_bytes();
    for (uint16_t y = 0; y < height; y++) {
        uint8_t *line = scanout_row(y);
        for (uint16_t i = 0; i < store; i++) {
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
            scanout_set_pixel((uint16_t)x0, (uint16_t)y0, color);
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
        scanout_set_pixel((uint16_t)(cx + x), (uint16_t)(cy + y), color);
        scanout_set_pixel((uint16_t)(cx + y), (uint16_t)(cy + x), color);
        scanout_set_pixel((uint16_t)(cx - y), (uint16_t)(cy + x), color);
        scanout_set_pixel((uint16_t)(cx - x), (uint16_t)(cy + y), color);
        scanout_set_pixel((uint16_t)(cx - x), (uint16_t)(cy - y), color);
        scanout_set_pixel((uint16_t)(cx - y), (uint16_t)(cy - x), color);
        scanout_set_pixel((uint16_t)(cx + y), (uint16_t)(cy - x), color);
        scanout_set_pixel((uint16_t)(cx + x), (uint16_t)(cy - y), color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void gfx_ellipse(int cx, int cy, int rx, int ry, PixelColor color) {
    if (rx < 0) {
        rx = -rx;
    }
    if (ry < 0) {
        ry = -ry;
    }
    if (rx == 0) {
        gfx_line(cx, cy - ry, cx, cy + ry, color);
        return;
    }
    if (ry == 0) {
        gfx_line(cx - rx, cy, cx + rx, cy, color);
        return;
    }

    int x = -rx;
    int y = 0;
    int64_t a2 = (int64_t)rx * rx;
    int64_t b2 = (int64_t)ry * ry;
    int64_t err = (int64_t)x * (2 * b2 + x) + b2;
    do {
        scanout_set_pixel((uint16_t)(cx - x), (uint16_t)(cy + y), color);
        scanout_set_pixel((uint16_t)(cx + x), (uint16_t)(cy + y), color);
        scanout_set_pixel((uint16_t)(cx + x), (uint16_t)(cy - y), color);
        scanout_set_pixel((uint16_t)(cx - x), (uint16_t)(cy - y), color);
        int64_t e2 = err * 2;
        if (e2 >= (2 * (int64_t)x + 1) * b2) {
            x++;
            err += (2 * (int64_t)x + 1) * b2;
        }
        if (e2 <= (2 * (int64_t)y + 1) * a2) {
            y++;
            err += (2 * (int64_t)y + 1) * a2;
        }
    } while (x <= 0);
}
