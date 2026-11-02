#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "duel_bridge.h"
#include "scene.h"
#include "audio.h"
#include "intro.h"
#include "naming.h"
#include "presentation.h"
#include "visibility.h"
#include "dialogue.h"
#include "medal.h"
#include "video.h"
#include "generated/world3d_maps.h"
#include "generated/character_lods.h"
#include "gen/constants.h"
#include "gen/text.h"
#include "../../../overworld/gen/data.h"
#include "../../common/save_store.h"

Game game;
bool game_credits_finished;
static Scene *room,*overlay,*actors,*switches[8];
static Scene *actor_scenes[256];
static Scene *actor_lod_scenes[256];
static bool actor_low_detail[256];
static AdventureFrustum frame_frustum;
static surface_t depth;
static surface_t *last_world_frame;
static unsigned last_world_map;
static int loaded_map=-1;
static const char *loaded_path;
static Presentation presentation;
static bool player_running;
static bool rendering;
static uint8_t saved_duel[TCG_SAVE_SIZE];
static bool continue_duel;
static bool duel_save_pending;
static bool session_active;
static bool opening_shown;
void game_tick(void)
{
 static uint32_t previous,elapsed_us;
 uint32_t now=timer_ticks();
 if(previous&&session_active){elapsed_us+=(uint32_t)TICKS_TO_US((uint32_t)(now-previous));while(elapsed_us>=1000000){elapsed_us-=1000000;if(game.play_seconds<3599999)game.play_seconds++;}}
 previous=now;
}
#if ADVENTURE_DIAGNOSTIC
static bool capture_next;
static void capture(surface_t *frame,const char *label)
{
 const uint16_t *pixels=UncachedAddr(frame->buffer);
 if(label)debugf("FRAMEBUFFER %s %u %u\n",label,frame->width,frame->height);else debugf("FRAMEBUFFER map-%02u %u %u\n",game.ow.map,frame->width,frame->height);
 for(unsigned y=0;y<frame->height;y++)for(unsigned x=0;x<frame->width;x+=32){char line[129];unsigned count=frame->width-x;if(count>32)count=32;for(unsigned i=0;i<count;i++)snprintf(line+i*4,5,"%04x",pixels[y*(frame->stride/2)+x+i]);debugf("PIXELS %u %u %s\n",x,y,line);}
 debugf("FRAMEBUFFER END\n");
}
#endif

static const char *map_path(void)
{
 if(game.ow.map==2)return W3_DECK_MACHINE_BASE;
 const char *path=w3_maps[game.ow.map].scene;
 for(unsigned i=0;i<W3_EVENT_SCENE_COUNT;i++) {
  const W3EventScene *event=&w3_event_scenes[i];
  bool enabled=game.ow.map_patch[event->event]!=0;
  if(event->map==2)for(unsigned machine=2;machine<=9;machine++)enabled=enabled&&game.ow.map_patch[machine];
  if(event->map==game.ow.map && enabled)path=event->scene;
 }
 return path;
}
static void load_scene(void)
{
 const char *path=map_path();
 if(loaded_map==game.ow.map && loaded_path==path)return;
 if(loaded_map!=game.ow.map) {
  for(unsigned model=7;model<256;model++) {
   scene_close(actor_scenes[model]);actor_scenes[model]=NULL;
   scene_close(actor_lod_scenes[model]);actor_lod_scenes[model]=NULL;
  }
  adventure_audio_poll();
  memset(actor_low_detail,0,sizeof(actor_low_detail));
 }
 scene_close(overlay);scene_close(room);overlay=NULL;
 for(unsigned i=0;i<8;i++){scene_close(switches[i]);switches[i]=NULL;}
 adventure_audio_poll();
 room=scene_open(path,W3_TEXTURE_ROOT);
 adventure_audio_poll();
 const W3Map *map=&w3_maps[game.ow.map];
 if(map->overlay){overlay=scene_open(map->overlay,W3_TEXTURE_ROOT);adventure_audio_poll();}
 if(game.ow.map==2)for(unsigned i=0;i<W3_SWITCH_SCENE_COUNT;i++){const W3SwitchScene *panel=&w3_switch_scenes[i];switches[i]=scene_open(game.ow.map_patch[panel->event]?panel->on_scene:panel->off_scene,W3_TEXTURE_ROOT);adventure_audio_poll();}
 loaded_map=game.ow.map;loaded_path=path;
 glMatrixMode(GL_PROJECTION);glLoadIdentity();
 gluPerspective(map->fov,4.0/3.0,1.5,90);
 glMatrixMode(GL_MODELVIEW);
 assertf(glGetError()==GL_NO_ERROR,"Map setup GL error");
}
static unsigned npc_model(unsigned index)
{
 unsigned id=game.ow.npcs[index].id;
 if(game.ow.npc_animation[index]==SPRITE_ANIM_CGB_AMY_LAYING&&w3_npc_pose_models[id])return w3_npc_pose_models[id];
 return w3_npc_models[id];
}
static const char *actor_path(unsigned model)
{
 for(unsigned i=0;i<W3_MAP_ACTOR_SCENE_COUNT;i++)
  if(w3_map_actor_scenes[i].map==game.ow.map&&w3_map_actor_scenes[i].model==model)
   return w3_map_actor_scenes[i].scene;
 return w3_actor_scenes[model];
}
static void load_characters(void)
{
 uint8_t needed[256]={0};
 needed[W3_PLAYER_MODEL]=1;
 if(game.ow.map==0)needed[6]=1;
 else {
  needed[W3_PLAYER_MODEL]=1;
  for(unsigned i=0;i<game.ow.npc_count;i++)if(game.ow.npcs[i].visible)needed[npc_model(i)]=1;
 }
 for(unsigned model=7;model<256;model++) {
  if(actor_scenes[model]&&!needed[model]){scene_close(actor_scenes[model]);actor_scenes[model]=NULL;}
  if(actor_lod_scenes[model]&&!needed[model]){scene_close(actor_lod_scenes[model]);actor_lod_scenes[model]=NULL;}
 }
 /* Preloads run before the frame's GL/RDP context. Keep the AI ring fed
    between completed loads rather than waiting for the whole actor set. */
 for(unsigned model=7;model<256;model++)if(needed[model]&&actor_path(model)&&!actor_scenes[model]){actor_scenes[model]=scene_open(actor_path(model),W3_TEXTURE_ROOT);adventure_audio_poll();}
 for(unsigned model=7;model<256;model++)if(needed[model]&&w3_actor_lod_scenes[model]&&!actor_lod_scenes[model]){actor_lod_scenes[model]=scene_open(w3_actor_lod_scenes[model],W3_TEXTURE_ROOT);adventure_audio_poll();}
}
static const Scene *actor_scene(unsigned model,unsigned npc,const PresentationPose *pose,float scale,float floor)
{
 const Scene *full=model<256&&actor_scenes[model]?actor_scenes[model]:actors;
 if(model>=256||!actor_lod_scenes[model]||!frame_frustum.valid)return full;
 /* Close conversations retain the exact lead heads before the camera moves.
    Hysteresis prevents a distant character flickering between detail levels. */
 bool focus=presentation.conversation_npc&&(!npc||npc==presentation.conversation_npc);
 float position[3]={pose->x-frame_frustum.eye[0],floor+pose->floor+1.3f*scale-frame_frustum.eye[1],pose->z-frame_frustum.eye[2]};
 float depth=0;for(unsigned c=0;c<3;c++)depth+=position[c]*frame_frustum.forward[c];
 float pixels=depth>1?3*scale*120/(depth*frame_frustum.tan_y):1000;
 bool low=!focus&&pixels<(actor_low_detail[npc]?60:48);
 actor_low_detail[npc]=low;
 return low?actor_lod_scenes[model]:full;
}
static void draw_presented_actor(unsigned model,const PresentationPose *pose,float scale,float floor,float tilt,unsigned npc)
{
 if(!pose)return;
 scene_draw_actor_presented(actor_scene(model,npc,pose,scale,floor),model,pose->x,
  floor+pose->floor+pose->bob*scale,pose->z,pose->heading,scale,pose->lean,pose->gait,tilt);
}
static void copy_frame(surface_t *destination,const surface_t *source)
{
 rdpq_attach(destination,NULL);rdpq_mode_push();rdpq_set_mode_copy(false);
 rdpq_tex_blit(source,0,0,NULL);rdpq_mode_pop();rdpq_detach_wait();
}
#if ADVENTURE_DIAGNOSTIC == 13
/* Hold a completed production frame for the VI encoder. The game and its
   presentation are frozen only after the real dialogue camera has settled. */
