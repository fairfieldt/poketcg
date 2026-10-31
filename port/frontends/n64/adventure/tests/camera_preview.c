/* Fixed-pose conversation camera comparisons; production cameras are unchanged. */
#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <math.h>
#include "../scene.h"
#include "../audio.h"
#include "../duel_bridge.h"
#include "../video.h"
#include "generated/world3d_maps.h"

extern void adventure_camera_dialogue_overlay(surface_t *,const char *,const char *);

static const unsigned models[]={7,8,9,11};
static const char *const stages[]={"angle-current","angle-30","angle-20","angle-12",
 "close-ronald","close-mason","hair-front","hair-side","hair-high"};
static const char *const labels[]={"Current: 42 degrees","Lower: 30 degrees","Lower: 20 degrees",
 "Lower: 12 degrees","Closer: Mark and Ronald","Closer: Mark and Mason",
 "Ronald: front","Ronald: side","Ronald: high view"};

static surface_t *draw(surface_t *depth,Scene *room,Scene **actors,unsigned view)
{
 surface_t *frame=display_get();rdpq_attach(frame,depth);
 gl_context_begin();scene_begin_opaque();
 glViewport(0,0,n64_video_width,n64_video_height);
 glClearColor(.69f,.82f,.86f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();
 glMatrixMode(GL_MODELVIEW);glLoadIdentity();
 if(view<6) {
  float horizontal=sqrtf(100.25f),eye_y=10,eye_x=14.5f,eye_z=30;
  if(view>0&&view<4) {
   const float pitches[]={0,30,20,12};
   eye_y=1+horizontal*tanf(pitches[view]*3.14159265359f/180);
  } else if(view>=4) {
   /* Tighter framing is a separate trial: same20-degree pitch and50 FOV. */
   horizontal=6.8f;eye_x=14+.5f*horizontal/sqrtf(100.25f);
   eye_z=20+10*horizontal/sqrtf(100.25f);
   eye_y=1+horizontal*tanf(20*3.14159265359f/180);
  }
  glMatrixMode(GL_PROJECTION);gluPerspective(50,4.0/3.0,1.5,90);
  glMatrixMode(GL_MODELVIEW);gluLookAt(eye_x,eye_y,eye_z,14,1,20,0,1,0);
  scene_draw_room(room);
  float positions[4][2];unsigned count=view<4?4:2;
  for(unsigned i=0;i<count;i++) {
   positions[i][0]=view<4?9+i*3:12.3f+i*3.4f;
   positions[i][1]=20;
  }
  scene_draw_contact_shadows(positions,count);
  for(unsigned i=0;i<count;i++) {
   unsigned actor=view<4?i:(i==0?0:view==4?1:2);
   float heading=view<4?0:i==0?40:-40;
   scene_draw_actor(actors[actor],models[actor],positions[i][0],positions[i][1],heading,0);
  }
 } else {
  /* Close checks deliberately expose every part of Ronald's hair cap. */
  glMatrixMode(GL_PROJECTION);gluPerspective(32,4.0/3.0,.1,30);
  glMatrixMode(GL_MODELVIEW);
  if(view==6)gluLookAt(0,1.9,3.4,0,1.7,0,0,1,0);
  else if(view==7)gluLookAt(3.4,1.9,0,0,1.7,0,0,1,0);
  else gluLookAt(2.4,4.1,2.4,0,1.7,0,0,1,0);
  scene_draw_actor(actors[1],models[1],0,0,0,0);
 }
 gl_context_end();rdpq_detach_wait();
 if(view<6) {
  const char *speaker=view==5?"Dr. Mason":"Ronald";
  const char *text=view==5?"To use the attack command, you need\nto attach Energy cards to your\nPokemon.":
   "Hey, MARK!\nI'm not gonna lose to you!";
  adventure_camera_dialogue_overlay(frame,speaker,text);
 }
 n64_map_label(frame,labels[view]);
 display_show(frame);adventure_audio_poll();
 assertf(glGetError()==GL_NO_ERROR,"Camera preview GL error");return frame;
}

void adventure_camera_preview(surface_t *depth,void (*capture)(surface_t *,const char *))
{
 Scene *room=scene_open(w3_maps[1].scene,W3_TEXTURE_ROOT),*actors[4];
 for(unsigned i=0;i<4;i++)actors[i]=scene_open(w3_actor_scenes[models[i]],W3_TEXTURE_ROOT);
 heap_stats_t stats;sys_get_heap_stats(&stats);
 debugf("VIDEO HEAP stage=camera-trials total=%d used=%d free=%d\n",
  stats.total,stats.used,stats.total-stats.used);
 for(unsigned view=0;view<9;view++) {
  debugf("VIDEO REVIEW %s BEGIN\n",stages[view]);
  for(unsigned f=0;f<24;f++) {
   surface_t *frame=draw(depth,room,actors,view);
   if(f==4&&capture)capture(frame,stages[view]);
  }
 }
 for(unsigned i=0;i<4;i++)scene_close(actors[i]);
 scene_close(room);debugf("VIDEO REVIEW PASS\n");
}
