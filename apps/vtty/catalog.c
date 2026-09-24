#include <string.h>
#include "hardware/flash.h"
#include "recover.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "builtin.h"
#include "catalog.h"
#include "proto.h"
#include "scanout.h"

/* Top 512 KB of the Pico flash. picotool writes the UF2's own sectors, so a
 * program that ends below this fence leaves the catalog in place. No
 * compaction: a slot that moves abandons its old span until a later PUT
 * lands in that hole. */
#define VTTY_PART_BYTES (512u * 1024u)
#define VTTY_DIR_BYTES  4096u
#define VTTY_SLOTS      16
#define VTTY_MAGIC      0x56545459u
#define VTTY_VERSION    1u

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#endif

typedef struct {
    uint16_t id;
    uint16_t w;
    uint16_t h;
    uint16_t flags;
    uint32_t off;
    uint32_t cap;
    uint32_t len;
} VttySlot;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t nused;
    VttySlot slots[VTTY_SLOTS];
} VttyCatalog;

extern uint8_t __flash_binary_end;

static VttyCatalog catalog;
static uint8_t page[4096] __attribute__((aligned(256)));

static bool put_open;
static int put_idx;
static uint16_t put_id;
static uint16_t put_w;
static uint16_t put_h;
static uint32_t put_off;
static uint32_t put_cap;
static uint32_t put_total;
static uint32_t put_got;

static uint32_t part_off(void) {
    return PICO_FLASH_SIZE_BYTES - VTTY_PART_BYTES;
}

static bool partition_ok(void) {
    return (uintptr_t)&__flash_binary_end <= XIP_BASE + part_off();
}

static void recount(void) {
    uint16_t n = 0;
    for (int i = 0; i < VTTY_SLOTS; i++) {
        if (catalog.slots[i].flags) {
            n++;
        }
    }
    catalog.magic = VTTY_MAGIC;
    catalog.version = VTTY_VERSION;
    catalog.nused = n;
}

/* XIP is off for the whole erase or program. USB's IRQ handler is in flash,
 * so an interrupt in that window hard-faults and CDC never comes back. */
static void flash_erase_at(uint32_t off, size_t count) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(off, count);
    restore_interrupts(ints);
}

static void flash_program_at(uint32_t off, const uint8_t *data, size_t count) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(off, data, count);
    restore_interrupts(ints);
}

static void catalog_store(void) {
    recount();
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &catalog, sizeof(catalog));
    flash_erase_at(part_off(), VTTY_DIR_BYTES);
    flash_program_at(part_off(), page, VTTY_DIR_BYTES);
}

static int find_id(uint16_t id) {
    for (int i = 0; i < VTTY_SLOTS; i++) {
        if (catalog.slots[i].flags && catalog.slots[i].id == id) {
            return i;
        }
    }
    return -1;
}

static int find_room(uint32_t need, int omit, uint32_t *off_out) {
    uint32_t off[VTTY_SLOTS];
    uint32_t cap[VTTY_SLOTS];
    int n = 0;

    for (int i = 0; i < VTTY_SLOTS; i++) {
        if (i == omit || !catalog.slots[i].flags) {
            continue;
        }
        uint32_t s = catalog.slots[i].off;
        uint32_t c = catalog.slots[i].cap;
        if (c == 0 || s < VTTY_DIR_BYTES || s + c > VTTY_PART_BYTES) {
            continue;
        }
        off[n] = s;
        cap[n] = c;
        n++;
    }
    for (int i = 1; i < n; i++) {
        uint32_t so = off[i];
        uint32_t sc = cap[i];
        int j = i;
        while (j > 0 && off[j - 1] > so) {
            off[j] = off[j - 1];
            cap[j] = cap[j - 1];
            j--;
        }
        off[j] = so;
        cap[j] = sc;
    }

    uint32_t cursor = VTTY_DIR_BYTES;
    for (int i = 0; i < n; i++) {
        if (off[i] > cursor && off[i] - cursor >= need) {
            *off_out = cursor;
            return VTTY_OK;
        }
        uint32_t end = off[i] + cap[i];
        if (end > cursor) {
            cursor = end;
        }
    }
    if (cursor < VTTY_PART_BYTES && VTTY_PART_BYTES - cursor >= need) {
        *off_out = cursor;
        return VTTY_OK;
    }
    return VTTY_ERR_FULL;
}

void catalog_init(void) {
    memset(&catalog, 0, sizeof(catalog));
    catalog.magic = VTTY_MAGIC;
    catalog.version = VTTY_VERSION;
    put_open = false;
    if (!partition_ok()) {
        return;
    }
    const VttyCatalog *rom = (const VttyCatalog *)(XIP_BASE + part_off());
    if (rom->magic == VTTY_MAGIC && rom->version == VTTY_VERSION) {
        memcpy(&catalog, rom, sizeof(catalog));
    }
}