static void conversation_capture(surface_t *source,const char *label)
{
 surface_t still=n64_video_surface();copy_frame(&still,source);
 debugf("VIDEO REVIEW %s BEGIN\n",label);
 for(unsigned f=0;f<24;f++) {
  surface_t *frame=display_get();copy_frame(frame,&still);
  if(f==4)capture(frame,label);
  n64_video_present(frame);adventure_audio_poll();
 }
 surface_free(&still);
}
static void conversation_fixture(unsigned map,unsigned npc,unsigned x,unsigned z,unsigned direction,const char *label)
{
 memset(&game,0,sizeof(game));strcpy(game.name,"MARK");game.text_speed=2;
 game.ow.map=map;game.ow.x=x;game.ow.y=z;game.ow.direction=direction;
 for(const OwNpcDef *n=ow_map_npcs[map];n&&n->id;n++) {
  if(n->id==255||n->x>=w3_maps[map].width||n->y>=w3_maps[map].height)continue;
  unsigned i=game.ow.npc_count++;assert(i<OW_NPC_LIMIT);
  game.ow.npcs[i]=(OwNpc){n->id,n->x,n->y,n->dir,1,ow_npc_headers[n->id].sprite};
  game.ow.npc_flags[i]=ow_npc_headers[n->id].flags;
  game.ow.npc_animation[i]=ow_npc_headers[n->id].animation;
 }
 presentation_init(&presentation,NULL,false);game_conversation(npc);
 /* Match ow_interact's source facing without moving any source positions. */
 for(unsigned i=0;i<game.ow.npc_count;i++)if(game.ow.npcs[i].id==npc)
  game.ow.npcs[i].direction=(direction+2)&3;
 OwState before=game.ow;
 for(unsigned f=0;f<40;f++)game_draw();
 conversation_capture(adventure_story_message_preview(ow_npc_name(npc),
  npc==NPC_RONALD1?"Hey, MARK!\nI'm not gonna lose to you!":"Welcome!"),label);
 assertf(!memcmp(&before,&game.ow,sizeof(before)),"Camera changed canonical state");
 game_conversation(0);for(unsigned f=0;f<40;f++)game_draw();
}
#endif
surface_t *game_render_frame(void)
{
 assertf(!rendering,"Reentrant world frame");
 rendering=true;load_scene();load_characters();
 const W3Map *map=&w3_maps[game.ow.map];
 presentation_sync(&presentation,&game.ow,player_running);
 presentation_step(&presentation,&game.ow,map);
 const PresentationCamera *camera=presentation_camera(&presentation);
 AdventureFrustum frustum;adventure_frustum_init(&frustum,camera,4.0f/3.0f,1.5f,90);
 frame_frustum=frustum;
 surface_t *frame=display_get();rdpq_attach(frame,&depth);gl_context_begin();scene_begin_opaque();
#if ADVENTURE_DIAGNOSTIC == 10 || ADVENTURE_DIAGNOSTIC == 15
 uint32_t render_started=timer_ticks();
#endif
 glViewport(0,0,n64_video_width,n64_video_height);
 if(game.ow.map)glClearColor(26/255.0f,18/255.0f,14/255.0f,1);
 else glClearColor(.02,.025,.055,1);
 glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();gluPerspective(camera->fov,4.0/3.0,1.5,90);
 glMatrixMode(GL_MODELVIEW);glLoadIdentity();
 gluLookAt(camera->eye[0],camera->eye[1],camera->eye[2],camera->target[0],camera->target[1],camera->target[2],0,1,0);
 scene_camera(camera->fov,4.0f/3.0f,1.5f,90,camera->eye,camera->target,0,0,n64_video_width,n64_video_height);
 scene_draw_room(room);scene_draw_room(overlay);
 for(unsigned i=0;i<8;i++)scene_draw_room(switches[i]);
 if(game.ow.map==0) {
  /* The source map draws its player and selection cursor in front of the
     landmark icons. Keep their own 3D depth while clearing the island's Z. */
  glClear(GL_DEPTH_BUFFER_BIT);
  const PresentationPose *world_player=presentation_world(&presentation);
  if(adventure_frustum_actor(&frustum,world_player,.48f,.35f,0))
   draw_presented_actor(W3_PLAYER_MODEL,world_player,.48f,.35f,0,0);
  const PresentationPose *cursor=presentation_world_cursor(&presentation);
  if(adventure_frustum_sphere(&frustum,cursor->x,1.8f+cursor->bob+ADVENTURE_ACTOR_CENTER*.55f,
      cursor->z,ADVENTURE_ACTOR_RADIUS*.55f)) {
   scene_draw_actor_presented(actors,6,cursor->x,1.8f+cursor->bob,cursor->z,0,.55f,0,0,0);
  }
 } else {
  float contacts[OW_NPC_LIMIT+1][2];unsigned contact_count=0;
  const PresentationPose *player=presentation_player(&presentation);
  bool draw_player=adventure_frustum_actor(&frustum,player,1,0,0);
  bool draw_npc[OW_NPC_LIMIT]={0};
  float tilt[OW_NPC_LIMIT]={0};
  if(draw_player){contacts[contact_count][0]=player->x;contacts[contact_count++][1]=player->z;}
  for(unsigned i=0;i<game.ow.npc_count;i++) {
   const OwNpc *npc=&game.ow.npcs[i];
   if(!npc->visible)continue;
   const PresentationPose *pose=presentation_npc(&presentation,npc->id);
   /* Source scripts park actors past the map edge, where the Game Boy never
      shows them (Ronald below the Dome entrance). They appear as they walk
      in through a doorway. */
   if(!pose||pose->x<-1||pose->z<-1||pose->x>w3_maps[game.ow.map].width-1||
      pose->z>w3_maps[game.ow.map].height-1)continue;
   tilt[i]=game.ow.npc_animation[i]==SPRITE_ANIM_CGB_AMY_LAYING&&!w3_npc_pose_models[npc->id]?90:0;
   draw_npc[i]=adventure_frustum_actor(&frustum,pose,1,0,tilt[i]);
   if(!draw_npc[i]||(npc->id>=NPC_TORCH&&npc->id<=NPC_LEGENDARY_CARD_RIGHT_SPARK)||
      game.ow.npc_animation[i]==SPRITE_ANIM_CGB_AMY_LAYING)continue;
   if(pose->floor)continue;
   contacts[contact_count][0]=pose->x;contacts[contact_count++][1]=pose->z;
  }
  scene_draw_contact_shadows(contacts,contact_count);
  for(unsigned i=0;i<game.ow.npc_count;i++) {
   const OwNpc *npc=&game.ow.npcs[i];if(!draw_npc[i])continue;
   unsigned model=npc_model(i);
   draw_presented_actor(model,presentation_npc(&presentation,npc->id),1,0,tilt[i],npc->id);
  }
  if(draw_player)draw_presented_actor(W3_PLAYER_MODEL,player,1,0,0,0);
 }
 gl_context_end();scene_frame_end();rdpq_detach_wait();
#if ADVENTURE_DIAGNOSTIC == 10 || ADVENTURE_DIAGNOSTIC == 15
 debugf("VIDEO FRAME map=%u render_us=%u\n",game.ow.map,(unsigned)TICKS_TO_US((uint32_t)(timer_ticks()-render_started)));
#endif
 unsigned selection=game.ow.overworld_selection;
 if(selection<1||selection>12)selection=1;
 unsigned map_name=OverworldMapMasonLaboratoryText+selection-1;
 if(selection==2&&!ow_event(&game.ow,EVENT_ISHIHARAS_HOUSE_MENTIONED))map_name=OverworldMapMysteryHouseText;
 if(game.ow.map==0)n64_map_label(frame,tcg_text(map_name));
#if ADVENTURE_DIAGNOSTIC
 assertf(glGetError()==GL_NO_ERROR,"Map render GL error");
 if(capture_next){capture(frame,NULL);capture_next=false;}
#endif
 last_world_frame=frame;last_world_map=game.ow.map;
 rendering=false;return frame;
}
void game_draw(void)
{
 if(rendering)return;
 n64_video_present(game_render_frame());adventure_audio_poll();
}
static void warp_fade(bool revealing,unsigned map,const surface_t *snapshot)
{
 for(unsigned step=1;step<=6;step++) {
  surface_t *frame;
  if(revealing)frame=game_render_frame();
  else {frame=display_get();copy_frame(frame,snapshot);}
  unsigned alpha=(revealing?6-step:step)*255/6;
  rdpq_attach(frame,NULL);rdpq_mode_push();
  color_t tint=map?RGBA32(26,18,14,alpha):RGBA32(5,6,14,alpha);
  if(alpha==255)rdpq_set_mode_fill(tint);
  else {
   rdpq_set_mode_standard();rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
   rdpq_set_prim_color(tint);rdpq_mode_alphacompare(0);
   rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
  }
  rdpq_fill_rectangle(0,0,n64_video_width,n64_video_height);
  rdpq_mode_pop();rdpq_detach_wait();n64_video_present(frame);adventure_audio_poll();
 }
}

