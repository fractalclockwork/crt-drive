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
void screen_scroll_up(void);
uint16_t screen_cols(void);
uint16_t screen_rows(void);

#endif
