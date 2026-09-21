#include "scenes.h"

#include "gfx.h"
#include "scanout.h"

#include <stdint.h>

#define STAR_COUNT      96
#define CONTACT_COUNT   8
#define CUBE_ZOFF       420
#define CUBE_FOCAL      260

typedef struct {
    int16_t x;
    int16_t y;
    uint16_t z;
} Star;

typedef struct {
    uint8_t angle;
    uint8_t radius;
} Contact;

static Star stars[STAR_COUNT];
static uint32_t rng_state = 0xC0FFEEu;
static uint8_t fade_div;
static uint8_t radar_sweep;
static int radar_rx;
static int radar_ry;
static Contact contacts[CONTACT_COUNT];
static uint16_t lissa_t;
static uint8_t xor_phase;
static uint8_t cube_yaw;
static uint8_t cube_pitch;

static uint32_t rng(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static int scene_cx(void) {
    return (int)(scanout_width() / 2);
}

static int scene_cy(void) {
    return (int)(scanout_height() / 2);
}

static void radar_fit(int *rx, int *ry) {
    /* HIL fill from pattern: 225×170 mm @ 60 Hz, 226×157 mm @ 78 Hz. */
    uint32_t fill_w = scanout_mode_78() ? 226u : 225u;
    uint32_t fill_h = scanout_mode_78() ? 157u : 170u;
    int max_rx = scene_cx() - 9;
    int max_ry = scene_cy() - 9;
    if (max_rx < 32) {
        max_rx = 32;
    }
    if (max_ry < 32) {
        max_ry = 32;
    }
    uint32_t num = (uint32_t)scanout_width() * fill_h;
    uint32_t den = (uint32_t)scanout_height() * fill_w;
    int try_ry = max_ry;
    int try_rx = (int)(((uint32_t)try_ry * num + den / 2u) / den);
    if (try_rx > max_rx) {
        try_rx = max_rx;
        try_ry = (int)(((uint32_t)try_rx * den + num / 2u) / num);
    }
    if (try_rx < 32) {
        try_rx = 32;
    }
    if (try_ry < 32) {
        try_ry = 32;
    }
    *rx = try_rx;
    *ry = try_ry;
}

static void spawn_star(Star *s) {
    uint16_t w = scanout_width();
    uint16_t h = scanout_height();
    s->x = (int16_t)((rng() % (uint32_t)(w + 100u)) - (int)(w / 2 + 50));
    s->y = (int16_t)((rng() % (uint32_t)(h + 80u)) - (int)(h / 2 + 40));
    s->z = (uint16_t)(180u + (rng() % 700u));
}

static void reset_starfield(void) {
    for (int i = 0; i < STAR_COUNT; i++) {
        spawn_star(&stars[i]);
        stars[i].z = (uint16_t)(40u + (rng() % 840u));
    }
}

static void tick_starfield(void) {
    if ((fade_div++ & 1u) == 0) {
        gfx_fade_active();
    }
    int cx = scene_cx();
    int cy = scene_cy();
    uint16_t w = scanout_width();
    uint16_t h = scanout_height();
    for (int i = 0; i < STAR_COUNT; i++) {
        Star *s = &stars[i];
        if (s->z < 12) {
            spawn_star(s);
        } else {
            s->z = (uint16_t)(s->z - 7u);
        }
        int32_t z = s->z;
        if (z < 8) {
            continue;
        }
        int32_t sx = cx + (s->x * 220) / z;
        int32_t sy = cy + (s->y * 220) / z;
        PixelColor c = PIXEL_DIM;
        if (s->z < 50) {
            c = PIXEL_BOLD;
        } else if (s->z < 140) {
            c = PIXEL_NORMAL;
        }
        if (sx >= 0 && sy >= 0 && sx < w && sy < h) {
            scanout_set_pixel((uint16_t)sx, (uint16_t)sy, c);
            if (c == PIXEL_BOLD && sx + 1 < w) {
                scanout_set_pixel((uint16_t)(sx + 1), (uint16_t)sy, PIXEL_NORMAL);
            }
        }
    }
}

static void polar_to_xy(uint8_t angle, int radius, int *x, int *y) {
    int32_t s = gfx_sin(angle);
    int32_t c = gfx_cos(angle);
    int ry = radar_ry > 0 ? radar_ry : 1;
    *x = scene_cx() + (int)(((int64_t)radar_rx * radius * s / ry) >> 15);
    *y = scene_cy() - (int)(((int64_t)radius * c) >> 15);
}

static void reset_radar(void) {
    radar_sweep = 0;
    radar_fit(&radar_rx, &radar_ry);
    uint32_t span = (uint32_t)(radar_ry - 36);
    if (span < 1) {
        span = 1;
    }
    for (int i = 0; i < CONTACT_COUNT; i++) {
        contacts[i].angle = (uint8_t)(rng() & 255u);
        contacts[i].radius = (uint8_t)(28u + (rng() % span));
    }
}

static void tick_radar(void) {
    if ((fade_div++ & 1u) == 0) {
        gfx_fade_active();
    }

    int cx = scene_cx();
    int cy = scene_cy();
    gfx_ellipse(cx, cy, radar_rx, radar_ry, PIXEL_NORMAL);
    gfx_ellipse(cx, cy, radar_rx * 3 / 4, radar_ry * 3 / 4, PIXEL_DIM);
    gfx_ellipse(cx, cy, radar_rx / 2, radar_ry / 2, PIXEL_DIM);
    gfx_ellipse(cx, cy, radar_rx / 4, radar_ry / 4, PIXEL_DIM);
    gfx_line(cx - radar_rx, cy, cx + radar_rx, cy, PIXEL_DIM);
    gfx_line(cx, cy - radar_ry, cx, cy + radar_ry, PIXEL_DIM);

    int x1, y1;
    polar_to_xy(radar_sweep, radar_ry, &x1, &y1);
    gfx_line(cx, cy, x1, y1, PIXEL_BOLD);
    polar_to_xy((uint8_t)(radar_sweep + 1u), radar_ry, &x1, &y1);
    gfx_line(cx, cy, x1, y1, PIXEL_NORMAL);

    for (int i = 0; i < CONTACT_COUNT; i++) {
        uint8_t d = (uint8_t)(radar_sweep - contacts[i].angle);
        if (d < 6u) {
            int px, py;
            polar_to_xy(contacts[i].angle, contacts[i].radius, &px, &py);
            PixelColor c = (d < 2u) ? PIXEL_BOLD : PIXEL_NORMAL;
            scanout_set_pixel((uint16_t)px, (uint16_t)py, c);
            scanout_set_pixel((uint16_t)(px + 1), (uint16_t)py, c);
            scanout_set_pixel((uint16_t)px, (uint16_t)(py + 1), PIXEL_NORMAL);
            scanout_set_pixel((uint16_t)(px - 1), (uint16_t)py, PIXEL_DIM);
        }
        if ((fade_div & 15u) == 0) {
            contacts[i].angle++;
        }
    }

    radar_sweep = (uint8_t)(radar_sweep + 2u);
}

static void reset_lissajous(void) {
    lissa_t = 0;
}

static void tick_lissajous(void) {
    if ((fade_div++ & 1u) == 0) {
        gfx_fade_active();
    }
    int rx = scene_cx() - 40;
    int ry = scene_cy() - 19;
    if (rx < 40) {
        rx = 40;
    }
    if (ry < 24) {
        ry = 24;
    }
    for (int i = 0; i < 12; i++) {
        uint8_t a = (uint8_t)((lissa_t + (uint16_t)(i * 3)) & 255u);
        uint8_t b = (uint8_t)(((lissa_t * 2u) / 3u + (uint16_t)(i * 2)) & 255u);
        int x = scene_cx() + (int)((rx * (int32_t)gfx_sin(a)) >> 15);
        int y = scene_cy() + (int)((ry * (int32_t)gfx_sin(b)) >> 15);
        scanout_set_pixel((uint16_t)x, (uint16_t)y, PIXEL_BOLD);
        scanout_set_pixel((uint16_t)(x + 1), (uint16_t)y, PIXEL_NORMAL);
    }
    lissa_t = (uint16_t)((lissa_t + 4u) & 255u);
}

static void reset_xor(void) {
    xor_phase = 0;
}

static void tick_xor(void) {
    uint8_t phase = xor_phase;
    uint16_t height = scanout_height();
    uint16_t store = scanout_store_bytes();
    for (uint16_t y = 0; y < height; y++) {
        uint8_t *line = scanout_row(y);
        for (uint16_t bx = 0; bx < store; bx++) {
            uint16_t x = (uint16_t)((bx ^ 3u) * 4u);
            uint8_t packed = 0;
            for (uint8_t i = 0; i < 4; i++) {
                uint8_t v = (uint8_t)(((x + i) ^ y) + phase);
                uint8_t c = (uint8_t)((v >> 6) & 3u);
                packed |= (uint8_t)(c << (6 - 2 * i));
            }
            line[bx] = packed;
        }
    }
    xor_phase = (uint8_t)(xor_phase + 3u);
}

static void rotate_vertex(int16_t x, int16_t y, int16_t z,
                          int16_t *ox, int16_t *oy, int16_t *oz) {
    int32_t cy = gfx_cos(cube_yaw);
    int32_t sy = gfx_sin(cube_yaw);
    int32_t x1 = (x * cy - z * sy) >> 15;
    int32_t z1 = (x * sy + z * cy) >> 15;

    int32_t cp = gfx_cos(cube_pitch);
    int32_t sp = gfx_sin(cube_pitch);
    int32_t y2 = (y * cp - z1 * sp) >> 15;
    int32_t z2 = (y * sp + z1 * cp) >> 15;

    *ox = (int16_t)x1;
    *oy = (int16_t)y2;
    *oz = (int16_t)z2;
}

static int cube_scale(void) {
    return (int)scanout_height() * 110 / 338;
}

static void project_vertex(int16_t x, int16_t y, int16_t z, int *sx, int *sy) {
    int32_t zc = (int32_t)z + CUBE_ZOFF;
    if (zc < 32) {
        zc = 32;
    }
    *sx = scene_cx() + (int)((x * CUBE_FOCAL) / zc);
    *sy = scene_cy() + (int)((y * CUBE_FOCAL) / zc);
}

static void reset_wireframe(void) {
    cube_yaw = 20;
    cube_pitch = 40;
}

static void tick_wireframe(void) {
    if ((fade_div++ & 1u) == 0) {
        gfx_fade_active();
    }

    int scale = cube_scale();
    int sx[8];
    int sy[8];
    for (int i = 0; i < 8; i++) {
        int16_t vx = (i & 1) ? (int16_t)scale : (int16_t)-scale;
        int16_t vy = (i & 2) ? (int16_t)scale : (int16_t)-scale;
        int16_t vz = (i & 4) ? (int16_t)scale : (int16_t)-scale;
        int16_t rx, ry, rz;
        rotate_vertex(vx, vy, vz, &rx, &ry, &rz);
        project_vertex(rx, ry, rz, &sx[i], &sy[i]);
    }

    static const uint8_t edges[12][2] = {
        {0, 1}, {1, 3}, {3, 2}, {2, 0},
        {4, 5}, {5, 7}, {7, 6}, {6, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    for (int e = 0; e < 12; e++) {
        uint8_t a = edges[e][0];
        uint8_t b = edges[e][1];
        gfx_line(sx[a], sy[a], sx[b], sy[b], PIXEL_BOLD);
    }

    cube_yaw = (uint8_t)(cube_yaw + 2u);
    cube_pitch = (uint8_t)(cube_pitch + 1u);
}

void scenes_init(void) {
    gfx_init();
}

void scene_reset(SceneId id) {
    fade_div = 0;
    scanout_clear(PIXEL_OFF);
    switch (id) {
    case SCENE_RADAR:
        reset_radar();
        break;
    case SCENE_LISSAJOUS:
        reset_lissajous();
        break;
    case SCENE_XOR:
        reset_xor();
        break;
    case SCENE_WIREFRAME:
        reset_wireframe();
        break;
    case SCENE_STARFIELD:
    default:
        reset_starfield();
        break;
    }
}

void scene_tick(SceneId id) {
    switch (id) {
    case SCENE_RADAR:
        tick_radar();
        break;
    case SCENE_LISSAJOUS:
        tick_lissajous();
        break;
    case SCENE_XOR:
        tick_xor();
        break;
    case SCENE_WIREFRAME:
        tick_wireframe();
        break;
    case SCENE_STARFIELD:
    default:
        tick_starfield();
        break;
    }
}

const char *scene_name(SceneId id) {
    switch (id) {
    case SCENE_RADAR:
        return "radar";
    case SCENE_LISSAJOUS:
        return "lissajous";
    case SCENE_XOR:
        return "xor";
    case SCENE_WIREFRAME:
        return "wireframe";
    case SCENE_STARFIELD:
    default:
        return "starfield";
    }
}