static void format_story(uint16_t id,char *out,size_t capacity,unsigned depth_level)
{
 if(depth_level>4){if(capacity)out[0]=0;return;}
 const char *source=tcg_text(id);size_t n=0;unsigned ramtext=0,ramnum=0;
 for(;*source&&n+1<capacity;source++) {
  char replacement[512];const char *insert=NULL;
  if(*source==9)insert=game.name;
  else if(*source==11){unsigned value=game.ow.txram[ramtext++%2];format_story(value,replacement,sizeof(replacement),depth_level+1);insert=replacement;}
  else if(*source==12){snprintf(replacement,sizeof(replacement),"%u",game.ow.txram[2+(ramnum++%2)]);insert=replacement;}
  if(insert){size_t length=strlen(insert);if(length>capacity-1-n)length=capacity-1-n;memcpy(out+n,insert,length);n+=length;}
  else out[n++]=*source;
 }
 out[n]=0;
}
void game_story_format(unsigned text,char *out,unsigned capacity)
{format_story(text,out,capacity,0);}
void game_conversation(unsigned npc)
{presentation_conversation(&presentation,npc);}
static void host_text(void *ctx,OwState *state,uint16_t text,uint8_t npc)
{
 (void)ctx;(void)state;
#if ADVENTURE_DIAGNOSTIC
 debugf("STORY text=%u npc=%u\n",text,npc);
#else
 n64_ui_text_speed(game.text_speed);
 static char formatted[8192];format_story(text,formatted,sizeof(formatted),0);
 game_conversation(npc);
 adventure_story_message(npc?ow_npc_name(npc):"",formatted,game.text_speed);
 game_conversation(0);
#endif
}
static int host_choice(void *ctx,OwState *state,unsigned kind,uint16_t text,unsigned count,unsigned initial)
{
 (void)ctx;(void)state;(void)count;
#if ADVENTURE_DIAGNOSTIC
 (void)text;(void)initial;return kind==OW_CHOICE_YES_NO?1:0;
#else
 static const char *const yesno[]={"Yes","No"};
 static const char *const starters[]={"Charmander & Friends","Squirtle & Friends","Bulbasaur & Friends"};
 static const char *const aaron[]={"Lightning & Fire","Water & Fighting","Grass & Psychic"};
 static const char *const sam[]={"Normal duel","Practice","Rules","Exit"};
 static const char *const rules[]={"Energy","Attacking","Retreating","Evolution","Pokemon Powers","Ending your turn","Winning and losing","Exit"};
 static const char *const gifts[]={"Send cards","Receive cards","Send deck","Receive deck","Exit"};
 const char *const *items=yesno;int n=2;
 if(kind==OW_CHOICE_STARTER){items=starters;n=3;}
 if(kind==OW_CHOICE_AARON){items=aaron;n=3;}
 if(kind==OW_CHOICE_SAM){items=sam;n=4;}
 if(kind==OW_CHOICE_SAM_RULES){items=rules;n=8;}
 if(kind==OW_CHOICE_GIFT){items=gifts;n=5;}
 char title[256];format_story(text,title,sizeof(title),0);
 game_conversation(state->active_npc?state->dialog_npc:0);
 int result=n<=4?adventure_story_choice(title,items,n,kind==OW_CHOICE_YES_NO?(initial?0:1):(int)initial,kind!=OW_CHOICE_STARTER):n64_menu(title,items,n,(int)initial,true);
 game_conversation(0);
 if(result<0&&kind!=OW_CHOICE_YES_NO)result=kind==OW_CHOICE_SAM_RULES?7:kind==OW_CHOICE_GIFT?4:3;
 return kind==OW_CHOICE_YES_NO?(result==0):result;
#endif
}
static void duel_setup(TcgDuelSetup *setup,const OwDuel *duel)
{
 memset(setup,0,sizeof(*setup));
 setup->prizes=duel->prizes;setup->music=duel->music;
 setup->animations_disabled=game.animations_disabled;
 setup->skip_delay_allowed=game.skip_delay_allowed;
 setup->received_legendary_cards=ow_event(&game.ow,EVENT_RECEIVED_LEGENDARY_CARDS)!=0;
 memcpy(setup->rng,game.cards.rng,3);
 setup->duelist[0].kind=TCG_HUMAN;setup->duelist[0].name=game.name;
 memcpy(setup->duelist[0].cards,game.cards.built[game.cards.selected_deck].cards,60);
 setup->duelist[1].kind=TCG_AI;setup->duelist[1].deck_id=duel->deck;
 setup->duelist[1].name=ow_npc_name(duel->npc);
}
static int host_duel(void *ctx,OwState *state,const OwDuel *duel)
{
 (void)ctx;(void)state;
#if ADVENTURE_DIAGNOSTIC
 debugf("STORY duel=%u deck=%u prizes=%u\n",duel->npc,duel->deck,duel->prizes);return 1;
#else
 TcgDuelSetup setup;game_rng_to_cards();duel_setup(&setup,duel);
 if(!continue_duel)save_store_remove(SAVE_STORE_DUEL);
 uint8_t owner=game_profile;save_store_write("duelowner",&owner,1);
 game_save();
 adventure_duel_transition(true);
 rspq_wait();
 int result=n64_duel_play(&setup,continue_duel?saved_duel:NULL);continue_duel=false;
 n64_duel_rng(game.cards.rng);game_rng_to_story();
 duel_save_pending=true;
 loaded_map=-1; /* Restore projection/state after the CPU-drawn duel screen. */
 scene_begin_opaque();glEnable(GL_MULTISAMPLE_ARB);
 adventure_duel_transition(false);
 return result==0?1:0;
#endif
}

