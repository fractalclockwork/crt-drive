#include "scenes.h"

#include "gfx.h"
#include "video.h"

#include <string.h>

#define STAR_COUNT      96
#define RADAR_CX        (FRAME_WIDTH / 2)
#define RADAR_CY        (FRAME_HEIGHT / 2)
#define RADAR_R         160
#define CONTACT_COUNT   8
#define CUBE_SCALE      110
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

static void spawn_star(Star *s) {
    s->x = (int16_t)((rng() % 900u) - 450);
    s->y = (int16_t)((rng() % 420u) - 210);
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
        int32_t sx = (FRAME_WIDTH / 2) + (s->x * 220) / z;
        int32_t sy = (FRAME_HEIGHT / 2) + (s->y * 220) / z;
        PixelColor c = PIXEL_DIM;
        if (s->z < 50) {
            c = PIXEL_BOLD;
        } else if (s->z < 140) {
            c = PIXEL_NORMAL;
        }
        if (sx >= 0 && sy >= 0 && sx < FRAME_WIDTH && sy < FRAME_HEIGHT) {
            set_pixel((uint16_t)sx, (uint16_t)sy, c);
            if (c == PIXEL_BOLD && sx + 1 < FRAME_WIDTH) {
                set_pixel((uint16_t)(sx + 1), (uint16_t)sy, PIXEL_NORMAL);
            }
        }
    }
}

static void polar_to_xy(uint8_t angle, int radius, int *x, int *y) {
    int32_t s = gfx_sin(angle);
    int32_t c = gfx_cos(angle);
    *x = RADAR_CX + (int)((radius * s) >> 15);
    *y = RADAR_CY - (int)((radius * c) >> 15);
}

static void reset_radar(void) {
    radar_sweep = 0;
    for (int i = 0; i < CONTACT_COUNT; i++) {
        contacts[i].angle = (uint8_t)(rng() & 255u);
        contacts[i].radius = (uint8_t)(28u + (rng() % (uint32_t)(RADAR_R - 36)));
    }
}

static void tick_radar(void) {
    if ((fade_div++ & 1u) == 0) {
        gfx_fade_active();
    }

    gfx_circle(RADAR_CX, RADAR_CY, RADAR_R, PIXEL_NORMAL);
    gfx_circle(RADAR_CX, RADAR_CY, RADAR_R * 3 / 4, PIXEL_DIM);
    gfx_circle(RADAR_CX, RADAR_CY, RADAR_R / 2, PIXEL_DIM);
    gfx_circle(RADAR_CX, RADAR_CY, RADAR_R / 4, PIXEL_DIM);
    gfx_line(RADAR_CX - RADAR_R, RADAR_CY, RADAR_CX + RADAR_R, RADAR_CY, PIXEL_DIM);
    gfx_line(RADAR_CX, RADAR_CY - RADAR_R, RADAR_CX, RADAR_CY + RADAR_R, PIXEL_DIM);

    int x1, y1;
    polar_to_xy(radar_sweep, RADAR_R, &x1, &y1);
    gfx_line(RADAR_CX, RADAR_CY, x1, y1, PIXEL_BOLD);
    polar_to_xy((uint8_t)(radar_sweep + 1u), RADAR_R, &x1, &y1);
    gfx_line(RADAR_CX, RADAR_CY, x1, y1, PIXEL_NORMAL);

    for (int i = 0; i < CONTACT_COUNT; i++) {
        uint8_t d = (uint8_t)(radar_sweep - contacts[i].angle);
        if (d < 6u) {
            int cx, cy;
            polar_to_xy(contacts[i].angle, contacts[i].radius, &cx, &cy);
            PixelColor c = (d < 2u) ? PIXEL_BOLD : PIXEL_NORMAL;
            set_pixel((uint16_t)cx, (uint16_t)cy, c);
            set_pixel((uint16_t)(cx + 1), (uint16_t)cy, c);
            set_pixel((uint16_t)cx, (uint16_t)(cy + 1), PIXEL_NORMAL);
            set_pixel((uint16_t)(cx - 1), (uint16_t)cy, PIXEL_DIM);
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
    const int rx = 360;
    const int ry = 150;
    for (int i = 0; i < 12; i++) {
        uint8_t a = (uint8_t)((lissa_t + (uint16_t)(i * 3)) & 255u);
        uint8_t b = (uint8_t)(((lissa_t * 2u) / 3u + (uint16_t)(i * 2)) & 255u);
        int x = (FRAME_WIDTH / 2) + (int)((rx * (int32_t)gfx_sin(a)) >> 15);
        int y = (FRAME_HEIGHT / 2) + (int)((ry * (int32_t)gfx_sin(b)) >> 15);
        set_pixel((uint16_t)x, (uint16_t)y, PIXEL_BOLD);
        set_pixel((uint16_t)(x + 1), (uint16_t)y, PIXEL_NORMAL);
    }
    lissa_t = (uint16_t)((lissa_t + 4u) & 255u);
}

static void reset_xor(void) {
    xor_phase = 0;
}

static void tick_xor(void) {
    uint8_t phase = xor_phase;
    for (uint16_t y = 0; y < FRAME_HEIGHT; y++) {
        uint8_t *line = frame_buffer[y];
        for (uint16_t bx = 0; bx < BYTES_PER_LINE; bx++) {
            uint16_t x = (uint16_t)(bx * 4u);
            uint8_t packed = 0;
            for (uint8_t i = 0; i < 4; i++) {
                uint8_t v = (uint8_t)(((x + i) ^ y) + phase);
                uint8_t c = (uint8_t)((v >> 6) & 3u);
                packed |= (uint8_t)(c << (6 - 2 * i));
            }
            line[bx] = packed;
        }
        memset(&line[BYTES_PER_LINE], 0, LINE_STRIDE - BYTES_PER_LINE);
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

static void project_vertex(int16_t x, int16_t y, int16_t z, int *sx, int *sy) {
    int32_t zc = (int32_t)z + CUBE_ZOFF;
    if (zc < 32) {
        zc = 32;
    }
    *sx = (FRAME_WIDTH / 2) + (int)((x * CUBE_FOCAL) / zc);
    *sy = (FRAME_HEIGHT / 2) + (int)((y * CUBE_FOCAL) / zc);
}

static void reset_wireframe(void) {
    cube_yaw = 20;
    cube_pitch = 40;
}

static void tick_wireframe(void) {
    if ((fade_div++ & 1u) == 0) {
        gfx_fade_active();
    }

    int sx[8];
    int sy[8];
    for (int i = 0; i < 8; i++) {
        int16_t vx = (i & 1) ? CUBE_SCALE : (int16_t)-CUBE_SCALE;
        int16_t vy = (i & 2) ? CUBE_SCALE : (int16_t)-CUBE_SCALE;
        int16_t vz = (i & 4) ? CUBE_SCALE : (int16_t)-CUBE_SCALE;
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
    clear_buffer(PIXEL_OFF);
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
