#include "font.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* crt_vtty replaces this with the stall watchdog. Pattern and demos link the
 * same file and have no watchdog, so the default is a no-op. */
__attribute__((weak)) void vtty_checkpoint(void) {}

#define STBTT_STATIC
#define STBTT_assert(x) ((void)0)
/* RP2040 has no FPU. The default float rasterizer does not finish a glyph
 * before the stall watchdog. Version 1 fills each scanline in fixed point. */
#define STBTT_RASTERIZER_VERSION 1
#define STBTT_SCANLINE_CHECKPOINT() vtty_checkpoint()
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define GLYPH_MAX_W 96
#define GLYPH_MAX_H 72

extern const uint8_t font_noto_sans_regular[];
extern const unsigned int font_noto_sans_regular_len;

static stbtt_fontinfo font_info;
static bool font_ready;
static float size_px = 16.0f;
static float x_scale = 1.0f;
static float y_scale = 1.0f;
static int ascent;
static int descent;
static int line_gap;
static uint8_t glyph_bits[GLYPH_MAX_H * GLYPH_MAX_W];

/* The requested em height is a whole number of pixels. Horizontal scale stays
 * the glass stretch, so a stem is not forced onto a second integer grid. */
static int snap_px(float px) {
    int n = (int)(px + 0.5f);
    if (n < 1) {
        n = 1;
    }
    return n;
}

static void font_update_scale(void) {
    uint32_t fill_w = scanout_mode_78() ? 226u : 225u;
    uint32_t fill_h = scanout_mode_78() ? 157u : 170u;
    uint16_t w = scanout_width();
    uint16_t h = scanout_height();
    int y_px = snap_px(size_px);
    y_scale = stbtt_ScaleForPixelHeight(&font_info, (float)y_px);
    if (h == 0 || fill_w == 0) {
        x_scale = y_scale;
        return;
    }
    x_scale = y_scale * ((float)w * (float)fill_h) / ((float)h * (float)fill_w);
}

void font_init(void) {
    if (font_noto_sans_regular_len < 256u) {
        font_ready = false;
        return;
    }
    font_ready = stbtt_InitFont(&font_info, font_noto_sans_regular, 0) != 0;
    if (!font_ready) {
        return;
    }
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);
    font_update_scale();
}

void font_set_size(float px_height) {
    int px = snap_px(px_height);
    if (px < 6) {
        px = 6;
    }
    size_px = (float)px;
    if (font_ready) {
        font_update_scale();
    }
}

int font_utf8_next(const char **s, uint32_t *cp) {
    const unsigned char *p = (const unsigned char *)*s;
    if (p == NULL || *p == 0) {
        return 0;
    }

    unsigned char b0 = p[0];
    if (b0 < 0x80u) {
        *cp = b0;
        *s = (const char *)(p + 1);
        return 1;
    }
    if ((b0 & 0xE0u) == 0xC0u && p[1] != 0) {
        *cp = ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)(p[1] & 0x3Fu);
        *s = (const char *)(p + 2);
        return 1;
    }
    if ((b0 & 0xF0u) == 0xE0u && p[1] != 0 && p[2] != 0) {
        *cp = ((uint32_t)(b0 & 0x0Fu) << 12)
            | ((uint32_t)(p[1] & 0x3Fu) << 6)
            | (uint32_t)(p[2] & 0x3Fu);
        *s = (const char *)(p + 3);
        return 1;
    }
    if ((b0 & 0xF8u) == 0xF0u && p[1] != 0 && p[2] != 0 && p[3] != 0) {
        *cp = ((uint32_t)(b0 & 0x07u) << 18)
            | ((uint32_t)(p[1] & 0x3Fu) << 12)
            | ((uint32_t)(p[2] & 0x3Fu) << 6)
            | (uint32_t)(p[3] & 0x3Fu);
        *s = (const char *)(p + 4);
        return 1;
    }
    *cp = 0xFFFDu;
    *s = (const char *)(p + 1);
    return 1;
}

