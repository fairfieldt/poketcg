#ifndef OW_DATA_H
#define OW_DATA_H
#include "../overworld.h"
typedef struct {uint8_t id,x,y,dir;uint16_t preload;} OwNpcDef;
typedef struct {uint8_t x,y,map,tx,ty;} OwWarp;
typedef struct {uint16_t script,name;uint8_t sprite,deck,flags,music,animation;} OwNpcHeader;
typedef struct {uint16_t pc,next;const char *op,*a,*b,*c;} OwNative;
extern const uint8_t ow_script_bank[16384],ow_event_masks[];
extern const OwNpcHeader ow_npc_headers[];
extern const OwNpcDef *const ow_map_npcs[];
extern const OwWarp *const ow_map_warps[];
extern const uint16_t ow_map_handlers[][8];
extern const OwNative ow_native[];
extern const unsigned ow_native_count;
extern const uint8_t ow_location_warps[][4],ow_location_positions[][2];
extern const uint8_t ow_script_lengths[];
typedef struct {uint8_t map,x,y,w,h;const uint8_t *permissions;} OwPermissionPatch;
extern const OwPermissionPatch ow_permission_patches[11];
extern const uint8_t ow_world_neighbors[13][4];
extern const uint8_t *const ow_world_paths[12][12];
#endif
