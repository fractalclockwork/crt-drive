#ifndef CRT_TERM_SCREEN_H
#define CRT_TERM_SCREEN_H

#include <stdint.h>

typedef struct {
    uint16_t cp;   /* BMP codepoint; 0 = blank */
    uint8_t attr;  /* reserved */
    uint8_t flags;
} TermCell;

void screen_init(void);
void screen_clear(void);
void screen_put(uint16_t col, uint16_t row, uint16_t cp);
/* Restore one cell from the stored codepoint, and paint the cursor underscore. */
void screen_refresh_cell(uint16_t col, uint16_t row);
void screen_cursor_underscore(uint16_t col, uint16_t row);
/* Draw cells that hold a codepoint. Blank cells stay as they are, so a
 * picture under the page is left alone. */
void screen_redraw(void);
void screen_scroll_up(void);
/* Cell window for put, cursor, and scroll. cols or rows of 0 is the full grid.
 * The scroll band is only this window, so a header above it stays put.
 * *out_cols and *out_rows are the size actually used. Returns 0 if the origin
 * is off the grid. */
int screen_set_area(uint16_t col, uint16_t row, uint16_t cols, uint16_t rows,
                    uint16_t *out_cols, uint16_t *out_rows);
uint16_t screen_cols(void);
uint16_t screen_rows(void);

#endif
