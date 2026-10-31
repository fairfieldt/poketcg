/* Private actual-renderer shadow A/B. No production main/entry changes. */
#define main shadow_production_main
#define scene_draw_contact_shadows review_contacts
#define scene_draw_actor_presented review_actor
#include "../main.c"
#undef main
#undef scene_draw_contact_shadows
#undef scene_draw_actor_presented
#include <setjmp.h>
extern void scene_draw_contact_shadows(const float (*)[2],unsigned);
extern void scene_draw_actor_presented(const Scene *,unsigned,float,float,float,float,float,float,float,float);
int shadow_review_disable_depth;
static unsigned mode;
static bool hide_actors,force_contact;
static float contact[1][2];
static unsigned cases,missing_before,missing_after,depth_pixels;
static OwState mason_state,sam_state;
static jmp_buf source_done;

static void legacy_contacts(const float (*positions)[2],unsigned count)
{
 static const float ring[13][2]={{1,0},{.8660254f,.5f},{.5f,.8660254f},{0,1},{-.5f,.8660254f},{-.8660254f,.5f},{-1,0},{-.8660254f,-.5f},{-.5f,-.8660254f},{0,-1},{.5f,-.8660254f},{.8660254f,-.5f},{1,0}};
 glDisable(GL_TEXTURE_2D);glDepthFunc(GL_LESS);glDepthMask(GL_FALSE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
 for(unsigned i=0;i<count;i++){glBegin(GL_TRIANGLE_FAN);glColor4ub(16,22,32,64);glVertex3f(positions[i][0],.045f,positions[i][1]);glColor4ub(16,22,32,0);
  for(unsigned j=0;j<13;j++)glVertex3f(positions[i][0]+ring[j][0]*.56f,.045f,positions[i][1]+ring[j][1]*.40f);
  glEnd();}
 scene_begin_opaque();
}
void review_contacts(const float (*positions)[2],unsigned count)
{
 if(force_contact){positions=contact;count=1;}
 else if(hide_actors)count=count?1:0;
 if(mode==2)return;
 if(!mode||mode==4){if(mode==4)glDisable(GL_DEPTH_TEST);legacy_contacts(positions,count);}
 else{shadow_review_disable_depth=mode==3;scene_draw_contact_shadows(positions,count);}
}
void review_actor(const Scene *scene,unsigned model,float x,float y,float z,float heading,float scale,float lean,float gait,float tilt)
{if(!hide_actors)scene_draw_actor_presented(scene,model,x,y,z,heading,scale,lean,gait,tilt);}
static void source_text(void *ctx,OwState *s,uint16_t text,uint8_t npc)
{(void)ctx;(void)text;if(npc==NPC_DRMASON&&s->x==14&&s->y==8&&!mason_state.version)mason_state=*s;}
static int source_choice(void *ctx,OwState *s,unsigned kind,uint16_t text,unsigned n,unsigned initial)
{(void)ctx;(void)s;(void)text;(void)n;(void)initial;return kind==OW_CHOICE_SAM_RULES?7:1;}
static int source_duel(void *ctx,OwState *s,const OwDuel *duel)
{(void)ctx;assert(duel->npc==NPC_SAM);sam_state=*s;longjmp(source_done,1);}
static void source_states(void)
{
 OwHost host={.text=source_text,.choice=source_choice,.duel=source_duel};
 if(!setjmp(source_done))ow_new_game(&game.ow,&host,123);
 assert(mason_state.version&&sam_state.version&&!game.ow.error[0]);
}
static void fixture(unsigned map,unsigned x,unsigned z)
{
 memset(&game,0,sizeof(game));strcpy(game.name,"Mark");game.text_speed=2;
 ow_set_event(&game.ow,EVENT_MASON_LAB_STATE,MASON_LAB_RECEIVED_STARTER_DECK);
 ow_set_event(&game.ow,EVENT_JOSHUA_STATE,JOSHUA_DEFEATED);
 ow_load_map(&game.ow,NULL,map,x,z,NORTH);assert(!game.ow.error[0]);
 presentation_init(&presentation,&game.ow,false);for(unsigned f=0;f<40;f++)presentation_camera_step(&presentation,&game.ow,&w3_maps[map]);
}
static surface_t *render(const Presentation *pose,unsigned variant)
{presentation=*pose;mode=variant;surface_t *frame=game_render_frame();assert(glGetError()==GL_NO_ERROR);return frame;}
static void presented(surface_t *frame){n64_video_present(frame);adventure_audio_poll();}
static void snapshot(const char *name,const Presentation *pose,unsigned variant)
{
 force_contact=hide_actors=false;surface_t *frame=render(pose,variant);surface_t still=n64_video_surface();copy_frame(&still,frame);
 /* Present before slow ISViewer output: the VI must not keep scanning the
    preceding actor-hidden reference while the screenshot gate reads pixels. */
 presented(frame);debugf("VIDEO REVIEW %s BEGIN\n",name);capture(frame,name);
 for(unsigned f=0;f<35;f++){surface_t *copy=display_get();copy_frame(copy,&still);presented(copy);}surface_free(&still);
}
int main(void)
{
 debug_init_isviewer();debug_init_usblog();timer_init();
 display_init(n64_video_init(get_memory_size()>=8*1024*1024),DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);
 dfs_init(DFS_DEFAULT_LOCATION);joypad_init();rdpq_init();gl_init();depth=n64_video_surface();scene_begin_opaque();glEnable(GL_MULTISAMPLE_ARB);glDisable(GL_CULL_FACE);glDisable(GL_LIGHTING);
 adventure_audio_init();n64_ui_init();actors=scene_open(W3_ACTOR_SCENE,W3_TEXTURE_ROOT);source_states();
 unsigned pixels=n64_video_width*n64_video_height;
 uint16_t *none=malloc(pixels*2),*before=malloc(pixels*2),*after=malloc(pixels*2),*zcopy=malloc(pixels*2);assert(none&&before&&after&&zcopy);
 static const struct {unsigned map,x,z;const char *name;bool intro;} tests[]={
  {MASON_LABORATORY,14,20,"lab-walk",false},{FIGHTING_CLUB_LOBBY,18,16,"lobby",false},
  {FIRE_CLUB,14,20,"fire-carpet",false},{LIGHTNING_CLUB,14,22,"lightning-runner",false},{WATER_CLUB,20,18,"water-deck",false},
  {MASON_LABORATORY,14,26,"intro-entry",true},{MASON_LABORATORY,14,18,"intro-mid",true},{MASON_LABORATORY,14,10,"intro-arrival",true}};
 for(unsigned stage=0;stage<sizeof(tests)/sizeof(*tests);stage++){
  fixture(tests[stage].map,tests[stage].x,tests[stage].z);
  if(tests[stage].intro){game.ow=mason_state;game.ow.y=tests[stage].z;presentation_init(&presentation,&game.ow,true);}
  game.ow.y-=2;presentation_sync(&presentation,&game.ow,tests[stage].intro);OwState canonical=game.ow;
  for(unsigned f=0;f<8;f++){
   Presentation pose=presentation;Presentation advanced;
   for(unsigned actor=0;actor<(tests[stage].intro?3:1);actor++){
   force_contact=actor!=0;hide_actors=true;
   if(actor){const PresentationPose *npc=presentation_npc(&pose,actor==1?NPC_DRMASON:NPC_SAM);assert(npc);contact[0][0]=npc->x;contact[0][1]=npc->z;}
   surface_t *frame=render(&pose,2);advanced=presentation;memcpy(none,UncachedAddr(frame->buffer),pixels*2);memcpy(zcopy,UncachedAddr(depth.buffer),pixels*2);presented(frame);
   frame=render(&pose,0);memcpy(before,UncachedAddr(frame->buffer),pixels*2);assert(!memcmp(zcopy,UncachedAddr(depth.buffer),pixels*2));presented(frame);
   frame=render(&pose,1);memcpy(after,UncachedAddr(frame->buffer),pixels*2);assert(!memcmp(zcopy,UncachedAddr(depth.buffer),pixels*2));presented(frame);
   frame=render(&pose,3);unsigned total=0,old=0,new=0,old_total=0;
   const uint16_t *reference=UncachedAddr(frame->buffer);
   for(unsigned i=0;i<pixels;i++)if(reference[i]!=none[i]){total++;if(after[i]==none[i])new++;}
   presented(frame);frame=render(&pose,4);reference=UncachedAddr(frame->buffer);
   for(unsigned i=0;i<pixels;i++)if(reference[i]!=none[i]){old_total++;if(before[i]==none[i])old++;}
   debugf("SHADOW COVERAGE case=%s frame=%u actor=%u expected=%u old_expected=%u missing_before=%u missing_after=%u\n",tests[stage].name,f,actor,total,old_total,old,new);
   assertf((actor||total>3)&&new==0,"Coplanar native shadow lost floor pixels");missing_before+=old;missing_after+=new;cases++;depth_pixels+=pixels;presented(frame);
   }
   if(stage==7&&(f==0||f==3||f==7)){
    char label[32];snprintf(label,sizeof(label),"walk-%u-before",f);snapshot(label,&pose,0);
    snprintf(label,sizeof(label),"walk-%u-after",f);snapshot(label,&pose,1);
   }
   assert(!memcmp(&game.ow,&canonical,sizeof(canonical)));presentation=advanced;
  }
 }
 /* A shadow wholly under Sam's tabletop must stay hidden by actual geometry. */
 fixture(MASON_LABORATORY,14,18);Presentation pose=presentation;force_contact=hide_actors=true;contact[0][0]=7;contact[0][1]=15;
 surface_t *frame=render(&pose,2);memcpy(none,UncachedAddr(frame->buffer),pixels*2);presented(frame);
 frame=render(&pose,1);assertf(!memcmp(none,UncachedAddr(frame->buffer),pixels*2),"Contact decal leaked through table");presented(frame);
 /* Prove the actor pass after shadows keeps the exact same Z contents. */
 game.ow=mason_state;presentation_init(&presentation,&game.ow,true);game_conversation(NPC_DRMASON);
 for(unsigned f=0;f<40;f++)presentation_camera_step(&presentation,&game.ow,&w3_maps[MASON_LABORATORY]);
 pose=presentation;
 force_contact=hide_actors=false;frame=render(&pose,2);memcpy(zcopy,UncachedAddr(depth.buffer),pixels*2);presented(frame);
 frame=render(&pose,1);assertf(!memcmp(zcopy,UncachedAddr(depth.buffer),pixels*2),"Shadow changed the later actor depth pass");presented(frame);
 snapshot("lab-before",&pose,0);snapshot("lab-after",&pose,1);
 game.ow=sam_state;presentation_init(&presentation,&game.ow,true);game_conversation(NPC_SAM);
 for(unsigned f=0;f<40;f++)presentation_camera_step(&presentation,&game.ow,&w3_maps[MASON_LABORATORY]);
 pose=presentation;
 snapshot("sam-before",&pose,0);snapshot("sam-after",&pose,1);
 debugf("GROUND SHADOW PASS width=%u cases=%u missing_before=%u missing_after=%u exact_depth_pixels=%u occlusion=1 actors=1\n",n64_video_width,cases,missing_before,missing_after,depth_pixels);
 debugf("VIDEO REVIEW PASS\n");for(;;)adventure_audio_poll();
}
