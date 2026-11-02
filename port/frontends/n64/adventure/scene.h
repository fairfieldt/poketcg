#ifndef ADVENTURE_SCENE_H
#define ADVENTURE_SCENE_H
typedef struct Scene Scene;
/* Optional idle work between completed loading batches, outside frame/block
 * recording. Standalone consumers keep the default null callback. */
void scene_set_load_idle(void (*callback)(void));
/* Establish the shared opaque world/portrait pass before clearing or drawing. */
void scene_begin_opaque(void);
/* One explicit camera scope per frame. End it and complete the RDP fence
 * before the next frame reuses DMA matrices. GL callers still establish
 * their normal matrices; this is a no-op in cached-GL builds. */
void scene_camera(float fov,float aspect,float near,float far,const float eye[3],const float target[3],int x,int y,int width,int height);
/* End an explicit native camera scope before the frame completion fence. */
void scene_frame_end(void);
Scene *scene_open(const char *path,const char *texture_root);
void scene_close(Scene *scene);
void scene_draw_room(const Scene *scene);
void scene_draw_contact_shadows(const float (*positions)[2],unsigned count);
/* Small depth-tested halo for a staged card reveal; no depth-buffer writes. */
void scene_draw_card_glow(float x,float y,float z,float amount,float phase);
void scene_draw_actor(const Scene *scene,unsigned model,float x,float z,float heading,float gait);
void scene_draw_actor_tilt(const Scene *scene,unsigned model,float x,float z,float heading,float gait,float tilt);
void scene_draw_actor_presented(const Scene *scene,unsigned model,float x,float y,float z,float heading,float scale,float lean,float gait,float tilt);
#endif
