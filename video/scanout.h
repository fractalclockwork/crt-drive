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
uint8_t *scanout_row(uint16_t y);
uint16_t scanout_store_bytes(void);
uint8_t scanout_hpad(void);
uint8_t scanout_vbp(void);
uint8_t scanout_vfp(void);

void scanout_nudge_h(int delta_words);
void scanout_nudge_v(int delta_lines);
void scanout_reset_timing(void);
void scanout_print_status(const char *pat);

#endif