static void finish_save(void)
{
 if(duel_save_pending && game_save()){save_store_remove(SAVE_STORE_DUEL);save_store_remove("duelowner");duel_save_pending=false;}
}
static bool host_save(void *ctx,const OwState *state){(void)ctx;OwState old=game.ow;game.ow=*state;bool saved=game_save();game.ow=old;return saved;}
static void host_changed(void *ctx,OwState *state)
{
 (void)ctx;
 bool map_change=presentation.initialized&&presentation.map!=state->map;
 bool intro_arrival=presentation.map==OVERWORLD_MAP&&state->map==MASON_LABORATORY&&
  ow_event(state,EVENT_MASON_LAB_STATE)<MASON_LAB_RECEIVED_STARTER_DECK;
 bool fade=map_change&&session_active&&last_world_frame&&last_world_map==presentation.map&&!intro_arrival;
 if(fade) {
  /* Copy the completed old image before display_get can recycle its surface.
     The temporary is released before loading the destination room. */
  surface_t snapshot=n64_video_surface();
  if(snapshot.buffer) {
   copy_frame(&snapshot,last_world_frame);
   warp_fade(false,presentation.map,&snapshot);surface_free(&snapshot);
  } else fade=false;
 }
 unsigned changed=presentation_sync(&presentation,state,player_running);
 if(fade){warp_fade(true,state->map,NULL);return;}
 bool scripted=(changed&PRESENTATION_WORLD)||((changed&PRESENTATION_PLAYER)&&presentation_scripted_player(state));
 if(scripted){do{game_draw();}while(presentation_moving(&presentation));}
 else if(changed&PRESENTATION_MAP)game_draw();
}
static void host_wait(void *ctx,unsigned frames)
{
 (void)ctx;unsigned n=presentation_wait_frames(&presentation,frames);
 for(unsigned i=0;i<n;i++)game_draw();
}
static void host_sound(void *ctx,unsigned kind,unsigned id){(void)ctx;adventure_audio_event(kind,id);}
static void host_error(void *ctx,const char *message){(void)ctx;debugf("ADVENTURE ERROR: %s\n",message);
#if !ADVENTURE_DIAGNOSTIC
 n64_message("Game stopped","Reset the console to return to your diary.");
#endif
}
static unsigned host_count(void *ctx,unsigned card,bool decks){(void)ctx;return decks?gp_card_count(&game.cards,card):gp_collection_count(&game.cards,card);}
static unsigned host_total(void *ctx){(void)ctx;return gp_total_cards(&game.cards);}
static void host_card(void *ctx,unsigned card,int delta){(void)ctx;while(delta>0){gp_add_card(&game.cards,card);delta--;}while(delta<0){gp_remove_card(&game.cards,card);delta++;}}
static void host_booster(void *ctx,unsigned id){(void)ctx;
#if ADVENTURE_DIAGNOSTIC
 uint8_t cards[10];gp_open_booster(&game.cards,id,cards);
#else
 game_show_booster(id);
#endif
}
static void host_starter(void *ctx,unsigned choice){(void)ctx;game_rng_to_cards();gp_give_starter(&game.cards,choice);game_rng_to_story();}
static bool host_feature(void *ctx,OwState *state,unsigned feature,unsigned argument){(void)ctx;(void)state;return game_feature(feature,argument);}
const OwHost game_host={NULL,host_text,host_choice,host_duel,host_feature,host_changed,host_wait,host_sound,host_save,host_error,host_count,host_card,host_total,host_booster,host_starter};

