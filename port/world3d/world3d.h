#ifndef WORLD3D_H
#define WORLD3D_H
#include <stdint.h>
/* Scene paths are DragonFS URLs. L643 vertex layout matches the Lightning demo.
 * Load scene and (when non-NULL) overlay with a shared actor scene separately.
 * Coordinates: X east, Y up, Z south; positions use source 8px map tiles. */
typedef struct {
    const char *scene;
    const char *overlay;
    float eye[3], target[3], fov;
    uint8_t width, height;
} W3Map;
typedef struct {
    uint8_t map, event;
    const char *scene;
} W3EventScene;
typedef struct {
    uint8_t map, event;
    const char *off_scene, *on_scene;
} W3SwitchScene;
typedef struct {
    uint8_t selection, map, x, y;
    float marker_x, marker_z;
} W3WorldNode;
#define W3_MAP_COUNT 34
#define W3_ACTOR_SCENE "rom:/world/actors.l64"
#define W3_TEXTURE_ROOT "rom:/world/textures/"
#endif
