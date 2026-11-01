/* Private Water cast art review. Production renderer/state/cameras unchanged. */
#define main production_entry
#define scene_open water_review_scene_open
#include "../main.c"
#undef main
#undef scene_open
#include "water_review_assets.h"
#include "water_resident_pad.h"
#if WATER_RESIDENT_PAD_BYTES > 0
volatile unsigned char water_review_resident_pad[WATER_RESIDENT_PAD_BYTES] __attribute__((used));
#endif
Scene *scene_open(const char *,const char *);
static bool before;

/* The normal production entry remains linked but is never executed here.
   Capture is diagnostic-only and deliberately outside the production path. */
static void capture(surface_t *frame,const char *label)
{
 const uint16_t *pixels=UncachedAddr(frame->buffer);
 debugf("FRAMEBUFFER %s %u %u\n",label,frame->width,frame->height);
 for(unsigned y=0;y<frame->height;y++)for(unsigned x=0;x<frame->width;x+=32) {
  char line[129];unsigned count=frame->width-x;if(count>32)count=32;
  for(unsigned i=0;i<count;i++)snprintf(line+i*4,5,"%04x",pixels[y*(frame->stride/2)+x+i]);
  debugf("PIXELS %u %u %s\n",x,y,line);
 }
 debugf("FRAMEBUFFER END\n");
}

Scene *water_review_scene_open(const char *path,const char *textures)
{
 char replacement[192],texture_root[96];
 const char *relative=NULL;
 if(!strncmp(path,"rom:/world/maps/actor_",22))relative=path+11;
 else if(!strncmp(path,"rom:/world/lod/actor_",21))relative=path+11;
 if(relative) {
  snprintf(replacement,sizeof(replacement),"rom:/cast/%s/%s",before?"before":"after",relative);
  snprintf(texture_root,sizeof(texture_root),"rom:/cast/%s/textures/",before?"before":"after");
  return scene_open(replacement,texture_root);
 }
 return scene_open(path,textures);
}