bool game_feature(unsigned feature,unsigned argument)
{
 switch(feature) {
 case OW_FEATURE_PAUSE:adventure_audio_event(0x4a,0);adventure_audio_event(0,MUSIC_PAUSE_MENU);game_pause();adventure_audio_event(0x4b,0);return true;
 case OW_FEATURE_DECK_MACHINE:game_deck_machine(argument);return true;
 case OW_FEATURE_PC_PACK:gp_give_mail(&game.cards,argument);return true;
 case OW_FEATURE_CARD_RECEIVED:
  if(argument==255){const unsigned legendary[]={MOLTRES_LV37,ZAPDOS_LV68,ARTICUNO_LV37,DRAGONITE_LV41};for(unsigned i=0;i<4;i++)n64_card_detail(legendary[i]);}
  else n64_card_detail(argument?argument:game.ow.received_card);
  return true;
 case OW_FEATURE_MEDAL:adventure_show_medal(argument,&game.ow);return true;
 case OW_FEATURE_MAP_PATCH:loaded_map=-1;return true;
 case OW_FEATURE_BATTLE_CENTER:return game_link(4);
 case OW_FEATURE_GIFT_CENTER:return game_link(argument);
 case OW_FEATURE_CREDITS:game_credits();game_credits_finished=true;return true;
 case OW_FEATURE_PC:adventure_audio_event(0x4a,0);adventure_audio_event(0,MUSIC_PC_MAIN_MENU);game_pc();adventure_audio_event(0x4b,0);return true;
 case OW_FEATURE_CHALLENGE_MACHINE: {
  static const char *const choices[]={"Start challenge","Record","Exit"};
  int action=n64_menu("Challenge Machine",choices,3,0,true);
  if(action==1){char text[96];snprintf(text,sizeof(text),"Consecutive wins: %u\nRecord: %u",game.cards.challenge.streak,game.cards.challenge.record);n64_message("Record",text);return true;}
  if(action!=0)return true;
  game_rng_to_cards();gp_challenge_begin(&game.cards);game_rng_to_story();
  for(;;){uint8_t deck,prizes;if(!gp_challenge_opponent(&game.cards,&deck,&prizes))return false;
   unsigned opponent=0;
   if(deck>=LIGHTNING_AND_FIRE_DECK_ID&&deck<=GRASS_AND_PSYCHIC_DECK_ID)opponent=NPC_AARON;
   else for(unsigned id=1;id<NUM_NPCS;id++)if(ow_npc_headers[id].deck==deck){opponent=id;break;}
   OwDuel duel={opponent,prizes,deck,opponent?ow_npc_headers[opponent].music:0};game.duel_mode=1;game.ow.pending_duel=duel;game.ow.duel_pending=1;game.cards.challenge.in_duel=1;
   bool won=host_duel(NULL,&game.ow,&duel)==1;
   unsigned result=gp_challenge_result(&game.cards,won,game.link_name);game.ow.duel_pending=0;game.duel_mode=0;game_save();finish_save();
   if(!result||result==2)return true;
   static const char *const again[]={"Continue","Quit"};if(n64_menu("Challenge Machine",again,2,0,false)!=0){gp_challenge_quit(&game.cards,game.link_name);return true;}}
 }
 default:return false;
 }
}

int main(void)
{
 debug_init_isviewer();debug_init_usblog();
 /* Completed CPU UI canvases are copied through RDP before presentation.
    Resample-only scanout keeps 320-wide landmarks and text readable: the
    extra VI AA/divot/dedither chain smeared their narrow features. Coverage
    remains coherent because displayed buffers are never edited by the CPU. */
 display_init(n64_video_init(ADVENTURE_HIGH_RES!=0),DEPTH_16_BPP,3,GAMMA_NONE,ADVENTURE_VI_FILTER);
 display_set_fps_limit(30);dfs_init(DFS_DEFAULT_LOCATION);joypad_init();rdpq_init();gl_init();
 depth=n64_video_surface();scene_begin_opaque();
 glEnable(GL_MULTISAMPLE_ARB);glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);
 adventure_audio_init();scene_set_load_idle(adventure_audio_poll);
 n64_ui_init();actors=scene_open(W3_ACTOR_SCENE,W3_TEXTURE_ROOT);
#if ADVENTURE_DIAGNOSTIC == 24
 extern void adventure_pack_native_regression(void);
 adventure_pack_native_regression();
 for(;;)adventure_audio_poll();
#elif ADVENTURE_DIAGNOSTIC == 23
 adventure_audio_nested_regression();
 for(;;){}
#elif ADVENTURE_DIAGNOSTIC == 20
 extern void adventure_fps_survey(void);
 adventure_fps_survey();
 for(;;){}
#elif ADVENTURE_DIAGNOSTIC == 17
 adventure_audio_regression();
 for(;;){}
#elif ADVENTURE_DIAGNOSTIC == 14
 adventure_audio_review();
 for(;;)adventure_audio_poll();
