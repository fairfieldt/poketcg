/* Native visual audit of every imported rigid character, using the production
   scene loader, texture setup and draw path. Four simultaneous views expose
   face/clothing depth errors and joint openings without changing game cameras. */
#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../scene.h"
#include "../audio.h"
#include "../duel_bridge.h"
#include "generated/world3d_maps.h"

static surface_t *actor_sheet(surface_t *depth,const Scene *scene,unsigned model,const char *label,bool interpenetrating)
{
 surface_t *frame=display_get();rdpq_attach(frame,depth);gl_context_begin();
 scene_begin_opaque();
 glViewport(0,0,320,240);glClearColor(.64f,.75f,.78f,1);
 glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 if(interpenetrating)glDepthFunc(GL_LESS_INTERPENETRATING_N64);
 glDisable(GL_LIGHTING);glDisable(GL_CULL_FACE);
 static const float headings[]={0,180,90,0};
 for(unsigned view=0;view<4;view++) {
  glViewport((view%2)*160,(1-view/2)*104+6,160,104);
  glMatrixMode(GL_PROJECTION);glLoadIdentity();gluPerspective(34,160.0/104.0,.5,12);
  glMatrixMode(GL_MODELVIEW);glLoadIdentity();gluLookAt(0,1.5,6.3,0,1.25,0,0,1,0);
  glDisable(GL_TEXTURE_2D);glColor4f(.34f,.43f,.45f,1);
  glBegin(GL_TRIANGLE_FAN);glVertex3f(0,-.03f,0);
  for(unsigned i=0;i<=16;i++) {float a=i*6.28318530718f/16;glVertex3f(cosf(a)*.72f,-.03f,sinf(a)*.5f);}
  glEnd();
  scene_draw_actor(scene,model,0,0,headings[view],view==3?1.57079632679f:0);
 }
 glViewport(0,0,320,240);gl_context_end();rdpq_detach_wait();
 n64_map_label(frame,label);display_show(frame);adventure_audio_poll();
 assertf(glGetError()==GL_NO_ERROR,"Character visual sweep GL error");return frame;
}

void adventure_visual_sweep(surface_t *depth,Scene *fallback,void (*capture)(surface_t *,const char *))
{
 unsigned count=0;const unsigned held=ADVENTURE_DIAGNOSTIC==5?2:24;
 for(unsigned model=1;model<256;model++) {
  if(model>6&&!w3_actor_scenes[model])continue;
  if(ADVENTURE_DIAGNOSTIC==6&&model!=7&&model!=9&&model!=31)continue;
  if(ADVENTURE_DIAGNOSTIC==7&&model!=7)continue;
  Scene *scene=model<=6?fallback:scene_open(w3_actor_scenes[model],W3_TEXTURE_ROOT);
  char label[64],name[32];
  snprintf(name,sizeof(name),"actor-%03u",model);
  snprintf(label,sizeof(label),"Actor %u: front, back, side, walk",model);
  if(ADVENTURE_DIAGNOSTIC!=7&&(model==7||model==9||model==31)) {
   char old_name[32],old_label[64];snprintf(old_name,sizeof(old_name),"inter-actor-%03u",model);
   snprintf(old_label,sizeof(old_label),"Actor %u: interpenetrating depth",model);
   debugf("VISUAL INTER ACTOR %u BEGIN\n",model);
   for(unsigned frame=0;frame<held;frame++) {
    surface_t *sheet=actor_sheet(depth,scene,model,old_label,true);
    if(frame==(ADVENTURE_DIAGNOSTIC==5?1:4)&&capture)capture(sheet,old_name);
   }
  }
  debugf("VISUAL ACTOR %u BEGIN\n",model);
  for(unsigned frame=0;frame<held;frame++) {
   surface_t *sheet=actor_sheet(depth,scene,model,label,false);
   if(frame==(ADVENTURE_DIAGNOSTIC==5?1:4)&&capture)capture(sheet,name);
  }
  if(model>6)scene_close(scene);
  count++;
 }
 debugf("ADVENTURE CHARACTER VISUAL PASS models=%u\n",count);
}
