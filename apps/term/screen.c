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

/* Cursor addresses are inside this window. Zero size means "not set yet",
 * which reads as the whole grid. */
static uint16_t view_col;
static uint16_t view_row;
static uint16_t view_cols;
static uint16_t view_rows;

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

static uint16_t grid_cols(void) {
    uint16_t n = (uint16_t)(scanout_width() / cell_w());
    if (n > TERM_COLS) {
        n = TERM_COLS;
    }
    return n;
}

static uint16_t grid_rows(void) {
    uint16_t n = (uint16_t)(scanout_height() / cell_h());
    if (n > TERM_ROWS) {
        n = TERM_ROWS;
    }
    return n;
}

static void view_full(void) {
    view_col = 0;
    view_row = 0;
    view_cols = grid_cols();
    view_rows = grid_rows();
}

uint16_t screen_cols(void) {
    if (view_cols == 0) {
        return grid_cols();
    }
    return view_cols;
}

uint16_t screen_rows(void) {
    if (view_rows == 0) {
        return grid_rows();
    }
    return view_rows;
}

int screen_set_area(uint16_t col, uint16_t row, uint16_t cols, uint16_t rows,
                    uint16_t *out_cols, uint16_t *out_rows) {
    uint16_t gc = grid_cols();
    uint16_t gr = grid_rows();
    if (cols == 0 || rows == 0) {
        view_full();
    } else if (col >= gc || row >= gr) {
        return 0;
    } else {
        if (cols > (uint16_t)(gc - col)) {
            cols = (uint16_t)(gc - col);
        }
        if (rows > (uint16_t)(gr - row)) {
            rows = (uint16_t)(gr - row);
        }
        if (cols == 0 || rows == 0) {
            return 0;
        }
        view_col = col;
        view_row = row;
        view_cols = cols;
        view_rows = rows;
    }
    for (uint16_t r = 0; r < view_rows; r++) {
        memset(&cells[view_row + r][view_col], 0, sizeof(TermCell) * view_cols);
    }
    if (out_cols) {
        *out_cols = view_cols;
    }
    if (out_rows) {
        *out_rows = view_rows;
    }
    return 1;
}

static void blit_cell(uint16_t col, uint16_t row, uint16_t cp) {
    uint16_t cw = cell_w();
    uint16_t ch = cell_h();
    uint16_t mh = matrix_h();
    uint16_t ox = (uint16_t)((view_col + col) * cw);
    uint16_t oy = (uint16_t)((view_row + row) * ch);

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
    view_full();
    screen_clear();
}

void screen_refresh_cell(uint16_t col, uint16_t row) {
    if (col >= screen_cols() || row >= screen_rows()) {
        return;
    }
    blit_cell(col, row, cells[view_row + row][view_col + col].cp);
}

void screen_cursor_underscore(uint16_t col, uint16_t row) {
    if (col >= screen_cols() || row >= screen_rows()) {
        return;
    }
    /* Same stroke as the font's '_': the bottom row of the matrix, normal ink. */
    uint16_t cw = cell_w();
    uint16_t ch = cell_h();
    uint16_t ox = (uint16_t)((view_col + col) * cw);
    uint16_t oy = (uint16_t)((view_row + row) * ch);
    uint16_t y = (uint16_t)(oy + GLYPH_ORIGIN_Y + matrix_h() - 1u);
    uint16_t x0 = (uint16_t)(ox + GLYPH_ORIGIN_X);
    if (y >= oy + ch) {
        y = (uint16_t)(oy + ch - 1u);
    }
    for (uint16_t x = 0; x < GLYPH_WIDTH; x++) {
        scanout_set_pixel((uint16_t)(x0 + x), y, PIXEL_NORMAL);
    }
}

void screen_put(uint16_t col, uint16_t row, uint16_t cp) {
    if (col >= screen_cols() || row >= screen_rows()) {
        return;
    }
    uint16_t ac = (uint16_t)(view_col + col);
    uint16_t ar = (uint16_t)(view_row + row);
    cells[ar][ac].cp = cp;
    cells[ar][ac].attr = 0;
    cells[ar][ac].flags = 0;
    blit_cell(col, row, cp);
}

void screen_redraw(void) {
    uint16_t cols = screen_cols();
    uint16_t rows = screen_rows();

    for (uint16_t row = 0; row < rows; row++) {
        for (uint16_t col = 0; col < cols; col++) {
            uint16_t cp = cells[view_row + row][view_col + col].cp;
            if (cp != 0) {
                blit_cell(col, row, cp);
            }
        }
    }
}

static void redraw_view(void) {
    uint16_t cols = screen_cols();
    uint16_t rows = screen_rows();
    for (uint16_t row = 0; row < rows; row++) {
        for (uint16_t col = 0; col < cols; col++) {
            blit_cell(col, row, cells[view_row + row][view_col + col].cp);
        }
    }
}

void screen_scroll_up(void) {
    uint16_t rows = screen_rows();
    uint16_t cols = screen_cols();
    if (rows < 2 || cols == 0) {
        return;
    }
    for (uint16_t row = 0; row < (uint16_t)(rows - 1u); row++) {
        memmove(&cells[view_row + row][view_col],
                &cells[view_row + row + 1u][view_col],
                sizeof(TermCell) * cols);
    }
    memset(&cells[view_row + rows - 1u][view_col], 0, sizeof(TermCell) * cols);
    if (view_col == 0 && cols == grid_cols()) {
        scanout_scroll_band((uint16_t)(view_row * cell_h()),
                            (uint16_t)(rows * cell_h()), cell_h());
    } else {
        redraw_view();
    }
}