#elif ADVENTURE_DIAGNOSTIC == 13
 presentation_init(&presentation,NULL,true);
 extern bool adventure_cinematic_diagnostic(void (*)(surface_t *,const char *));
 assertf(adventure_cinematic_diagnostic(conversation_capture),"Conversation opening failed");
 game_conversation(0);for(unsigned f=0;f<40;f++)game_draw();
 surface_t *exploration=game_render_frame();n64_video_present(exploration);
 conversation_capture(exploration,"sam-exploration");
 conversation_fixture(PSYCHIC_CLUB_LOBBY,NPC_RONALD1,14,10,WEST,"ronald-dialogue");
 conversation_fixture(PSYCHIC_CLUB_ENTRANCE,NPC_CLERK6,6,4,NORTH,"clerk-dialogue");
 exploration=game_render_frame();n64_video_present(exploration);
 conversation_capture(exploration,"clerk-exploration");
 debugf("VIDEO REVIEW PASS\n");for(;;)adventure_audio_poll();
#elif ADVENTURE_DIAGNOSTIC == 12
 extern void adventure_camera_preview(surface_t *,void (*)(surface_t *,const char *));
 adventure_camera_preview(&depth,capture);
 for(;;)adventure_audio_poll();
#elif ADVENTURE_DIAGNOSTIC == 18
 extern void adventure_model_options_preview(surface_t *,void (*)(surface_t *,const char *));
 adventure_model_options_preview(&depth,capture);
 for(;;)adventure_audio_poll();
#elif ADVENTURE_DIAGNOSTIC == 11
 extern void adventure_leads_preview(surface_t *,void (*)(surface_t *,const char *));
 adventure_leads_preview(&depth,capture);
 for(;;)adventure_audio_poll();
#elif ADVENTURE_DIAGNOSTIC == 10 || ADVENTURE_DIAGNOSTIC == 15
 static const unsigned review_maps[]={0,1,15,32};
 static const char *const review_names[]={"overworld","lab","lightning","dome"};
 for(unsigned i=0;i<4;i++) {
  unsigned map=review_maps[i];memset(&game,0,sizeof(game));
  game.ow.map=map;game.ow.x=w3_maps[map].width/2;game.ow.y=w3_maps[map].height-4;
  game.ow.direction=2;game.ow.overworld_selection=OWMAP_MASON_LABORATORY;
  for(const OwNpcDef *npc=ow_map_npcs[map];npc&&npc->id;npc++)if(npc->id!=255&&npc->x<w3_maps[map].width&&npc->y<w3_maps[map].height&&game.ow.npc_count<OW_NPC_LIMIT) {
   unsigned index=game.ow.npc_count++;
   game.ow.npcs[index]=(OwNpc){npc->id,npc->x,npc->y,npc->dir,1,ow_npc_headers[npc->id].sprite};
   game.ow.npc_flags[index]=ow_npc_headers[npc->id].flags;
   game.ow.npc_animation[index]=npc->id==NPC_AMY?SPRITE_ANIM_CGB_AMY_LAYING:ow_npc_headers[npc->id].animation;
  }
  debugf("VIDEO REVIEW %s BEGIN\n",review_names[i]);
  for(unsigned f=0;f<24;f++) {
   /* A fixed presentation pose compares raster resolution independently of
      a running gait, cursor bob, or camera interpolation. */
   presentation_init(&presentation,NULL,false);surface_t *frame=game_render_frame();
   if(f==4)capture(frame,review_names[i]);
   n64_video_present(frame);adventure_audio_poll();
  }
  heap_stats_t stats;sys_get_heap_stats(&stats);
  debugf("VIDEO HEAP stage=%s total=%d used=%d free=%d\n",review_names[i],stats.total,stats.used,stats.total-stats.used);
 }
 debugf("VIDEO REVIEW naming BEGIN\n");
 for(unsigned f=0;f<24;f++) {
  surface_t *frame=adventure_name_player_preview();if(f==4)capture(frame,"naming");
 }
 memset(&game,0,sizeof(game));game.ow.map=1;game.ow.x=14;game.ow.y=20;
 debugf("VIDEO REVIEW dialogue BEGIN\n");
 for(unsigned f=0;f<24;f++) {
  presentation_init(&presentation,NULL,false);
  surface_t *frame=adventure_story_message_preview("Dr. Mason","Welcome to the world of Pokemon cards!\nLet's learn how to play together.");
  if(f==4)capture(frame,"dialogue");
 }
#if ADVENTURE_DIAGNOSTIC == 10
 n64_ui_polish_preview();
#endif
 debugf("VIDEO REVIEW PASS\n");
 for(;;)adventure_audio_poll();
#elif ADVENTURE_DIAGNOSTIC == 9
 n64_ui_polish_preview();
 for(;;)adventure_audio_poll();
#elif ADVENTURE_DIAGNOSTIC == 8
 extern void adventure_face_preview(surface_t *,void (*)(surface_t *,const char *));
 adventure_face_preview(&depth,capture);
 for(;;)adventure_audio_poll();
#elif ADVENTURE_DIAGNOSTIC == 5 || ADVENTURE_DIAGNOSTIC == 6 || ADVENTURE_DIAGNOSTIC == 7
 extern void adventure_visual_sweep(surface_t *,Scene *,void (*)(surface_t *,const char *));
 adventure_visual_sweep(&depth,actors,capture);
 for(unsigned map=0;map<34;map++) {
  if(ADVENTURE_DIAGNOSTIC==6&&map!=0&&map!=1&&map!=12&&map!=15&&map!=32)continue;
  if(ADVENTURE_DIAGNOSTIC==7&&map!=15&&map!=32)continue;
  memset(&game,0,sizeof(game));game.ow.map=map;game.ow.x=w3_maps[map].width/2;game.ow.y=w3_maps[map].height-4;
  for(const OwNpcDef *npc=ow_map_npcs[map];npc&&npc->id;npc++)if(npc->id!=255&&npc->x<w3_maps[map].width&&npc->y<w3_maps[map].height&&game.ow.npc_count<OW_NPC_LIMIT) {
   unsigned index=game.ow.npc_count++;
   game.ow.npcs[index]=(OwNpc){npc->id,npc->x,npc->y,npc->dir,1,ow_npc_headers[npc->id].sprite};
   game.ow.npc_flags[index]=ow_npc_headers[npc->id].flags;
   game.ow.npc_animation[index]=npc->id==NPC_AMY?SPRITE_ANIM_CGB_AMY_LAYING:ow_npc_headers[npc->id].animation;
  }
  presentation_init(&presentation,NULL,false);debugf("VISUAL MAP %u BEGIN\n",map);
  const unsigned held=ADVENTURE_DIAGNOSTIC==5?2:24;
  for(unsigned f=0;f<held;f++) {
   capture_next=false;surface_t *frame=game_render_frame();
   char label[40];snprintf(label,sizeof(label),"Map %u: surface review",map);n64_map_label(frame,label);
   if(f==(ADVENTURE_DIAGNOSTIC==5?1:4))capture(frame,NULL);
   n64_video_present(frame);adventure_audio_poll();
  }
 }
 capture_next=false;surface_t *name_frame=adventure_name_player_preview();capture(name_frame,"naming");
 debugf("ADVENTURE VISUAL SWEEP PASS\n");
 for(;;)game_draw();
