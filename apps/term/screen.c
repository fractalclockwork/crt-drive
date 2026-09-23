#include <string.h>
#include "screen.h"
#include "font.h"
#include "scanout.h"

/* Appendix B (WY-120 maintenance manual 880491-01):
     60 Hz 80-col  cell 10×16  matrix 7×12
     60 Hz 132-col cell  9×16  matrix 7×12
     78 Hz 80-col  cell 10×13  matrix 7×10
     78 Hz 132-col cell  9×13  matrix 7×10
   Ink origin is (1, 1): 1 px left and top. 80-col keeps 2 px on the right
   of the 7-wide matrix; 132-col keeps 1 px. 78 Hz rows occupy 26×13 = 338
   of the 416-line raster. */
#define TERM_COLS       132
#define TERM_ROWS       26
#define GLYPH_ORIGIN_X  1
#define GLYPH_ORIGIN_Y  1

static TermCell cells[TERM_ROWS][TERM_COLS];

static uint16_t cell_w(void) {
    return scanout_mode_132() ? 9u : 10u;
}

static uint16_t cell_h(void) {
    return scanout_mode_78() ? 13u : 16u;
}

static uint16_t matrix_h(void) {
    return scanout_mode_78() ? 10u : 12u;
}

/* 60 Hz matrix is 12 rows. Repeat body rows 4 and 8 of the 10-row artwork. */
static uint8_t matrix_bits(const uint8_t *glyph, uint16_t row, uint16_t rows) {
    static const uint8_t map12[12] = {0, 1, 2, 3, 4, 4, 5, 6, 7, 8, 8, 9};

    if (rows == GLYPH_HEIGHT) {
        return glyph[row];
    }
    return glyph[map12[row]];
}

uint16_t screen_cols(void) {
    uint16_t n = (uint16_t)(scanout_width() / cell_w());
    if (n > TERM_COLS) {
        n = TERM_COLS;
    }
    return n;
}

uint16_t screen_rows(void) {
    uint16_t n = (uint16_t)(scanout_height() / cell_h());
    if (n > TERM_ROWS) {
        n = TERM_ROWS;
    }
    return n;
}

static void blit_cell(uint16_t col, uint16_t row, uint16_t cp) {
    uint16_t cw = cell_w();
    uint16_t ch = cell_h();
    uint16_t mh = matrix_h();
    uint16_t ox = (uint16_t)(col * cw);
    uint16_t oy = (uint16_t)(row * ch);

    for (uint16_t y = 0; y < ch; y++) {
        for (uint16_t x = 0; x < cw; x++) {
            scanout_set_pixel((uint16_t)(ox + x), (uint16_t)(oy + y), PIXEL_OFF);
        }
    }

    if (cp == 0) {
        return;
    }

    const uint8_t *glyph = font_glyph(cp);
    uint16_t gx = (uint16_t)(ox + GLYPH_ORIGIN_X);
    uint16_t gy = (uint16_t)(oy + GLYPH_ORIGIN_Y);
    for (uint16_t r = 0; r < mh; r++) {
        uint8_t bits = matrix_bits(glyph, r, mh);
        for (uint16_t c = 0; c < GLYPH_WIDTH; c++) {
            if (bits & (uint8_t)(1u << (GLYPH_WIDTH - 1 - c))) {
                scanout_set_pixel((uint16_t)(gx + c), (uint16_t)(gy + r), PIXEL_NORMAL);
            }
        }
    }
}

void screen_clear(void) {
    memset(cells, 0, sizeof(cells));
    scanout_clear(PIXEL_OFF);
}

void screen_init(void) {
    screen_clear();
}

void screen_put(uint16_t col, uint16_t row, uint16_t cp) {
    if (col >= screen_cols() || row >= screen_rows()) {
        return;
    }
    cells[row][col].cp = cp;
    cells[row][col].attr = 0;
    cells[row][col].flags = 0;
    blit_cell(col, row, cp);
}

void screen_scroll_up(void) {
    uint16_t rows = screen_rows();
    if (rows < 2) {
        return;
    }
    memmove(&cells[0][0], &cells[1][0], sizeof(TermCell) * TERM_COLS * (rows - 1));
    memset(&cells[rows - 1][0], 0, sizeof(TermCell) * TERM_COLS);
    scanout_scroll(cell_h());
}