uint16_t catalog_slots_used(void) {
    return catalog.nused;
}

/* Active pixels are scanout_store_bytes() per row, four per byte, and DMA
 * reads them as 32-bit little-endian words. x and w stay multiples of 4 so
 * a clip never splits a word (that painted bars). A rect may hang off any
 * edge; only the overlap is written. src_col/src_row skip the off-glass part. */
static bool clip_visible(int16_t x, int16_t y, uint16_t w, uint16_t h,
                         uint16_t *ox, uint16_t *oy, uint16_t *ow, uint16_t *oh,
                         uint16_t *src_col, uint16_t *src_row) {
    if ((w & 3u) || (x & 3) || w == 0 || h == 0) {
        return false;
    }

    int32_t x0 = x;
    int32_t y0 = y;
    int32_t x1 = x0 + (int32_t)w;
    int32_t y1 = y0 + (int32_t)h;
    int32_t sw = scanout_width();
    int32_t sh = scanout_height();
    int32_t cx0 = x0 < 0 ? 0 : x0;
    int32_t cy0 = y0 < 0 ? 0 : y0;
    int32_t cx1 = x1 > sw ? sw : x1;
    int32_t cy1 = y1 > sh ? sh : y1;
    if (cx0 >= cx1 || cy0 >= cy1) {
        *ox = 0;
        *oy = 0;
        *ow = 0;
        *oh = 0;
        *src_col = 0;
        *src_row = 0;
        return true;
    }

    uint16_t base = (uint16_t)(cx0 / 4);
    uint16_t store = scanout_store_bytes();
    if (base >= store) {
        return false;
    }
    uint16_t groups = (uint16_t)((cx1 - cx0) / 4);
    uint16_t room_g = (uint16_t)(store - base);
    if (groups > room_g) {
        groups = room_g;
    }
    if (groups == 0 || cy1 <= cy0) {
        *ox = 0;
        *oy = 0;
        *ow = 0;
        *oh = 0;
        *src_col = 0;
        *src_row = 0;
        return true;
    }

    *ox = (uint16_t)cx0;
    *oy = (uint16_t)cy0;
    *ow = (uint16_t)(groups * 4u);
    *oh = (uint16_t)(cy1 - cy0);
    *src_col = (uint16_t)((cx0 - x0) / 4);
    *src_row = (uint16_t)(cy0 - y0);
    return true;
}

bool catalog_blit(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint8_t *src,
                  uint16_t *drawn_w, uint16_t *drawn_h) {
    uint16_t ox, oy, ow, oh, src_col, src_row;

    if (src == NULL || !clip_visible(x, y, w, h, &ox, &oy, &ow, &oh, &src_col, &src_row)) {
        return false;
    }
    if (ow == 0 || oh == 0) {
        if (drawn_w) {
            *drawn_w = 0;
        }
        if (drawn_h) {
            *drawn_h = 0;
        }
        return true;
    }

    uint16_t stride = (uint16_t)(w / 4u);
    uint16_t groups = (uint16_t)(ow / 4u);
    uint16_t base = (uint16_t)(ox / 4u);

    for (uint16_t row = 0; row < oh; row++) {
        vtty_checkpoint();
        uint8_t *dst = scanout_row((uint16_t)(oy + row));
        const uint8_t *s = src + (uint32_t)(src_row + row) * stride + src_col;
        for (uint16_t i = 0; i < groups; i++) {
            dst[(base + i) ^ 3u] = s[i];
        }
    }
    if (drawn_w) {
        *drawn_w = ow;
    }
    if (drawn_h) {
        *drawn_h = oh;
    }
    return true;
}

bool catalog_fill(int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t color,
                  uint16_t *drawn_w, uint16_t *drawn_h) {
    uint16_t ox, oy, ow, oh, src_col, src_row;

    if (!clip_visible(x, y, w, h, &ox, &oy, &ow, &oh, &src_col, &src_row)) {
        return false;
    }
    if (ow == 0 || oh == 0) {
        if (drawn_w) {
            *drawn_w = 0;
        }
        if (drawn_h) {
            *drawn_h = 0;
        }
        return true;
    }

    uint8_t packed = (uint8_t)((color & 3u) * 0x55u);
    uint16_t groups = (uint16_t)(ow / 4u);
    uint16_t base = (uint16_t)(ox / 4u);

    for (uint16_t row = 0; row < oh; row++) {
        vtty_checkpoint();
        uint8_t *dst = scanout_row((uint16_t)(oy + row));
        for (uint16_t i = 0; i < groups; i++) {
            dst[(base + i) ^ 3u] = packed;
        }
    }
    if (drawn_w) {
        *drawn_w = ow;
    }
    if (drawn_h) {
        *drawn_h = oh;
    }
    return true;
}

