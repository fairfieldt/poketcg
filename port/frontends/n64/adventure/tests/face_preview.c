/* Isolated face texture trials; production geometry and presentation are fixed. */
#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <assert.h>
#include <stdio.h>
#include "../scene.h"
#include "../audio.h"
#include "../duel_bridge.h"
#include "generated/world3d_maps.h"

static const char *const options[]={"baseline","moderate","bold","large"};
static const char *const labels[]={"A Current","B Larger","C Bold","D Big eyes"};
static const char *const slugs[]={"mark","dr_mason","nikki","ronald","ken","granny"};
static const unsigned models[]={7,9,12,8,14,43};
static const char *const views[]={"portrait","room","conversation"};

static surface_t *draw(surface_t *depth,Scene *room,Scene **actors,unsigned option,unsigned view)
{
 surface_t *frame=display_get();rdpq_attach(frame,depth);gl_context_begin();scene_begin_opaque();
 glViewport(0,0,320,240);glClearColor(.69f,.82f,.86f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 if(view==0) {
  for(unsigned i=0;i<3;i++) {
   glViewport(2+i*106,38,104,154);
   glMatrixMode(GL_PROJECTION);glLoadIdentity();gluPerspective(34,104.0/154.0,.1,20);
   glMatrixMode(GL_MODELVIEW);glLoadIdentity();gluLookAt(0,1.28,4.7,0,1.08,0,0,1,0);
   scene_draw_actor(actors[i],models[i],0,0,0,0);
  }
 } else {
  const W3Map *map=&w3_maps[1];
  glMatrixMode(GL_PROJECTION);glLoadIdentity();gluPerspective(view==1?map->fov:50,4.0/3.0,1.5,90);
  glMatrixMode(GL_MODELVIEW);glLoadIdentity();
  if(view==1)gluLookAt(map->eye[0],map->eye[1],map->eye[2],map->target[0],map->target[1],map->target[2],0,1,0);
  else gluLookAt(14.5,10,30,14,1,20,0,1,0);
  scene_draw_room(room);
  float positions[6][2];
  for(unsigned i=0;i<6;i++) {
   positions[i][0]=view==1?10+(i%3)*4:9+i*2;
   positions[i][1]=view==1?(i<3?20:10):20;
  }
  scene_draw_contact_shadows(positions,6);
  for(unsigned i=0;i<6;i++)scene_draw_actor(actors[i],models[i],positions[i][0],positions[i][1],0,0);
 }
 glViewport(0,0,320,240);gl_context_end();rdpq_detach_wait();
 char label[40];snprintf(label,sizeof(label),"%s %s",labels[option],view==2?"talk":views[view]);
 n64_map_label(frame,label);display_show(frame);adventure_audio_poll();
 assertf(glGetError()==GL_NO_ERROR,"Face preview GL error");return frame;
}

void adventure_face_preview(surface_t *depth,void (*capture)(surface_t *,const char *))
{
 Scene *room=scene_open(w3_maps[1].scene,W3_TEXTURE_ROOT);
 for(unsigned option=0;option<4;option++) {
  Scene *actors[6];char texture_root[96];
  snprintf(texture_root,sizeof(texture_root),"rom:/faces/%s/textures/",options[option]);
  for(unsigned i=0;i<6;i++) {
   char path[128];snprintf(path,sizeof(path),"rom:/faces/%s/maps/actor_%s.l64",options[option],slugs[i]);
   actors[i]=scene_open(path,texture_root);
  }
  for(unsigned view=0;view<3;view++) {
   debugf("FACE PHASE %s %s BEGIN\n",options[option],views[view]);
   for(unsigned frame=0;frame<24;frame++) {
    surface_t *output=draw(depth,room,actors,option,view);
    if(frame==4&&capture) {
     char name[64];snprintf(name,sizeof(name),"face-%s-%s",options[option],views[view]);capture(output,name);
    }
   }
   heap_stats_t stats;sys_get_heap_stats(&stats);
   debugf("FACE HEAP option=%s view=%s total=%d used=%d free=%d\n",options[option],views[view],stats.total,stats.used,stats.total-stats.used);
  }
  for(unsigned i=0;i<6;i++)scene_close(actors[i]);
 }
 scene_close(room);debugf("FACE PREVIEW PASS\n");
}