static int glyph_advance(int glyph) {
    int adv = 0;
    int lsb = 0;
    stbtt_GetGlyphHMetrics(&font_info, glyph, &adv, &lsb);
    return (int)(adv * x_scale + 0.5f);
}

/* Text is normal or bold. A dim request is drawn normal. Coverage under the
 * cutoff is off; everything else is the one requested ink. */
static PixelColor coverage_color(uint8_t a, PixelColor want) {
    if (want == PIXEL_OFF || a < 32u) {
        return PIXEL_OFF;
    }
    if (want == PIXEL_BOLD) {
        return PIXEL_BOLD;
    }
    return PIXEL_NORMAL;
}

int font_draw_codepoint(int x, int y, uint32_t cp, PixelColor color) {
    if (!font_ready) {
        return 0;
    }
    font_update_scale();

    int glyph = stbtt_FindGlyphIndex(&font_info, (int)cp);
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBoxSubpixel(&font_info, glyph, x_scale, y_scale, 0.0f, 0.0f,
                                    &x0, &y0, &x1, &y1);
    int gw = x1 - x0;
    int gh = y1 - y0;
    int adv = glyph_advance(glyph);

    if (gw <= 0 || gh <= 0) {
        return adv;
    }
    if (gw > GLYPH_MAX_W || gh > GLYPH_MAX_H) {
        return adv;
    }

    memset(glyph_bits, 0, (size_t)gw * (size_t)gh);
    vtty_checkpoint();
    stbtt_MakeGlyphBitmapSubpixel(&font_info, glyph_bits, gw, gh, gw,
                                  x_scale, y_scale, 0.0f, 0.0f, glyph);
    vtty_checkpoint();

    for (int row = 0; row < gh; row++) {
        vtty_checkpoint();
        int py = y + y0 + row;
        if (py < 0) {
            continue;
        }
        for (int col = 0; col < gw; col++) {
            int px = x + x0 + col;
            if (px < 0) {
                continue;
            }
            PixelColor ink = coverage_color(glyph_bits[row * gw + col], color);
            if (ink != PIXEL_OFF) {
                scanout_set_pixel((uint16_t)px, (uint16_t)py, ink);
            }
        }
    }
    return adv;
}

int font_draw_utf8(int x, int y, const char *utf8, PixelColor color) {
    int pen = x;
    uint32_t cp;
    const char *p = utf8;
    while (font_utf8_next(&p, &cp)) {
        if (cp == '\n' || cp == '\r') {
            continue;
        }
        pen += font_draw_codepoint(pen, y, cp, color);
    }
    return pen - x;
}

int font_text_width(const char *utf8) {
    if (!font_ready) {
        return 0;
    }
    font_update_scale();
    int width = 0;
    uint32_t cp;
    const char *p = utf8;
    while (font_utf8_next(&p, &cp)) {
        if (cp == '\n' || cp == '\r') {
            continue;
        }
        int glyph = stbtt_FindGlyphIndex(&font_info, (int)cp);
        width += glyph_advance(glyph);
    }
    return width;
}

float font_line_height(void) {
    if (!font_ready) {
        return size_px;
    }
    font_update_scale();
    return (float)(ascent - descent + line_gap) * y_scale;
}

static int font_h_box(int *y0, int *y1) {
    int x0;
    int x1;
    if (!font_ready) {
        return 0;
    }
    font_update_scale();
    stbtt_GetCodepointBitmapBoxSubpixel(&font_info, 'H', x_scale, y_scale, 0.0f, 0.0f,
                                        &x0, y0, &x1, y1);
    (void)x0;
    (void)x1;
    return 1;
}

int font_cap_height(void) {
    int y0;
    int y1;
    if (!font_h_box(&y0, &y1)) {
        return 0;
    }
    return y1 - y0;
}

int font_cap_ascent(void) {
    int y0;
    int y1;
    if (!font_h_box(&y0, &y1)) {
        return 0;
    }
    return -y0;
}
