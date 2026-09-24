#include "pico/stdlib.h"

#include "tty.h"
#include "screen.h"

static uint16_t cur_col;
static uint16_t cur_row;
static bool cursor_live;
static bool cursor_phase;
static bool cursor_shown;
static absolute_time_t cursor_next;

/* A glass TTY underscore, on and off for half a second each. */
#define CURSOR_BLINK_MS 500

static void cursor_park(void) {
    if (!cursor_shown) {
        return;
    }
    screen_refresh_cell(cur_col, cur_row);
    cursor_shown = false;
}

static void cursor_wake(void) {
    if (!cursor_live || !cursor_phase || cursor_shown) {
        return;
    }
    screen_cursor_underscore(cur_col, cur_row);
    cursor_shown = true;
}
static uint32_t utf8_cp;
static uint8_t utf8_need;
static uint8_t utf8_got;

static void utf8_reset(void) {
    utf8_cp = 0;
    utf8_need = 0;
    utf8_got = 0;
}

static void tty_lf(void) {
    cur_row++;
    if (cur_row >= screen_rows()) {
        screen_scroll_up();
        cur_row = (uint16_t)(screen_rows() - 1);
    }
}

static void tty_put_cp(uint16_t cp) {
    if (cp < 0x20u || (cp >= 0x7Fu && cp < 0xA0u)) {
        return;
    }
    cursor_park();
    screen_put(cur_col, cur_row, cp);
    cur_col++;
    if (cur_col >= screen_cols()) {
        cur_col = 0;
        tty_lf();
    }
    cursor_wake();
}

static void tty_control(uint8_t b) {
    cursor_park();
    switch (b) {
    case 0x07: /* BEL */
        break;
    case 0x08: /* BS */
        if (cur_col > 0) {
            cur_col--;
        }
        break;
    case 0x09: /* TAB */
        cur_col = (uint16_t)((cur_col + 8u) & ~7u);
        if (cur_col >= screen_cols()) {
            cur_col = 0;
            tty_lf();
        }
        break;
    case 0x0A: /* LF */
        tty_lf();
        break;
    case 0x0D: /* CR */
        cur_col = 0;
        break;
    default:
        break;
    }
    cursor_wake();
}

void tty_init(void) {
    utf8_reset();
    cur_col = 0;
    cur_row = 0;
    cursor_live = false;
    cursor_phase = false;
    cursor_shown = false;
    screen_init();
}

void tty_home(void) {
    tty_goto(0, 0);
}

void tty_goto(uint16_t col, uint16_t row) {
    utf8_reset();
    cursor_park();
    if (col >= screen_cols()) {
        col = 0;
    }
    if (row >= screen_rows()) {
        row = (uint16_t)(screen_rows() - 1u);
    }
    cur_col = col;
    cur_row = row;
    cursor_wake();
}

void tty_cursor_enable(void) {
    if (cursor_live) {
        cursor_wake();
        return;
    }
    cursor_live = true;
    cursor_phase = true;
    cursor_next = make_timeout_time_ms(CURSOR_BLINK_MS);
    cursor_wake();
}

void tty_cursor_disable(void) {
    cursor_park();
    cursor_live = false;
    cursor_phase = false;
}

void tty_cursor_poll(void) {
    if (!cursor_live || !time_reached(cursor_next)) {
        return;
    }
    cursor_next = make_timeout_time_ms(CURSOR_BLINK_MS);
    cursor_phase = !cursor_phase;
    if (cursor_phase) {
        cursor_wake();
    } else {
        cursor_park();
    }
}

void tty_cursor(uint16_t *col, uint16_t *row) {
    if (col) {
        *col = cur_col;
    }
    if (row) {
        *row = cur_row;
    }
}

void tty_write_byte(uint8_t b) {
    if (utf8_need) {
        if ((b & 0xC0u) != 0x80u) {
            utf8_reset();
        } else {
            utf8_cp = (utf8_cp << 6) | (uint32_t)(b & 0x3Fu);
            utf8_got++;
            if (utf8_got == utf8_need) {
                uint32_t cp = utf8_cp;
                utf8_reset();
                if (cp <= 0xFFFFu) {
                    tty_put_cp((uint16_t)cp);
                }
            }
            return;
        }
    }

    if (b < 0x20u || b == 0x7Fu) {
        tty_control(b);
        return;
    }
    if (b < 0x80u) {
        tty_put_cp(b);
        return;
    }

    if ((b & 0xE0u) == 0xC0u) {
        utf8_need = 2;
        utf8_got = 1;
        utf8_cp = (uint32_t)(b & 0x1Fu);
        return;
    }
    if ((b & 0xF0u) == 0xE0u) {
        utf8_need = 3;
        utf8_got = 1;
        utf8_cp = (uint32_t)(b & 0x0Fu);
        return;
    }
    if ((b & 0xF8u) == 0xF0u) {
        utf8_need = 4;
        utf8_got = 1;
        utf8_cp = (uint32_t)(b & 0x07u);
        return;
    }
}
