#ifndef CRT_VIDEO_H
#define CRT_VIDEO_H

#include <stdint.h>
#include "hardware/pio.h"

#define PIN_V0      0
#define PIN_V1      1
#define PIN_HSYNC   2
#define PIN_VSYNC   3

#define SM_PIXEL    0
#define SM_HSYNC    1
#define SM_VSYNC    2

#define CRT_SYS_CLK_KHZ 144000
#define PIO_CLKDIV      3.0f

#define FRAME_WIDTH     800
#define FRAME_HEIGHT    338
#define LINES_PER_FRAME 402
#define BYTES_PER_LINE  (FRAME_WIDTH / 4)   /* 200 */
#define LINE_STRIDE     204                 /* 200 + 4 trailing off pixels */
#define WORDS_PER_LINE  (LINE_STRIDE / 4)   /* 51 */

#define CELL_WIDTH      10
#define CELL_HEIGHT     13
#define GLYPH_WIDTH     7
#define GLYPH_HEIGHT    10
#define GLYPH_ORIGIN_X  1
#define GLYPH_ORIGIN_Y  1
#define CHAR_COLS       (FRAME_WIDTH / CELL_WIDTH)
#define CHAR_ROWS       (FRAME_HEIGHT / CELL_HEIGHT)

typedef enum {
    PIXEL_OFF    = 0b00,
    PIXEL_DIM    = 0b01,
    PIXEL_NORMAL = 0b10,
    PIXEL_BOLD   = 0b11
} PixelColor;

extern uint8_t frame_buffer[FRAME_HEIGHT][LINE_STRIDE];

uint8_t packed_color(PixelColor color);
void set_pixel(uint16_t x, uint16_t y, PixelColor color);
void clear_buffer(PixelColor color);
void fill_active_line(uint16_t y, PixelColor color);

void video_set_sys_clock(void);
void video_start(PIO pio);

#endif