#elif ADVENTURE_DIAGNOSTIC == 4
 /* Hold the production island for reviewing the actual VI/display output.
    The ordinary framebuffer capture is retained as a separate reference. */
 memset(&game,0,sizeof(game));game.ow.overworld_selection=OWMAP_MASON_LABORATORY;
 presentation_init(&presentation,NULL,false);capture_next=true;
 for(unsigned f=0;;f++) {
  if(f&&f%900==0) {
   game.ow.overworld_selection=game.ow.overworld_selection==OWMAP_MASON_LABORATORY?
     OWMAP_POKEMON_DOME:OWMAP_MASON_LABORATORY;
   presentation_init(&presentation,NULL,false);
   debugf("OVERWORLD VI LOCATION %u\n",game.ow.overworld_selection);
  }
  game_draw();
  if(f==4)debugf("OVERWORLD VI READY filter=%u\n",(unsigned)ADVENTURE_VI_FILTER);
 }
#elif ADVENTURE_DIAGNOSTIC
 memset(&game,0,sizeof(game));presentation_init(&presentation,NULL,false);
 adventure_audio_event(0,MUSIC_OVERWORLD);adventure_audio_event(1,SFX_CURSOR);
 for(unsigned map=0;map<34;map++){
  game.ow.map=map;game.ow.x=w3_maps[map].width/2;game.ow.y=w3_maps[map].height-4;game.ow.npc_count=0;
  for(const OwNpcDef *npc=ow_map_npcs[map];npc&&npc->id;npc++)if(npc->id!=255&&npc->x<w3_maps[map].width&&npc->y<w3_maps[map].height&&game.ow.npc_count<OW_NPC_LIMIT) {
   unsigned index=game.ow.npc_count++;
   game.ow.npcs[index]=(OwNpc){npc->id,npc->x,npc->y,npc->dir,1,ow_npc_headers[npc->id].sprite};
   game.ow.npc_flags[index]=ow_npc_headers[npc->id].flags;
   game.ow.npc_animation[index]=npc->id==NPC_AMY?SPRITE_ANIM_CGB_AMY_LAYING:ow_npc_headers[npc->id].animation;
  }
  capture_next=ADVENTURE_DIAGNOSTIC!=2;game_draw();
  heap_stats_t stats;sys_get_heap_stats(&stats);debugf("MEMORY map=%u total=%d used=%d free=%d\n",map,stats.total,stats.used,stats.total-stats.used);
  debugf("MAP PASS %u\n",map);
 }
 assertf(adventure_intro_check(),"Opening animation source invariants failed");
 static const unsigned intro_times[]={64,64,80,70};
 static const char *const intro_labels[]={"intro-charizard","intro-scyther","intro-aerodactyl","title"};
 for(unsigned i=0;i<4;i++) {
  surface_t *intro=adventure_intro_preview(i,intro_times[i]);
  if(ADVENTURE_DIAGNOSTIC!=2)capture(intro,intro_labels[i]);
 }
 assertf(adventure_naming_check(),"Player naming rules failed");
 assertf(adventure_dialogue_check(),"Story dialogue rules failed");
 surface_t *naming=adventure_name_player_preview();if(ADVENTURE_DIAGNOSTIC!=2)capture(naming,"naming");
 debugf("ADVENTURE INTRO PASS\n");
 extern bool adventure_story_diagnostic(void);assertf(adventure_story_diagnostic(),"MIPS story campaign failed");
 TcgDuelSetup duel_test={0};duel_test.prizes=2;duel_test.animations_disabled=1;duel_test.skip_delay_allowed=1;duel_test.rng[0]=0x57;duel_test.rng[1]=0x31;
 duel_test.duelist[0].kind=TCG_AI;duel_test.duelist[0].deck_id=CHARMANDER_AND_FRIENDS_DECK_ID;duel_test.duelist[0].name="MARK";
#if ADVENTURE_DIAGNOSTIC == 3
 /* Request the human frontend screens while the diagnostic bot answers. */
 duel_test.duelist[0].kind=TCG_HUMAN;
#endif
 duel_test.duelist[1].kind=TCG_AI;duel_test.duelist[1].deck_id=SAMS_NORMAL_DECK_ID;duel_test.duelist[1].name="SAM";
 n64_duel_automated(true);int duel_result=n64_duel_play(&duel_test,NULL);n64_duel_automated(false);
 assertf(duel_result==0||duel_result==1,"Native duel did not finish");
 if(ADVENTURE_DIAGNOSTIC!=2)capture(n64_ui_frame(),"duel");
 debugf("ADVENTURE NATIVE DUEL PASS result=%d\n",duel_result);save_store_remove(SAVE_STORE_DUEL);
 const uint8_t seed[3]={1,2,3};memset(&game,0,sizeof(game));game.ow=(OwState){.version=OW_SAVE_VERSION,.rng=0x030201,.map=1,.x=14,.y=26};
 strcpy(game.name,"TEST");gp_init(&game.cards,seed);gp_give_starter(&game.cards,0);gp_name_encode(&game.cards,game.name,game.link_name);
 assertf(game_save(),"SRAM write failed");
 Game *peer=malloc(sizeof(*peer));assert(peer);*peer=game;strcpy(peer->name,"PEER");gp_name_encode(&peer->cards,peer->name,peer->link_name);
 assertf(game_save_peer(peer),"Two-diary atomic save failed");free(peer);
 extern void adventure_save_reopen(void);adventure_save_reopen();memset(&game,0,sizeof(game));
 assertf(game_load()&&!strcmp(game.name,"TEST")&&gp_total_cards(&game.cards)==90,"SRAM reload failed");
 debugf("ADVENTURE SRAM PASS\n");
 presentation_init(&presentation,NULL,true);
 extern bool adventure_cinematic_diagnostic(void (*)(surface_t *,const char *));
 assertf(adventure_cinematic_diagnostic(ADVENTURE_DIAGNOSTIC!=2?capture:NULL),"Opening choreography failed");
 debugf("ADVENTURE CINEMATIC PASS\n");