int catalog_show(uint16_t id, uint16_t x, uint16_t y, uint16_t *bw, uint16_t *bh) {
    const uint8_t *src = NULL;
    uint16_t w = 0;
    uint16_t h = 0;
    int idx = find_id(id);

    if (idx >= 0) {
        w = catalog.slots[idx].w;
        h = catalog.slots[idx].h;
        src = (const uint8_t *)(XIP_BASE + part_off() + catalog.slots[idx].off);
    } else if (id == 0) {
        w = VTTY_BUILTIN_W;
        h = VTTY_BUILTIN_H;
        src = &vtty_builtin_bits[0][0];
    } else {
        return VTTY_ERR_RANGE;
    }

    if (bw) {
        *bw = w;
    }
    if (bh) {
        *bh = h;
    }
    if (!catalog_blit(x, y, w, h, src, NULL, NULL)) {
        return VTTY_ERR_RANGE;
    }
    return VTTY_OK;
}

static void flush_tail(void) {
    uint32_t into = put_got % 4096u;
    if (into == 0) {
        return;
    }
    uint32_t prog = (into + 255u) & ~255u;
    memset(page + into, 0x00, prog - into);
    flash_program_at(part_off() + put_off + (put_got - into), page, prog);
}

static void sink(const uint8_t *data, uint32_t n) {
    while (n > 0) {
        uint32_t into = put_got % 4096u;
        if (into == 0) {
            /* One sector, then ACK, so USB runs again before the next erase. */
            flash_erase_at(part_off() + put_off + put_got, 4096u);
        }
        uint32_t room = 4096u - into;
        uint32_t take = n < room ? n : room;
        memcpy(page + into, data, take);
        put_got += take;
        data += take;
        n -= take;
        if ((put_got % 4096u) == 0) {
            flash_program_at(part_off() + put_off + put_got - 4096u, page, 4096);
        }
    }
}

int catalog_put_begin(uint16_t id, uint16_t w, uint16_t h, uint32_t total) {
    if (!partition_ok()) {
        return VTTY_ERR_FLASH;
    }
    if (w == 0 || h == 0 || (w & 3u) || w > 2048u || h > 1024u) {
        return VTTY_ERR_RANGE;
    }
    uint32_t nbytes = (uint32_t)(w / 4u) * (uint32_t)h;
    if (total == 0 || total != nbytes) {
        return VTTY_ERR_LEN;
    }
    uint32_t cap = (total + 4095u) & ~4095u;
    if (cap > VTTY_PART_BYTES - VTTY_DIR_BYTES) {
        return VTTY_ERR_RANGE;
    }

    int idx = find_id(id);
    if (idx < 0) {
        for (int i = 0; i < VTTY_SLOTS; i++) {
            if (!catalog.slots[i].flags) {
                idx = i;
                break;
            }
        }
    }
    if (idx < 0) {
        return VTTY_ERR_FULL;
    }

    bool live = catalog.slots[idx].flags != 0;
    uint32_t off = 0;
    int room = find_room(cap, live ? idx : -1, &off);
    if (room != VTTY_OK) {
        return room;
    }

    if (live) {
        catalog.slots[idx].flags = 0;
        catalog_store();
    }

    put_open = true;
    put_idx = idx;
    put_id = id;
    put_w = w;
    put_h = h;
    put_off = off;
    put_cap = cap;
    put_total = total;
    put_got = 0;
    return VTTY_OK;
}

int catalog_put_data(uint32_t offset, const uint8_t *data, uint32_t n, bool *done) {
    if (done) {
        *done = false;
    }
    if (!put_open) {
        return VTTY_ERR_STATE;
    }
    if (offset != put_got || n > put_total - put_got) {
        return VTTY_ERR_STATE;
    }

    sink(data, n);
    if (put_got != put_total) {
        return VTTY_OK;
    }

    flush_tail();
    VttySlot *slot = &catalog.slots[put_idx];
    slot->id = put_id;
    slot->w = put_w;
    slot->h = put_h;
    slot->flags = 1;
    slot->off = put_off;
    slot->cap = put_cap;
    slot->len = put_total;
    catalog_store();
    put_open = false;
    if (done) {
        *done = true;
    }
    return VTTY_OK;
}
