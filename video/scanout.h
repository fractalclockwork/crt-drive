#ifndef CRT_SCANOUT_H
#define CRT_SCANOUT_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/pio.h"

#define PIN_V0      0
#define PIN_V1      1
#define PIN_HSYNC   2
#define PIN_VSYNC   3
#define PIN_VSIZE78 4                   /* high in 78 Hz: U6 pin 5 / VR301 select */

typedef enum {
    PIXEL_OFF    = 0b00,
    PIXEL_DIM    = 0b01,
    PIXEL_NORMAL = 0b10,
    PIXEL_BOLD   = 0b11
} PixelColor;

typedef enum {
    MODE_60_80 = 0,
    MODE_60_132,
    MODE_78_80,
    MODE_78_132
} VideoMode;

void scanout_init(PIO pio);
void scanout_set_mode(VideoMode mode);
void scanout_enable(bool on);

VideoMode scanout_mode(void);
bool scanout_mode_78(void);
bool scanout_mode_132(void);
uint16_t scanout_width(void);
uint16_t scanout_height(void);
uint16_t scanout_cell_w(void);
uint16_t scanout_cell_h(void);
uint16_t scanout_char_w(void);
uint16_t scanout_char_h(void);

void scanout_set_pixel(uint16_t x, uint16_t y, PixelColor color);
void scanout_clear(PixelColor color);
void scanout_fill_line(uint16_t y, PixelColor color);
void scanout_scroll(uint16_t lines);
uint8_t *scanout_row(uint16_t y);
uint16_t scanout_store_bytes(void);
uint8_t scanout_hpad(void);
uint8_t scanout_vbp(void);
uint8_t scanout_vfp(void);

/* 272 dots clocked inside the 289-dot /HSYNC. Packed like the framebuffer. */
#define SCANOUT_RETRACE_BYTES 68

uint8_t *scanout_retrace_hsync(void);
uint8_t *scanout_retrace_vblank(void);
void scanout_retrace_test(bool on);

/* Dots clocked after the stored line, before /HSYNC. Packed like the framebuffer. */
uint8_t *scanout_margin_right(void);
uint16_t scanout_margin_right_dots(void);
/* Porch lines (not sync) show scanout_retrace_vblank(). The active frame stays put. */
void scanout_margin_blips(bool on);

void scanout_nudge_h(int delta_words);
void scanout_nudge_v(int delta_lines);
void scanout_reset_timing(void);
void scanout_print_status(const char *pat);

#endif
