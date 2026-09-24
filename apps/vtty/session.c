#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

#include "catalog.h"
#include "../../font/font.h"
#include "proto.h"
#include "scanout.h"
#include "screen.h"
#include "session.h"
#include "tty.h"

static uint8_t payload[VTTY_MAX_PAYLOAD];
static char label_text[VTTY_MAX_PAYLOAD + 1];
static uint16_t need;
static uint16_t got;
static uint16_t discard;
static uint8_t msg_type;
static uint8_t phase;

static uint16_t rd16(uint16_t off) {
    return (uint16_t)payload[off] | ((uint16_t)payload[off + 1] << 8);
}

static uint32_t rd32(uint16_t off) {
    return (uint32_t)rd16(off) | ((uint32_t)rd16((uint16_t)(off + 2)) << 16);
}

static void send_ack(uint8_t status, uint16_t a, uint16_t b) {
    uint8_t buf[11];

    buf[0] = VTTY_SYNC0;
    buf[1] = VTTY_SYNC1;
    buf[2] = VTTY_ACK;
    buf[3] = 6;
    buf[4] = 0;
    buf[5] = msg_type;
    buf[6] = status;
    buf[7] = (uint8_t)a;
    buf[8] = (uint8_t)(a >> 8);
    buf[9] = (uint8_t)b;
    buf[10] = (uint8_t)(b >> 8);
    for (unsigned i = 0; i < sizeof(buf); i++) {
        putchar(buf[i]);
    }
    stdio_flush();
}

void session_reset(void) {
    phase = 0;
    got = 0;
    need = 0;
    discard = 0;
}

bool session_busy(void) {
    return phase != 0;
}

static void blank_cells(void) {
    uint16_t cols = screen_cols();
    uint16_t rows = screen_rows();

    for (uint16_t row = 0; row < rows; row++) {
        for (uint16_t col = 0; col < cols; col++) {
            screen_put(col, row, 0);
        }
    }
    tty_home();
}

