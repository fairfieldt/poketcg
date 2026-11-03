/* Isolated review of the real map, actor loading and conversation renderer. */
#define main production_entry
#include "/Users/fairfieldt/dev/poketcg/port/frontends/n64/adventure/main.c"
#undef main
static void cat_capture(surface_t *frame,const char *label)
{
 const uint16_t *pixels=UncachedAddr(frame->buffer);
 debugf("FRAMEBUFFER %s %u %u\n",label,frame->width,frame->height);
 for(unsigned y=0;y<frame->height;y++)for(unsigned x=0;x<frame->width;x+=32){
  char line[129];unsigned count=frame->width-x;if(count>32)count=32;
  for(unsigned i=0;i<count;i++)snprintf(line+i*4,5,"%04x",pixels[y*(frame->stride/2)+x+i]);
  debugf("PIXELS %u %u %s\n",x,y,line);
 }
 debugf("FRAMEBUFFER END\n");
}

static void cat_model_view(unsigned model,bool turn)
{
 surface_t *frame=display_get();rdpq_attach(frame,&depth);gl_context_begin();scene_begin_opaque();
 glViewport(0,0,320,240);glClearColor(.56f,.66f,.72f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 int x=turn?0:108,y=turn?0:44,w=turn?320:104,h=turn?240:154;
 float eye[3]={0,turn?1.6f:1.28f,turn?7.9f:4.7f},target[3]={0,turn?1.28f:1.08f,0};
 glViewport(x,y,w,h);glMatrixMode(GL_PROJECTION);glLoadIdentity();gluPerspective(turn?36:34,(float)w/h,.1,30);
 glMatrixMode(GL_MODELVIEW);glLoadIdentity();gluLookAt(eye[0],eye[1],eye[2],target[0],target[1],target[2],0,1,0);
 scene_camera(turn?36:34,(float)w/h,.1,30,eye,target,x,y,w,h);
 if(turn){for(unsigned i=0;i<4;i++)scene_draw_actor(actor_scenes[model],model,-2.25f+i*1.5f,0,i*90,(i&1)?1.5707963f:0);}
 else scene_draw_actor(actor_scenes[model],model,0,0,-12,0);
 gl_context_end();scene_frame_end();rdpq_detach_wait();
 char name[64];snprintf(name,sizeof(name),"%s-%s",model==200?"tuxedo":"tabby",turn?"turn":"portrait");cat_capture(frame,name);n64_video_present(frame);
}

int main(void)
{
 debug_init_isviewer();debug_init_usblog();timer_init();
 display_init(n64_video_init(false),DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);
 display_set_fps_limit(30);dfs_init(DFS_DEFAULT_LOCATION);joypad_init();rdpq_init();gl_init();
 depth=n64_video_surface();scene_begin_opaque();
 glEnable(GL_MULTISAMPLE_ARB);glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);
 adventure_audio_init();scene_set_load_idle(adventure_audio_poll);n64_ui_init();actors=scene_open(W3_ACTOR_SCENE,W3_TEXTURE_ROOT);
 memset(&game,0,sizeof(game));strcpy(game.name,"MARK");game.text_speed=2;
 game.ow.version=OW_SAVE_VERSION;game.ow.rng=123;
 ow_set_event(&game.ow,EVENT_MEDAL_FLAGS,1);
 ow_set_event(&game.ow,EVENT_ISHIHARA_MENTIONED,1);
 ow_load_map(&game.ow,NULL,ISHIHARAS_HOUSE,12,10,NORTH);assert(!game.ow.error[0]);
 loaded_map=-1;presentation_init(&presentation,&game.ow,false);
 assert(w3_npc_models[CAT_TUXEDO_NPC]==CAT_TUXEDO_MODEL&&w3_npc_models[CAT_TABBY_NPC]==CAT_TABBY_MODEL);
 for(unsigned f=0;f<40;f++)game_draw();
 surface_t *frame=game_render_frame();cat_capture(frame,"room");n64_video_present(frame);
 for(unsigned npc=CAT_TUXEDO_NPC;npc<=CAT_TABBY_NPC;npc++) {
  game.ow.x=npc==CAT_TUXEDO_NPC?12:14;game.ow.y=10;game.ow.direction=NORTH;
  game_conversation(npc);
  for(unsigned f=0;f<40;f++)game_draw();
  frame=adventure_story_message_preview(ow_npc_name(npc),cat_duo_text(CAT_DUO_CHALLENGE_TEXT));
  surface_t still=n64_video_surface();copy_frame(&still,frame);
  for(unsigned f=0;f<4;f++){frame=display_get();copy_frame(frame,&still);n64_video_present(frame);adventure_audio_poll();}
  cat_capture(frame,npc==CAT_TUXEDO_NPC?"tuxedo":"tabby");surface_free(&still);
  game_conversation(0);for(unsigned f=0;f<30;f++)game_draw();
 }
 for(unsigned model=200;model<=201;model++){cat_model_view(model,false);cat_model_view(model,true);}
 heap_stats_t heap;sys_get_heap_stats(&heap);debugf("CAT DUO FREE HEAP %u\n",(unsigned)(heap.total-heap.used));
 debugf("CAT DUO NATIVE PASS\n");for(;;)adventure_audio_poll();
}
