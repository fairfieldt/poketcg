/* Isolated character-model style options at production cameras.
   Geometry comes from rom:/options/<id>/; production assets are untouched. */
#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <math.h>
#include <stdio.h>
#include "scene.h"
#include "audio.h"
#include "duel_bridge.h"
#include "video.h"
#include "generated/world3d_maps.h"
#include "model_options.h"

static const char *const slugs[]={"mark","dr_mason","ronald"};
static const char *const names[]={"Subject","Dr. Mason","Ronald"};
/* Views: 0 room, 1 talk, 2-4 portraits, 5-7 turnarounds. */
#define VIEW_COUNT 8

static void camera(float fov,float aspect,float near,float far,const float eye[3],const float target[3],int x,int y,int w,int h)
{
 glViewport(x,y,w,h);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();gluPerspective(fov,aspect,near,far);
 glMatrixMode(GL_MODELVIEW);glLoadIdentity();
 gluLookAt(eye[0],eye[1],eye[2],target[0],target[1],target[2],0,1,0);
 scene_camera(fov,aspect,near,far,eye,target,x,y,w,h);
}

static surface_t *draw(surface_t *depth,Scene *room,Scene **actors,unsigned option,unsigned view)
{
 surface_t *frame=display_get();
 rdpq_attach(frame,depth);gl_context_begin();scene_begin_opaque();
 glViewport(0,0,n64_video_width,n64_video_height);
 glClearColor(.69f,.82f,.86f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 const W3Map *map=&w3_maps[1];
 char label[48];
 if(view>=2&&view<5) {
  unsigned who=view-2;
  /* The exact production naming viewport, perspective and camera. */
  camera(34,104.0/154.0,.1,20,(const float[]){0,1.28f,4.7f},(const float[]){0,1.08f,0},
   n64_video_x(108),n64_video_y(44),n64_video_x(104),n64_video_y(154));
  glDisable(GL_TEXTURE_2D);glColor4f(.24f,.36f,.46f,1);
  glBegin(GL_TRIANGLE_FAN);glVertex3f(0,-.025f,0);
  for(unsigned i=0;i<=24;i++){float a=i*6.28318530718f/24;glVertex3f(cosf(a)*.78f,-.025f,sinf(a)*.6f);}
  glEnd();
  scene_draw_actor(actors[who],1,0,0,-12,0);
  snprintf(label,sizeof(label),"%s portrait %s",option_labels[option],names[who]);
 } else if(view==0||view==1) {
  if(view==0)camera(map->fov,4.0/3.0,1.5,90,map->eye,map->target,0,0,n64_video_width,n64_video_height);
  else camera(50,4.0/3.0,1.5,90,(const float[]){14.5f,10,30},(const float[]){14,1,20},0,0,n64_video_width,n64_video_height);
  scene_draw_room(room);
  float positions[6][2];unsigned count=view==0?6:3;
  for(unsigned i=0;i<count;i++) {
   if(view==0){positions[i][0]=10+(i%3)*4;positions[i][1]=i<3?20:13;}
   else{positions[i][0]=11+i*3;positions[i][1]=20;}
  }
  scene_draw_contact_shadows(positions,count);
  for(unsigned i=0;i<count;i++)
   scene_draw_actor(actors[i%3],1,positions[i][0],positions[i][1],view==0&&i>=3?180:0,0);
  snprintf(label,sizeof(label),"%s %s",option_labels[option],view==0?"room":"talk");
 } else {
  unsigned who=view-5;
  camera(36,4.0/3.0,.5,60,(const float[]){14,3.0f,27.5f},(const float[]){14,1.0f,20},0,0,n64_video_width,n64_video_height);
  scene_draw_room(room);
  float positions[4][2];
  for(unsigned i=0;i<4;i++){positions[i][0]=11.6f+i*1.6f;positions[i][1]=20;}
  scene_draw_contact_shadows(positions,4);
  for(unsigned i=0;i<4;i++)
   scene_draw_actor(actors[who],1,positions[i][0],positions[i][1],i*90,(i&1)?1.5707963f:0);
  snprintf(label,sizeof(label),"%s turn %s",option_labels[option],names[who]);
 }
 glViewport(0,0,n64_video_width,n64_video_height);
 gl_context_end();scene_frame_end();rdpq_detach_wait();
 n64_map_label(frame,label);
 display_show(frame);adventure_audio_poll();
 assertf(glGetError()==GL_NO_ERROR,"Model options GL error");return frame;
}

void adventure_model_options_preview(surface_t *depth,void (*capture)(surface_t *,const char *))
{
 static const char *const view_names[VIEW_COUNT]={"room","talk","portrait","portrait-dr_mason","portrait-ronald","turn","turn-dr_mason","turn-ronald"};
 Scene *room=scene_open(w3_maps[1].scene,W3_TEXTURE_ROOT);
 for(unsigned option=1;option<OPTION_COUNT;option++) {
  Scene *actors[3];char texture_root[96];
  snprintf(texture_root,sizeof(texture_root),"rom:/options/%s/textures/",option_ids[option]);
  for(unsigned i=0;i<3;i++) {
   char path[128];snprintf(path,sizeof(path),"rom:/options/%s/maps/actor_%s.l64",option_ids[option],slugs[i]);
   actors[i]=scene_open(path,texture_root);
  }
  const unsigned selected[]={0,1,2,5};
  for(unsigned selection=0;selection<4;selection++) {
   unsigned view=selected[selection];
   debugf("OPTIONS PHASE %s %s BEGIN\n",option_ids[option],view_names[view]);
   for(unsigned f=0;f<12;f++) {
    surface_t *frame=draw(depth,room,actors,option,view);
    if(f==4&&capture){char name[80];snprintf(name,sizeof(name),"%s-%s",option_ids[option],view_names[view]);capture(frame,name);}
   }
  }
  heap_stats_t stats;sys_get_heap_stats(&stats);
  debugf("OPTIONS HEAP option=%s total=%d used=%d free=%d\n",option_ids[option],stats.total,stats.used,stats.total-stats.used);
  for(unsigned i=0;i<3;i++)scene_close(actors[i]);
 }
 scene_close(room);debugf("MODEL OPTIONS PASS\n");
}
