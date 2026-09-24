#ifndef CRT_VTTY_CATALOG_H
#define CRT_VTTY_CATALOG_H

#include <stdbool.h>
#include <stdint.h>

void catalog_init(void);
uint16_t catalog_slots_used(void);

/* Copy a catalog bitmap, or the linked card when id 0 is empty.
 * On success or a clipped-origin miss of a known bitmap, *bw and *bh
 * are that bitmap's size. */
int catalog_show(uint16_t id, uint16_t x, uint16_t y, uint16_t *bw, uint16_t *bh);

int catalog_put_begin(uint16_t id, uint16_t w, uint16_t h, uint32_t total);
int catalog_put_data(uint32_t offset, const uint8_t *data, uint32_t n, bool *done);

/* Draw the overlap of the rect with the active store. x and w are multiples
 * of 4 and may start off the left or top. *drawn_w and *drawn_h are the
 * pixels actually written (0 if the rect misses the glass). */
bool catalog_blit(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint8_t *src,
                  uint16_t *drawn_w, uint16_t *drawn_h);
bool catalog_fill(int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t color,
                  uint16_t *drawn_w, uint16_t *drawn_h);

#endif
