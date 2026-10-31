/* Production lead models at controlled portrait and gameplay distances. */
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

static const unsigned models[]={7,8,9,11};
static const char *const labels[]={"Mark","Ronald","Dr. Mason","Imakuni?"};
static const char *const stages[]={"mark-portrait","ronald-portrait","mason-portrait",
 "imakuni-portrait","leads-room","leads-conversation"};

static surface_t *draw(surface_t *depth,Scene *room,Scene **actors,unsigned view)
{
 surface_t *frame=display_get();uint32_t started=timer_ticks();
 rdpq_attach(frame,depth);gl_context_begin();scene_begin_opaque();
 glViewport(0,0,n64_video_width,n64_video_height);
 glClearColor(.69f,.82f,.86f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 if(view<4) {
  /* The exact production naming viewport, perspective and camera. Each
     family's own dimensions are retained; no per-character framing fit. */
  glViewport(n64_video_x(14),n64_video_y(44),n64_video_x(104),n64_video_y(154));
  glMatrixMode(GL_PROJECTION);glLoadIdentity();gluPerspective(34,104.0/154.0,.1,20);
  glMatrixMode(GL_MODELVIEW);glLoadIdentity();gluLookAt(0,1.28,4.7,0,1.08,0,0,1,0);
  scene_camera(34,104.0/154.0,.1,20,(const float[]){0,1.28f,4.7f},(const float[]){0,1.08f,0},
   n64_video_x(14),n64_video_y(44),n64_video_x(104),n64_video_y(154));
  glDisable(GL_TEXTURE_2D);glColor4f(.24f,.36f,.46f,1);
  glBegin(GL_TRIANGLE_FAN);glVertex3f(0,-.025f,0);
  for(unsigned i=0;i<=24;i++) {
   float angle=i*6.28318530718f/24;
   glVertex3f(cosf(angle)*.78f,-.025f,sinf(angle)*.6f);
  }
  glEnd();
  scene_draw_actor(actors[view],models[view],0,0,-12,0);
 } else {
  glMatrixMode(GL_PROJECTION);glLoadIdentity();
  gluPerspective(view==4?58:50,4.0/3.0,1.5,90);
  glMatrixMode(GL_MODELVIEW);glLoadIdentity();
  if(view==4)gluLookAt(14,8.4375,32,14,0,20.1,0,1,0);
  else gluLookAt(14.5,10,30,14,1,20,0,1,0);
  scene_camera(view==4?58:50,4.0/3.0,1.5,90,
   view==4?(const float[]){14,8.4375f,32}:(const float[]){14.5f,10,30},
   (const float[]){14,view==4?0:1,view==4?20.1f:20},0,0,n64_video_width,n64_video_height);
  scene_draw_room(room);
  float positions[8][2];unsigned count=view==4?8:4;
  for(unsigned i=0;i<count;i++) {
   positions[i][0]=9+(i%4)*3;
   positions[i][1]=view==4&&i>=4?10:20;
  }
  scene_draw_contact_shadows(positions,count);
  for(unsigned i=0;i<count;i++)
   scene_draw_actor(actors[i%4],models[i%4],positions[i][0],positions[i][1],0,0);
 }
 glViewport(0,0,n64_video_width,n64_video_height);
 gl_context_end();scene_frame_end();rdpq_detach_wait();
 debugf("VIDEO FRAME stage=%s render_us=%u\n",stages[view],
  (unsigned)TICKS_TO_US((uint32_t)(timer_ticks()-started)));
 n64_map_label(frame,view<4?labels[view]:view==4?"Lead characters: room":"Lead characters: conversation");
 display_show(frame);adventure_audio_poll();
 assertf(glGetError()==GL_NO_ERROR,"Lead preview GL error");return frame;
}

void adventure_leads_preview(surface_t *depth,void (*capture)(surface_t *,const char *))
{
 Scene *room=scene_open(w3_maps[1].scene,W3_TEXTURE_ROOT),*actors[4];
 for(unsigned i=0;i<4;i++) {
  assertf(w3_actor_scenes[models[i]],"Missing lead actor scene");
  actors[i]=scene_open(w3_actor_scenes[models[i]],W3_TEXTURE_ROOT);
 }
 heap_stats_t stats;sys_get_heap_stats(&stats);
 debugf("VIDEO HEAP stage=leads total=%d used=%d free=%d\n",stats.total,stats.used,stats.total-stats.used);
 for(unsigned view=0;view<6;view++) {
  debugf("VIDEO REVIEW %s BEGIN\n",stages[view]);
  for(unsigned f=0;f<24;f++) {
   surface_t *frame=draw(depth,room,actors,view);
   if(f==4&&capture)capture(frame,stages[view]);
  }
 }
 for(unsigned i=0;i<4;i++)scene_close(actors[i]);
 scene_close(room);debugf("VIDEO REVIEW PASS\n");
}