static void dispatch(void) {
    uint16_t col = 0;
    uint16_t row = 0;

    switch (msg_type) {
    case VTTY_TEXT:
        tty_cursor_enable();
        for (uint16_t i = 0; i < need; i++) {
            tty_write_byte(payload[i]);
        }
        tty_cursor(&col, &row);
        send_ack(VTTY_OK, col, row);
        return;
    case VTTY_GOTO:
        if (need != 4) {
            send_ack(VTTY_ERR_LEN, 0, 0);
            return;
        }
        tty_goto(rd16(0), rd16(2));
        tty_cursor_enable();
        tty_cursor(&col, &row);
        send_ack(VTTY_OK, col, row);
        return;
    case VTTY_AREA: {
        if (need != 8) {
            send_ack(VTTY_ERR_LEN, 0, 0);
            return;
        }
        uint16_t cols = 0;
        uint16_t rows = 0;
        tty_cursor_disable();
        if (!screen_set_area(rd16(0), rd16(2), rd16(4), rd16(6), &cols, &rows)) {
            send_ack(VTTY_ERR_RANGE, 0, 0);
            return;
        }
        tty_home();
        tty_cursor_enable();
        send_ack(VTTY_OK, cols, rows);
        return;
    }
    case VTTY_CLEAR:
        if (need != 1) {
            send_ack(VTTY_ERR_LEN, 0, 0);
            return;
        }
        tty_cursor_disable();
        if (payload[0] == 0) {
            screen_clear();
            tty_home();
        } else if (payload[0] == 1) {
            blank_cells();
        } else if (payload[0] == 2) {
            scanout_clear(PIXEL_OFF);
            screen_redraw();
        } else {
            send_ack(VTTY_ERR_RANGE, 0, 0);
            return;
        }
        send_ack(VTTY_OK, 0, 0);
        return;
    case VTTY_SHOW: {
        if (need != 6) {
            send_ack(VTTY_ERR_LEN, 0, 0);
            return;
        }
        uint16_t bw = 0;
        uint16_t bh = 0;
        tty_cursor_disable();
        int st = catalog_show(rd16(0), rd16(2), rd16(4), &bw, &bh);
        if (st == VTTY_OK) {
            screen_redraw();
        }
        send_ack((uint8_t)st, bw, bh);
        return;
    }
    case VTTY_PUT: {
        if (need != 10) {
            send_ack(VTTY_ERR_LEN, 0, 0);
            return;
        }
        uint16_t id = rd16(0);
        int st = catalog_put_begin(id, rd16(2), rd16(4), rd32(6));
        send_ack((uint8_t)st, id, 0);
        return;
    }
    case VTTY_DATA: {
        if (need < 4) {
            send_ack(VTTY_ERR_LEN, 0, 0);
            return;
        }
        bool done = false;
        uint32_t off = rd32(0);
        int st = catalog_put_data(off, payload + 4, (uint32_t)need - 4u, &done);
        send_ack((uint8_t)st, (uint16_t)off, done ? 1u : 0u);
        return;
    }
    case VTTY_BLIT: {
        if (need < 6) {
            send_ack(VTTY_ERR_LEN, 0, 0);
            return;
        }
        tty_cursor_disable();
        int16_t x = (int16_t)rd16(0);
        int16_t y = (int16_t)rd16(2);
        uint16_t w = rd16(4);
        uint16_t stride = (uint16_t)(w / 4u);
        uint16_t nbytes = (uint16_t)(need - 6u);
        if (stride == 0 || (nbytes % stride) != 0) {
            send_ack(VTTY_ERR_LEN, 0, 0);
            return;
        }
        uint16_t rows = (uint16_t)(nbytes / stride);
        uint16_t drawn_w = 0;
        uint16_t drawn_h = 0;
        if (!catalog_blit(x, y, w, rows, payload + 6, &drawn_w, &drawn_h)) {
            send_ack(VTTY_ERR_RANGE, 0, 0);
            return;
        }
        send_ack(VTTY_OK, drawn_w, drawn_h);
        return;
    }
    case VTTY_FILL:
        if (need != 9) {
            send_ack(VTTY_ERR_LEN, 0, 0);
            return;
        }
        tty_cursor_disable();
        {
            uint16_t drawn_w = 0;
            uint16_t drawn_h = 0;
            if (!catalog_fill((int16_t)rd16(0), (int16_t)rd16(2), rd16(4), rd16(6), payload[8],
                              &drawn_w, &drawn_h)) {
                send_ack(VTTY_ERR_RANGE, 0, 0);
                return;
            }
            send_ack(VTTY_OK, drawn_w, drawn_h);
        }
        return;
    case VTTY_MODE: {
        if (need != 2) {
            send_ack(VTTY_ERR_LEN, 0, 0);
            return;
        }
        uint8_t hz = payload[0];
        uint8_t cols = payload[1];
        VideoMode mode;
        if (hz == 60 && cols == 80) {
            mode = MODE_60_80;
        } else if (hz == 60 && cols == 132) {
            mode = MODE_60_132;
        } else if (hz == 78 && cols == 80) {
            mode = MODE_78_80;
        } else if (hz == 78 && cols == 132) {
            mode = MODE_78_132;
        } else {
            send_ack(VTTY_ERR_RANGE, 0, 0);
            return;
        }
        tty_cursor_disable();
        scanout_set_mode(mode);
        send_ack(VTTY_OK, scanout_width(), scanout_height());
        return;
    }
    case VTTY_LABEL: {
        /* x, y baseline, px height, ink 0..3, flags. Bit 0 measures only.
         * Same placement as font_draw_utf8: y is the baseline. */
        if (need < 7) {
            send_ack(VTTY_ERR_LEN, 0, 0);
            return;
        }
        uint8_t color = payload[5];
        if (color > PIXEL_BOLD) {
            send_ack(VTTY_ERR_RANGE, 0, 0);
            return;
        }
        uint16_t ntext = (uint16_t)(need - 7u);
        memcpy(label_text, payload + 7, ntext);
        label_text[ntext] = 0;
        font_set_size((float)payload[4]);
        int x = (int16_t)rd16(0);
        int y = (int16_t)rd16(2);
        int width;
        if (payload[6] & 1u) {
            width = font_text_width(label_text);
        } else {
            tty_cursor_disable();
            width = font_draw_utf8(x, y, label_text, (PixelColor)color);
        }
        if (width < 0) {
            width = 0;
        }
        send_ack(VTTY_OK, (uint16_t)width, (uint16_t)font_cap_ascent());
        return;
    }
    case VTTY_CAPS:
        if (need != 0) {
            send_ack(VTTY_ERR_LEN, 0, 0);
            return;
        }
        send_ack(VTTY_OK, scanout_width(), scanout_height());
        return;
    default:
        send_ack(VTTY_ERR_STATE, 0, 0);
        return;
    }
}

void session_rx(uint8_t byte) {
    switch (phase) {
    case 0:
        if (byte == VTTY_SYNC0) {
            phase = 1;
        }
        break;
    case 1:
        if (byte == VTTY_SYNC1) {
            phase = 2;
        } else {
            phase = (byte == VTTY_SYNC0) ? 1u : 0u;
        }
        break;
    case 2:
        msg_type = byte;
        phase = 3;
        break;
    case 3:
        need = byte;
        phase = 4;
        break;
    case 4:
        need = (uint16_t)(need | ((uint16_t)byte << 8));
        got = 0;
        if (need > VTTY_MAX_PAYLOAD) {
            /* Eat the declared payload before the next sync. Dropping
             * it in place made the following bytes look like a command,
             * the same way an oversized picture wedged the session. */
            discard = need;
            phase = 6;
            break;
        }
        if (need == 0) {
            phase = 0;
            dispatch();
        } else {
            phase = 5;
        }
        break;
    case 5:
        payload[got++] = byte;
        if (got == need) {
            phase = 0;
            dispatch();
        }
        break;
    default:
        if (discard > 0) {
            discard--;
        }
        if (discard == 0) {
            phase = 0;
            send_ack(VTTY_ERR_LEN, 0, 0);
        }
        break;
    }
}
