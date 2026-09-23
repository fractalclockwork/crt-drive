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
/* Stop the state machines, reload the current mode's PIO programs, and
 * start them again. Same path as a mode change, without changing the mode. */
void scanout_reset_pio(void);
/* Instruction currently addressed by each scanout state machine. */
void scanout_sm_instr(uint16_t instr[3]);
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
/* Whole frame, including the vertical porch and sync. x may run into the
 * horizontal porch (scanout_signal_width) on 80-col. */
uint16_t scanout_signal_width(void);
uint16_t scanout_frame_lines(void);
/* Line the pixel DMA just started (0 .. frame_lines-1), including porch.
 * Hsync pushes at the start of the line; the kick channel then loads that
 * line and its read pointer advances. Active picture y is
 * beam - scanout_vbp() while beam is inside the active window. */
uint16_t scanout_beam_line(void);
void scanout_set_frame_pixel(uint16_t line, uint16_t x, PixelColor color);
/* Show frame_buffer[line] on that scan line, and clock the horizontal porch. */
void scanout_code_blanking(bool on);
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
