#include <string.h>
#include "screen.h"
#include "font.h"

static TermCell cells[CHAR_ROWS][CHAR_COLS];

static void blit_cell(uint16_t col, uint16_t row, uint16_t cp) {
    uint16_t ox = (uint16_t)(col * CELL_WIDTH);
    uint16_t oy = (uint16_t)(row * CELL_HEIGHT);

    for (uint16_t y = 0; y < CELL_HEIGHT; y++) {
        for (uint16_t x = 0; x < CELL_WIDTH; x++) {
            set_pixel((uint16_t)(ox + x), (uint16_t)(oy + y), PIXEL_OFF);
        }
    }

    if (cp == 0) {
        return;
    }

    const uint8_t *glyph = font_glyph(cp);
    uint16_t gx = (uint16_t)(ox + GLYPH_ORIGIN_X);
    uint16_t gy = (uint16_t)(oy + GLYPH_ORIGIN_Y);
    for (uint16_t r = 0; r < GLYPH_HEIGHT; r++) {
        uint8_t bits = glyph[r];
        for (uint16_t c = 0; c < GLYPH_WIDTH; c++) {
            if (bits & (uint8_t)(1u << (GLYPH_WIDTH - 1 - c))) {
                set_pixel((uint16_t)(gx + c), (uint16_t)(gy + r), PIXEL_NORMAL);
            }
        }
    }
}

void screen_clear(void) {
    memset(cells, 0, sizeof(cells));
    clear_buffer(PIXEL_OFF);
}

void screen_init(void) {
    screen_clear();
}

void screen_put(uint16_t col, uint16_t row, uint16_t cp) {
    if (col >= CHAR_COLS || row >= CHAR_ROWS) {
        return;
    }
    cells[row][col].cp = cp;
    cells[row][col].attr = 0;
    cells[row][col].flags = 0;
    blit_cell(col, row, cp);
}

void screen_scroll_up(void) {
    memmove(&cells[0][0], &cells[1][0],
            sizeof(TermCell) * CHAR_COLS * (CHAR_ROWS - 1));
    memset(&cells[CHAR_ROWS - 1][0], 0, sizeof(TermCell) * CHAR_COLS);

    memmove(frame_buffer[0], frame_buffer[CELL_HEIGHT],
            (size_t)(FRAME_HEIGHT - CELL_HEIGHT) * LINE_STRIDE);
    for (uint16_t y = (uint16_t)(FRAME_HEIGHT - CELL_HEIGHT); y < FRAME_HEIGHT; y++) {
        fill_active_line(y, PIXEL_OFF);
    }
}
