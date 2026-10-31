#ifndef ADVENTURE_PRESENTATION_H
#define ADVENTURE_PRESENTATION_H
#include "overworld.h"
#include "../../../world3d/world3d.h"

/* Rendering state only: never serialized and never changes OwState. */
typedef struct {
 float x,z,heading,gait,bob,lean;
} PresentationPose;
typedef struct {
 PresentationPose pose;
 float from_x,from_z,target_x,target_z,target_heading,phase,idle_phase;
 unsigned elapsed,duration,movement_remainder;
 uint8_t id,visible;
 bool running;
} PresentationActor;
typedef struct {
 float eye[3],target[3],fov;
} PresentationCamera;
typedef struct {
 PresentationActor player,npcs[OW_NPC_LIMIT],world;
 PresentationPose cursor;
 PresentationCamera camera;
 uint8_t map,npc_count,world_location,world_destination;
 uint8_t world_points[64][2];
 unsigned world_count,world_index,source_remainder,render_frames;
 uint16_t world_script;
 bool initialized,camera_initialized,cinematic,tutorial_motion;
 unsigned intro_stage;
} Presentation;

enum {
 PRESENTATION_PLAYER=1, PRESENTATION_NPC=2,
 PRESENTATION_WORLD=4, PRESENTATION_MAP=8
};
/* A NULL state resets presentation before ow_new_game/ow_continue calls back. */
void presentation_init(Presentation *,const OwState *,bool cinematic);
unsigned presentation_sync(Presentation *,const OwState *,bool running);
void presentation_step(Presentation *,const OwState *,const W3Map *);
bool presentation_moving(const Presentation *);
bool presentation_scripted_player(const OwState *);
unsigned presentation_wait_frames(Presentation *,unsigned source_frames);
/* A selection moves the cursor. Call this only when the player confirms A;
   sync also starts the scripted new-game walk to Mason Laboratory. */
bool presentation_world_travel(Presentation *,unsigned destination);
const PresentationPose *presentation_player(const Presentation *);
const PresentationPose *presentation_npc(const Presentation *,unsigned id);
const PresentationPose *presentation_world(const Presentation *);
const PresentationPose *presentation_world_cursor(const Presentation *);
const PresentationCamera *presentation_camera(const Presentation *);
#endif
