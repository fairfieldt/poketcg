/* Actual source opening/camera, old and cleaned actor assets, same renderer. */
#define main grounding_production_main
#define scene_open review_scene_open
#define scene_draw_contact_shadows review_contacts
#include "grounding-main.c"
#undef main
#undef scene_open
#undef scene_draw_contact_shadows
#include <setjmp.h>
extern Scene *scene_open(const char *,const char *);
extern void scene_draw_contact_shadows(const float (*)[2],unsigned);
static unsigned pass,phase,phase_frame,trace_count;
static bool no_contacts;
static uint16_t *probe_pixels;
static unsigned soft_probes;
static jmp_buf arrived;
static OwState baseline_state;
typedef struct {PresentationPose player,npc[OW_NPC_LIMIT];PresentationCamera camera;unsigned count;} PoseTrace;
static PoseTrace traces[256];

Scene *review_scene_open(const char *path,const char *textures)
{
 char before[160];
 if(!pass&&(!strncmp(path,"rom:/world/maps/actor_",22)||!strncmp(path,"rom:/world/lod/actor_",21)||!strcmp(path,"rom:/world/actors.l64"))){
  snprintf(before,sizeof(before),"rom:/before%s",path+4);path=before;
 }
 return scene_open(path,textures);
}
void review_contacts(const float (*positions)[2],unsigned count)
{if(!no_contacts)scene_draw_contact_shadows(positions,count);}
static void pose_check(void)
{
 PoseTrace current={.player=presentation.player.pose,.camera=presentation.camera,.count=presentation.npc_count};
 for(unsigned i=0;i<presentation.npc_count;i++)current.npc[i]=presentation.npcs[i].pose;
 assert(trace_count<256);
 if(!pass)traces[trace_count]=current;
 else assertf(!memcmp(&traces[trace_count],&current,sizeof(current)),"Actor/camera animation changed at source frame%u",trace_count);
 trace_count++;
}
static void snapshot(surface_t *source)
{
 surface_t still=n64_video_surface();copy_frame(&still,source);
 char name[64];snprintf(name,sizeof(name),"%s-%s-%02u",pass?"after":"before",phase?"idle":"run",phase_frame);
 debugf("VIDEO REVIEW %s BEGIN\n",name);capture(source,name);
 for(unsigned f=0;f<24;f++){surface_t *frame=display_get();copy_frame(frame,&still);n64_video_present(frame);adventure_audio_poll();}
 surface_free(&still);
}
void game_draw(void)
{
 if(game.ow.map!=MASON_LABORATORY){presentation_sync(&presentation,&game.ow,false);presentation_step(&presentation,&game.ow,&w3_maps[game.ow.map]);adventure_audio_poll();return;}
 OwState canonical=game.ow;Presentation incoming=presentation;surface_t *frame=game_render_frame();Presentation advanced=presentation;
 assert(!memcmp(&canonical,&game.ow,sizeof(canonical)));pose_check();n64_video_present(frame);adventure_audio_poll();
 phase_frame++;
 if(pass&&phase_frame==40)memcpy(probe_pixels,UncachedAddr(frame->buffer),frame->stride*frame->height);
#ifdef GROUNDING_SELECTED_VI
 if((!phase&&phase_frame==40)||(phase&&(phase_frame==24||phase_frame==56)))snapshot(frame);
#else
 if(phase_frame==8||phase_frame==24||phase_frame==40||phase_frame==56)snapshot(frame);
#endif
 if(pass&&phase_frame==40){
  no_contacts=true;presentation=incoming;surface_t *bare=game_render_frame();unsigned changed=0;
  const uint16_t *pixels=UncachedAddr(bare->buffer);for(unsigned i=0;i<(unsigned)bare->width*bare->height;i++)changed+=pixels[i]!=probe_pixels[i];
  assertf(changed>3,"Shared soft contact disappeared with the legacy disk");soft_probes++;
  debugf("GROUNDING SOFT CONTACT phase=%u changed_pixels=%u remains=1\n",phase,changed);n64_video_present(bare);adventure_audio_poll();
  no_contacts=false;presentation=advanced;assert(!memcmp(&canonical,&game.ow,sizeof(canonical)));
 }
}
static void source_text(void *ctx,OwState *state,uint16_t text,uint8_t npc)
{
 (void)ctx;assert(text==Text05e3&&npc==NPC_DRMASON&&state->x==14&&state->y==8);
 debugf("GROUNDING SOURCE ARRIVAL pass=%u frames=%u x=%u y=%u\n",pass,phase_frame,state->x,state->y);
 phase=1;phase_frame=0;game_conversation(NPC_DRMASON);
 for(unsigned f=0;f<64;f++)game_draw();
 game_conversation(0);longjmp(arrived,1);
}
int main(void)
{
 debug_init_isviewer();debug_init_usblog();timer_init();
 display_init(n64_video_init(get_memory_size()>=8*1024*1024),DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);display_set_fps_limit(30);
 dfs_init(DFS_DEFAULT_LOCATION);joypad_init();rdpq_init();gl_init();depth=n64_video_surface();scene_begin_opaque();glEnable(GL_MULTISAMPLE_ARB);glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);
 adventure_audio_init();scene_set_load_idle(adventure_audio_poll);n64_ui_init();
 probe_pixels=malloc(n64_video_width*n64_video_height*2);assert(probe_pixels);
 for(pass=0;pass<2;pass++){
  scene_close(actors);actors=review_scene_open(W3_ACTOR_SCENE,W3_TEXTURE_ROOT);loaded_map=-1;
  memset(&game,0,sizeof(game));strcpy(game.name,"Mark");game.text_speed=2;player_running=false;session_active=false;
  presentation_init(&presentation,NULL,true);phase=phase_frame=trace_count=0;
  OwHost host=game_host;host.text=source_text;host.save=NULL;
  if(!setjmp(arrived))ow_new_game(&game.ow,&host,0x293157);
  assert(!game.ow.error[0]);
  if(!pass)baseline_state=game.ow;else assert(!memcmp(&baseline_state,&game.ow,sizeof(baseline_state)));
  debugf("GROUNDING SOURCE PASS variant=%s width=%u frames=%u same_poses=%u canonical_readonly=1\n",pass?"after":"before",n64_video_width,trace_count,pass);
 }
 assert(soft_probes==2);
 debugf("ACTOR GROUNDING NATIVE PASS width=%u frames=%u source_run=1 idle_camera=1 identical_animation=1 state_rng=1 soft_probes=%u\n",n64_video_width,trace_count,soft_probes);
 debugf("VIDEO REVIEW PASS\n");for(;;)adventure_audio_poll();
}
