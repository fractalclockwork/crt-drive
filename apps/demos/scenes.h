#ifndef CRT_DEMOS_SCENES_H
#define CRT_DEMOS_SCENES_H

#include <stdint.h>

#define SCENE_COUNT 5

typedef enum {
    SCENE_STARFIELD = 0,
    SCENE_RADAR,
    SCENE_LISSAJOUS,
    SCENE_XOR,
    SCENE_WIREFRAME
} SceneId;

void scenes_init(void);
void scene_reset(SceneId id);
void scene_tick(SceneId id);
const char *scene_name(SceneId id);

#endif
