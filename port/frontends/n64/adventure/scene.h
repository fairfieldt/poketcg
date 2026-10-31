#ifndef ADVENTURE_SCENE_H
#define ADVENTURE_SCENE_H
typedef struct Scene Scene;
Scene *scene_open(const char *path,const char *texture_root);
void scene_close(Scene *scene);
void scene_draw_room(const Scene *scene);
void scene_draw_contact_shadows(const float (*positions)[2],unsigned count);
void scene_draw_actor(const Scene *scene,unsigned model,float x,float z,float heading,float gait);
void scene_draw_actor_tilt(const Scene *scene,unsigned model,float x,float z,float heading,float gait,float tilt);
#endif