static void camera(float fov,float aspect,const float eye[3],const float target[3],int x,int y,int w,int h)
{
 glViewport(x,y,w,h);glMatrixMode(GL_PROJECTION);glLoadIdentity();gluPerspective(fov,aspect,.3,30);
 glMatrixMode(GL_MODELVIEW);glLoadIdentity();gluLookAt(eye[0],eye[1],eye[2],target[0],target[1],target[2],0,1,0);
 scene_camera(fov,aspect,.3,30,eye,target,x,y,w,h);
}
static surface_t *gallery(Scene *actor,unsigned model,const char *label)
{
 static const float headings[]={0,45,90,180,270,315};
 surface_t *frame=display_get();
 for(unsigned view=0;view<6;view++) {
  /* Each packed viewport owns DMA matrices until a completed RDP fence. */
  rdpq_attach(frame,&depth);gl_context_begin();scene_begin_opaque();
  if(!view) {
   glViewport(0,0,n64_video_width,n64_video_height);glClearColor(.62,.73,.77,1);
   glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  }
  unsigned col=view%3,row=view/3;
  int x=n64_video_x(col*106),right=n64_video_x((col+1)*106),y=n64_video_y((1-row)*108+12);
  camera(34,(float)(right-x)/n64_video_y(108),(const float[]){0,1.47,5.55},
         (const float[]){0,1.22,0},x,y,right-x,n64_video_y(108));
  scene_draw_contact_shadows((const float[][2]){{0,0}},1);
  scene_draw_actor(actor,model,0,0,headings[view],view==5?1.57079632679f:0);
  glViewport(0,0,n64_video_width,n64_video_height);gl_context_end();scene_frame_end();rdpq_detach_wait();
 }
 n64_map_label(frame,label);assertf(glGetError()==GL_NO_ERROR,"Water gallery GL error");return frame;
}
static void snapshot(surface_t *frame,const char *label,uint64_t elapsed,unsigned samples)
{
 debugf("VIDEO REVIEW %s BEGIN\n",label);capture(frame,label);
 heap_stats_t heap;sys_get_heap_stats(&heap);
 debugf("WATER_CASE {\"name\":\"%s\",\"free_heap\":%u,\"mean_cadence_us\":%u,\"samples\":%u}\n",
  label,(unsigned)(heap.total-heap.used),(unsigned)(TICKS_TO_US(elapsed)/samples),samples);
#ifdef WATER_SELECTED_VI
 surface_t still=n64_video_surface();copy_frame(&still,frame);
 for(unsigned hold=0;hold<36;hold++) {
  surface_t *output=display_get();copy_frame(output,&still);n64_video_present(output);adventure_audio_poll();
 }
 surface_free(&still);
#endif
}
static void fixture(unsigned npc,bool lying)
{
 memset(&game,0,sizeof(game));strcpy(game.name,"MARK");game.text_speed=2;
 game.ow.map=WATER_CLUB;game.ow.x=16;game.ow.y=24;game.ow.direction=NORTH;
 if(npc==NPC_AMY)ow_set_event(&game.ow,EVENT_JOSHUA_STATE,JOSHUA_DEFEATED);
 for(const OwNpcDef *n=ow_map_npcs[WATER_CLUB];n&&n->id;n++) {
  if(n->id==255||n->x>=w3_maps[WATER_CLUB].width||n->y>=w3_maps[WATER_CLUB].height)continue;
  unsigned i=game.ow.npc_count++;assertf(i<OW_NPC_LIMIT,"Water fixture overflow");
  game.ow.npcs[i]=(OwNpc){n->id,n->x,n->y,n->dir,1,ow_npc_headers[n->id].sprite};
  game.ow.npc_flags[i]=ow_npc_headers[n->id].flags;
  game.ow.npc_animation[i]=n->id==NPC_AMY&&lying?SPRITE_ANIM_CGB_AMY_LAYING:ow_npc_headers[n->id].animation;
  if(npc==NPC_AMY&&n->id==NPC_AMY) {
   /* Script_MeetAmy wakes Amy, then moves her one source step west. */
   game.ow.npcs[i].x=20;game.ow.npcs[i].y=4;
   game.ow.npc_animation[i]=SPRITE_ANIM_BLUE_NPC_UP;game.ow.npc_flags[i]=0;
  }
  if(npc==n->id) {
   /* Source interaction anchors, respecting the awake Amy source state. */
   game.ow.x=n->x;game.ow.y=n->y+2;game.ow.npcs[i].direction=SOUTH;
   if(npc==NPC_SARA){game.ow.x=n->x+2;game.ow.y=n->y;game.ow.direction=WEST;game.ow.npcs[i].direction=EAST;}
   else if(npc==NPC_AMANDA){game.ow.x=n->x-2;game.ow.y=n->y;game.ow.direction=EAST;game.ow.npcs[i].direction=WEST;}
   else if(npc==NPC_AMY){game.ow.x=20;game.ow.y=6;game.ow.npcs[i].direction=SOUTH;}
  }
 }
 loaded_map=-1;player_running=false;
 presentation_init(&presentation,&game.ow,false);game_conversation(npc);
 for(unsigned f=0;f<60;f++)presentation_camera_step(&presentation,&game.ow,&w3_maps[WATER_CLUB]);
}
static void calm_pose(void)
{
 presentation.render_frames=0;presentation.player.idle_phase=0;
 for(unsigned n=0;n<presentation.npc_count;n++)presentation.npcs[n].idle_phase=0;
}
int main(void)
{
#if WATER_RESIDENT_PAD_BYTES > 0
 water_review_resident_pad[0]=water_review_resident_pad[WATER_RESIDENT_PAD_BYTES-1]=0;
#endif
 debug_init_isviewer();debug_init_usblog();timer_init();
 display_init(n64_video_init(true),DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);display_set_fps_limit(30);
 dfs_init(DFS_DEFAULT_LOCATION);joypad_init();rdpq_init();gl_init();depth=n64_video_surface();
 scene_begin_opaque();glEnable(GL_MULTISAMPLE_ARB);glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);
 adventure_audio_init();scene_set_load_idle(adventure_audio_poll);n64_ui_init();
 actors=scene_open(W3_ACTOR_SCENE,W3_TEXTURE_ROOT);
 unsigned count=0;
 for(unsigned actor=0;actor<WATER_ACTOR_COUNT;actor++)for(unsigned detail=0;detail<2;detail++)for(unsigned variant=0;variant<2;variant++) {
#ifdef WATER_SELECTED_VI
  if(!((actor==0&&!detail)||(actor==4&&detail)))continue;
#endif
  before=variant==0;char path[128],label[80];
  snprintf(path,sizeof(path),"rom:/world/%s/actor_%s.l64",detail?"lod":"maps",water_actors[actor].slug);
  Scene *person=water_review_scene_open(path,W3_TEXTURE_ROOT);
  snprintf(label,sizeof(label),"%s-%s-%s",water_actors[actor].name,detail?"lod":"full",before?"before":"after");
  OwState canonical=game.ow;uint64_t elapsed=0;
  for(unsigned f=0;f<8;f++) {
   uint64_t start=get_ticks();surface_t *frame=gallery(person,water_actors[actor].model,label);
   n64_video_present(frame);adventure_audio_poll();if(f>=3)elapsed+=get_ticks()-start;
   if(f==7)snapshot(frame,label,elapsed,5);
  }
  assertf(!memcmp(&game.ow,&canonical,sizeof(canonical)),"Gallery changed source state");scene_close(person);count++;
 }
 static const unsigned targets[]={0,NPC_AMY,NPC_JOSHUA,NPC_SARA,NPC_AMANDA};
 static const char *const names[]={"water-lying","amy-dialogue","joshua-dialogue","sara-dialogue","amanda-dialogue"};
 for(unsigned c=0;c<5;c++)for(unsigned variant=0;variant<2;variant++) {
#ifdef WATER_SELECTED_VI
  if(c!=1)continue;
#endif
  before=variant==0;fixture(targets[c],c==0);OwState canonical=game.ow;
  char label[80];snprintf(label,sizeof(label),"%s-%s",names[c],before?"before":"after");uint64_t elapsed=0;
  for(unsigned f=0;f<12;f++) {
   calm_pose();uint64_t start=get_ticks();surface_t *frame;
   if(targets[c])frame=adventure_story_message_preview(ow_npc_name(targets[c]),"Welcome to the Water Club!");
   else {frame=game_render_frame();n64_video_present(frame);}
   adventure_audio_poll();if(f>=4)elapsed+=get_ticks()-start;
   if(f==11)snapshot(frame,label,elapsed,8);
  }
  assertf(!memcmp(&game.ow,&canonical,sizeof(canonical)),"Water review changed source state");count++;
 }
 debugf("WATER CAST NATIVE PASS frames=%u width=%u canonical_state_unchanged=1\n",count,n64_video_width);
 for(;;)adventure_audio_poll();
}
