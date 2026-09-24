#include <string.h>

#include "indian_head.h"
#include "scanout.h"
#include "../font/font.h"
#include "../assets/indian_head/indian_head_pattern.h"

/* 4:3 card inside the pack. Side columns are the retrace stimulus, not the picture. */
#define IH_CARD_X      176
#define IH_CARD_Y      1
#define IH_CARD_W      448
#define IH_CARD_H      336
/* Vertical pitch / horizontal pitch on this MC5: (154 mm / 504) / (214 mm / 1968). */
#define IH_PITCH_NUM   281
#define IH_PITCH_DEN   100
#define IH_BURST_X     632
#define IH_BURST_W     160
#define IH_BURST_Y     56
/* Cap height of the standby line, in millimeters on the measured fill. */
#define STANDBY_CAP_MM 13.5f
#define STANDBY_TEXT   "PLEASE STAND BY"

/* crt_vtty replaces this with the stall watchdog. Pattern has no watchdog. */
extern void vtty_checkpoint(void);

static PixelColor ih_pixel(const uint8_t *row, uint16_t x) {
    uint8_t shift = (uint8_t)((3 - (x % 4)) * 2);
    return (PixelColor)((row[x / 4] >> shift) & 0b11);
}

static void pack_pixel(uint8_t *row, uint16_t x, PixelColor color) {
    uint16_t byte_idx = (uint16_t)((x / 4) ^ 3);
    uint8_t shift = (uint8_t)((3 - (x % 4)) * 2);

    row[byte_idx] &= (uint8_t)~(0b11 << shift);
    row[byte_idx] |= (uint8_t)((color & 0b11) << shift);
}

static float mm_to_px_x(float mm) {
    uint16_t fill_mm = scanout_mode_78() ? 226 : 225;
    return mm * (float)scanout_width() / (float)fill_mm;
}

static float mm_to_px_y(float mm) {
    uint16_t fill_mm = scanout_mode_78() ? 157 : 170;
    return mm * (float)scanout_height() / (float)fill_mm;
}

/* Probe at 32 px, then scale so capital H is `mm` tall on the glass. */
static void font_set_cap_mm(float mm) {
    font_set_size(32.0f);
    int cap = font_cap_height();
    if (cap < 1) {
        return;
    }
    font_set_size(32.0f * mm_to_px_y(mm) / (float)cap);
}

static void fill_rect(int x0, int y0, int x1, int y1, PixelColor color) {
    int w = scanout_width();
    int h = scanout_height();
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > w) {
        x1 = w;
    }
    if (y1 > h) {
        y1 = h;
    }
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            scanout_set_pixel((uint16_t)x, (uint16_t)y, color);
        }
    }
}

/* Capitals centered on the active raster, on a dark band so they read on the portrait. */
static void overlay_standby(void) {
    font_set_cap_mm(STANDBY_CAP_MM);
    int cap = font_cap_height();
    int ascent = font_cap_ascent();
    if (cap < 1) {
        return;
    }
    int tw = font_text_width(STANDBY_TEXT);
    int w = scanout_width();
    int h = scanout_height();
    int x = (w - tw) / 2;
    int top = (h - cap) / 2;
    int pad_x = (int)(mm_to_px_x(2.0f) + 0.5f);
    int pad_y = (int)(mm_to_px_y(1.5f) + 0.5f);
    fill_rect(x - pad_x, top - pad_y, x + tw + pad_x, top + cap + pad_y, PIXEL_OFF);
    font_draw_utf8(x, top + ascent, STANDBY_TEXT, PIXEL_BOLD);
}

/* Three levels in the /HSYNC window. A leak is vertical bars, not the card. */
static PixelColor retrace_bar(uint16_t x) {
    if (x < 80) {
        return PIXEL_DIM;
    }
    if (x < 96) {
        return PIXEL_OFF;
    }
    if (x < 176) {
        return PIXEL_NORMAL;
    }
    if (x < 192) {
        return PIXEL_OFF;
    }
    return PIXEL_BOLD;
}

void indian_head_standby(void) {
    uint16_t w = scanout_width();
    uint16_t h = scanout_height();
    uint32_t dst_w32 = (uint32_t)IH_CARD_W * h * IH_PITCH_NUM / (IH_CARD_H * IH_PITCH_DEN);
    uint16_t dst_h = h;
    uint16_t dst_w = dst_w32 > w ? w : (uint16_t)dst_w32;
    uint16_t ox = (uint16_t)((w - dst_w) / 2);

    scanout_clear(PIXEL_OFF);
    for (uint16_t y = 0; y < dst_h; y++) {
        vtty_checkpoint();
        uint16_t sy = (uint16_t)(IH_CARD_Y + (uint32_t)y * IH_CARD_H / dst_h);
        const uint8_t *src = indian_head_packed[sy];
        for (uint16_t x = 0; x < dst_w; x++) {
            uint16_t sx = (uint16_t)(IH_CARD_X + (uint32_t)x * IH_CARD_W / dst_w);
            scanout_set_pixel((uint16_t)(ox + x), y, ih_pixel(src, sx));
        }
    }
    overlay_standby();

    /* Outside the frame: vertical bars during /HSYNC, horizontal bursts on
     * the vertical porch and sync. Both stay dark if blanking holds. */
    uint8_t *hsync = scanout_retrace_hsync();
    memset(hsync, 0, SCANOUT_RETRACE_BYTES);
    for (uint16_t x = 0; x < SCANOUT_RETRACE_BYTES * 4; x++) {
        pack_pixel(hsync, x, retrace_bar(x));
    }
    uint8_t *vblank = scanout_retrace_vblank();
    memset(vblank, 0, scanout_store_bytes());
    const uint8_t *burst = indian_head_packed[IH_BURST_Y];
    for (uint16_t x = 0; x < w; x++) {
        uint16_t sx = (uint16_t)(IH_BURST_X + (x % IH_BURST_W));
        pack_pixel(vblank, x, ih_pixel(burst, sx));
    }
    scanout_retrace_test(true);
}