#if ADVENTURE_DIAGNOSTIC == 3
 /* Exercise the production fade and shutter paths on real RDP surfaces. */
 session_active=true;
 game.ow.map=14;game.ow.x=14;game.ow.y=22;game.ow.npc_count=0;
 host_changed(NULL,&game.ow);
 adventure_duel_transition(true);
 const uint16_t *closed=UncachedAddr(last_world_frame->buffer);
 assertf(!(closed[120*(last_world_frame->stride/2)+160]&0xfffe),"Duel shutter did not close");
 adventure_duel_transition(false);session_active=false;
 debugf("ADVENTURE WARP TRANSITION PASS\n");
#endif
 debugf("ADVENTURE MAP TOUR PASS\n");
#if ADVENTURE_DIAGNOSTIC >= 2
 for(unsigned cycle=0;;cycle++) {
#if ADVENTURE_DIAGNOSTIC == 3
  debugf("REVIEW VI title cycle=%u frames=900\n",cycle);
  for(unsigned f=0;f<900;f++)adventure_intro_preview(3,70);
#endif
  game.ow.map=15;game.ow.x=w3_maps[15].width/2;game.ow.y=w3_maps[15].height-4;
  game.ow.direction=2;game.ow.npc_count=0;
  unsigned held_frames=ADVENTURE_DIAGNOSTIC==3?900:90;
  debugf("VIDEO PHASE room cycle=%u frames=%u\n",cycle,held_frames);
  for(unsigned f=0;f<held_frames;f++)game_draw();
  n64_ui_video_probe(N64_VIDEO_DIALOGUE,90);
  n64_ui_video_probe(N64_VIDEO_DUEL,held_frames);
  n64_ui_video_probe(N64_VIDEO_CARD,held_frames);
  debugf("VIDEO CYCLE PASS %u\n",cycle);
 }
#else
 for(;;)game_draw();
#endif
#else
restart_title:
 session_active=false;game_credits_finished=false;last_world_frame=NULL;
 adventure_intro_play(!opening_shown);opening_shown=true;
 adventure_audio_event(0,MUSIC_PC_MAIN_MENU);
 bool loaded=false,new_game=false;
 for(;;) {
  uint8_t owner=0;
  bool pending=save_store_read("duelowner",&owner,1)==1&&owner>=1&&owner<=2&&save_store_read(SAVE_STORE_DUEL,saved_duel,sizeof(saved_duel))==TCG_SAVE_SIZE;
  static const char *const diaries[]={"Diary 1","Diary 2"};
  game_profile=pending?owner:(unsigned)n64_menu("Choose diary",diaries,2,0,false)+1;
  loaded=game_load();
  if(!loaded){new_game=true;break;}
  static const char *const title[]={"Continue","Card Pop!","New game","Other diary"};
  int selected=n64_menu("Pokemon Trading Card Game",title,4,0,false);
  if(selected==0)break;
  if(selected==1){adventure_audio_event(0x4a,0);adventure_audio_event(0,MUSIC_CARD_POP);game_card_pop();adventure_audio_event(0x4b,0);continue;}
  if(selected==2){static const char *const replace[]={"Keep diary","Start a new game"};if(n64_menu("Replace this diary?",replace,2,0,false)==1){new_game=true;break;}}
 }
 session_active=true;presentation_init(&presentation,NULL,true);player_running=false;
 if(new_game){save_store_remove(SAVE_STORE_DUEL);save_store_remove("duelowner");memset(&game,0,sizeof(game));game.text_speed=2;strcpy(game.name,"MARK");adventure_name_player(game.name,7);uint32_t seed=(uint32_t)get_ticks();uint8_t seeds[3]={seed,seed>>8,seed>>16};gp_init(&game.cards,seeds);gp_name_encode(&game.cards,game.name,game.link_name);seed=(uint32_t)game.cards.rng[0]|(uint32_t)game.cards.rng[1]<<8|(uint32_t)game.cards.rng[2]<<16;ow_new_game(&game.ow,&game_host,seed);}
 else {
  continue_duel=save_store_read(SAVE_STORE_DUEL,saved_duel,sizeof(saved_duel))==TCG_SAVE_SIZE;
  if(game.ow.duel_pending){int result=host_duel(NULL,&game.ow,&game.ow.pending_duel);
   if(game.duel_mode==1){unsigned next=gp_challenge_result(&game.cards,result==1,game.link_name);game.ow.duel_pending=0;game.duel_mode=0;finish_save();if(next==1)game_feature(OW_FEATURE_CHALLENGE_MACHINE,0);}
   else ow_finish_duel(&game.ow,&game_host,result);}
  else {continue_duel=false;ow_continue(&game.ow,&game_host);}
 }
 unsigned cooldown=0;
 for(;;) {
  joypad_poll();joypad_buttons_t pressed=joypad_get_buttons_pressed(JOYPAD_PORT_1),held=joypad_get_buttons_held(JOYPAD_PORT_1);joypad_inputs_t input=joypad_get_inputs(JOYPAD_PORT_1);
  if(pressed.start)game_feature(OW_FEATURE_PAUSE,0);
  if(!game.ow.error[0]) {
   if(game.ow.map==0) {
    unsigned selection=game.ow.overworld_selection;if(selection<1||selection>12)selection=1;
    if(!cooldown){int direction=(held.d_up||input.stick_y>35)?0:(held.d_right||input.stick_x>35)?1:(held.d_down||input.stick_y< -35)?2:(held.d_left||input.stick_x< -35)?3:-1;
     if(direction>=0&&ow_world_select(&game.ow,direction)){selection=game.ow.overworld_selection;cooldown=7;}}
    if(pressed.a){presentation_world_travel(&presentation,selection);while(presentation_moving(&presentation))game_draw();ow_enter_location(&game.ow,&game_host,selection);}
   } else {
    player_running=held.b;
    if(pressed.a){while(presentation_moving(&presentation))game_draw();ow_interact(&game.ow,&game_host);}
    if(!cooldown){int direction=(held.d_up||input.stick_y>35)?0:(held.d_right||input.stick_x>35)?1:(held.d_down||input.stick_y< -35)?2:(held.d_left||input.stick_x< -35)?3:-1;if(direction>=0&&ow_move(&game.ow,&game_host,direction))cooldown=held.b?4:8;}
   }
  }
  if(cooldown)cooldown--;
  finish_save();
  if(game_credits_finished)goto restart_title;
  game_draw();
 }
#endif
}
