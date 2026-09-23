#ifndef CRT_TERM_TTY_H
#define CRT_TERM_TTY_H

#include <stdint.h>

void tty_init(void);
void tty_home(void);
void tty_write_byte(uint8_t b);
void tty_cursor(uint16_t *col, uint16_t *row);

#endif
