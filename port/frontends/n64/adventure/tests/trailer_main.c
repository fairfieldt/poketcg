/* Private capture ROM only. Production initialization, rendering and hosts are
 * included verbatim; this entry replaces the interactive title/diary loop. */
#define main trailer_production_main
#ifndef TRAILER_MAIN_SOURCE
#define TRAILER_MAIN_SOURCE "../main.c"
#endif
#include TRAILER_MAIN_SOURCE
#undef main
#include <setjmp.h>
#include "trailer_ronald.h"
#include "trailer_amy.h"
#include "../duel_input.h"
#include "engine.h"
#ifndef TRAILER_SEGMENT
#define TRAILER_SEGMENT 0
#endif
#ifndef TRAILER_PREROLL_VI
#define TRAILER_PREROLL_VI 480
#endif
#ifndef TRAILER_ACK_VI
#define TRAILER_ACK_VI 180
#endif

enum {DRIVER_NONE,DRIVER_TEXT,DRIVER_CHOICE,DRIVER_MEDAL,DRIVER_DUEL,DRIVER_TITLE};
static const char *const segment_names[]={"intro","ronald","clubs","ending","amy"};
static volatile unsigned trailer_vi,driver_start,driver_phase,driver_nav_steps,driver_nav_bit;
static volatile uint16_t virtual_held,virtual_pressed;
static bool begun,first_frame,ended,inside_title;
static jmp_buf segment_stop;
static OwHost trailer_host;
static unsigned duel_request_count;
static TcgHost duel_host,duel_proxy;
static bool ronald_ko,ronald_prize;
static bool saw_world,logged_run;
static unsigned world_x,world_y,world_selection;

static unsigned long long now_us(void){return (unsigned long long)TICKS_TO_US(get_ticks());}
static void marker(const char *kind,const char *name)
{debugf("TRAILER %s %s vi=%u us=%llu\n",kind,name,trailer_vi,now_us());}
static void phase(unsigned value)
{
 disable_interrupts();driver_phase=value;driver_start=trailer_vi;virtual_held=virtual_pressed=0;
 driver_nav_steps=driver_nav_bit=0;enable_interrupts();
}
static bool pulse(unsigned age,unsigned at,unsigned length)
{return age>=at&&age<at+length;}
static void input_tick(void)
{
 trailer_vi++;assertf(trailer_vi<60*240,"Private trailer segment exceeded240 seconds");
 unsigned age=trailer_vi-driver_start;uint16_t held=0;
 if(driver_phase==DRIVER_TEXT&&age>=TRAILER_ACK_VI&&age%TRAILER_ACK_VI<2)held=INPUT_A;
 if(driver_phase==DRIVER_CHOICE){
  for(unsigned i=0;i<driver_nav_steps;i++)if(pulse(age,120+i*12,4))held=(uint16_t)driver_nav_bit;
  if(age>=240&&(age-240)%150<2)held=INPUT_A;
 }
 if(driver_phase==DRIVER_MEDAL&&age>=270&&(age-270)%60<2)held=INPUT_A;
 if(driver_phase==DRIVER_DUEL){
  if(duel_request_count==1){if(pulse(age,84,2))held=INPUT_DOWN;if(pulse(age,114,2))held=INPUT_A;}
  else if(age>=90&&(age-90)%90<2)held=INPUT_A;
 }
 virtual_pressed|=held&~virtual_held;virtual_held=held;
}
static joypad_buttons_t buttons(uint16_t mask)
{
 joypad_buttons_t value={0};value.a=!!(mask&INPUT_A);value.b=!!(mask&INPUT_B);value.start=!!(mask&INPUT_START);
 value.d_up=!!(mask&INPUT_UP);value.d_down=!!(mask&INPUT_DOWN);value.d_left=!!(mask&INPUT_LEFT);value.d_right=!!(mask&INPUT_RIGHT);return value;
}
bool __wrap_joypad_is_connected(joypad_port_t port){return port==JOYPAD_PORT_1;}
joypad_inputs_t __wrap_joypad_get_inputs(joypad_port_t port)
{joypad_inputs_t value={0};if(port==JOYPAD_PORT_1)value.btn=buttons(virtual_held);return value;}
joypad_buttons_t __wrap_joypad_get_buttons_held(joypad_port_t port)
{return buttons(port==JOYPAD_PORT_1?virtual_held:0);}
joypad_buttons_t __wrap_joypad_get_buttons_pressed(joypad_port_t port)
{
 disable_interrupts();uint16_t pending=port==JOYPAD_PORT_1?virtual_pressed:0;if(port==JOYPAD_PORT_1)virtual_pressed=0;enable_interrupts();return buttons(pending);
}
extern void __real_n64_video_present(surface_t *frame);
void __wrap_n64_video_present(surface_t *frame)
{
 __real_n64_video_present(frame);
 if(begun&&!first_frame){first_frame=true;marker("FIRST_FRAME",segment_names[TRAILER_SEGMENT]);}
 if(inside_title&&!ended&&trailer_vi-driver_start>=420){ended=true;marker("END",segment_names[TRAILER_SEGMENT]);}
}

static void black_frames(unsigned vi)
{
 unsigned start=trailer_vi;
 while(trailer_vi-start<vi){surface_t *frame=display_get();rdpq_attach(frame,NULL);rdpq_set_mode_fill(RGBA32(0,0,0,255));
  rdpq_fill_rectangle(0,0,n64_video_width,n64_video_height);rdpq_detach_wait();n64_video_present(frame);adventure_audio_poll();}
}
static void hold_world(unsigned vi)
{unsigned start=trailer_vi;while(trailer_vi-start<vi)game_draw();}
static void hold_ui(unsigned vi)
{
 surface_t still=n64_video_surface();const surface_t *source=n64_ui_frame();assert(source);copy_frame(&still,source);
 unsigned start=trailer_vi;while(trailer_vi-start<vi){surface_t *frame=display_get();copy_frame(frame,&still);n64_video_present(frame);adventure_audio_poll();}
 surface_free(&still);
}
static void text_host(void *ctx,OwState *state,uint16_t text,uint8_t npc)
{
 debugf("TRAILER TEXT id=%u npc=%u map=%u x=%u y=%u vi=%u us=%llu\n",text,npc,state->map,state->x,state->y,trailer_vi,now_us());
 if(TRAILER_SEGMENT==0&&text==1511){
  assertf(state->x==10&&state->y==14,"First table arrival missed source player position");
  marker("SHOT","mark-at-first-table");hold_world(60);longjmp(segment_stop,4);
 }
 phase(DRIVER_TEXT);host_text(ctx,state,text,npc);phase(DRIVER_NONE);
}
static void changed_host(void *ctx,OwState *state)
{
 if(TRAILER_SEGMENT==0&&state->map==OVERWORLD_MAP){
  if(saw_world&&!logged_run&&(state->x!=world_x||state->y!=world_y||state->overworld_selection!=world_selection)){
   logged_run=true;marker("INTRO_RUN","overworld-source-movement");
  }
  saw_world=true;world_x=state->x;world_y=state->y;world_selection=state->overworld_selection;
 }
 host_changed(ctx,state);
}
static int choice_host(void *ctx,OwState *state,unsigned kind,uint16_t text,unsigned count,unsigned initial)
{
 debugf("TRAILER CHOICE kind=%u text=%u count=%u initial=%u vi=%u us=%llu\n",kind,text,count,initial,trailer_vi,now_us());
 phase(DRIVER_CHOICE);
 if(kind==OW_CHOICE_SAM_RULES){driver_nav_steps=7;driver_nav_bit=INPUT_DOWN;}
 else if(kind==OW_CHOICE_YES_NO&&!initial){driver_nav_steps=1;driver_nav_bit=INPUT_UP;}
 int answer=host_choice(ctx,state,kind,text,count,initial);phase(DRIVER_NONE);
 debugf("TRAILER CHOICE_DONE kind=%u answer=%d vi=%u us=%llu\n",kind,answer,trailer_vi,now_us());return answer;
}
static int first_duel(void *ctx,OwState *state,const OwDuel *duel)
{
 (void)ctx;assertf(TRAILER_SEGMENT==0&&duel->npc==NPC_SAM&&duel->deck==SAMS_PRACTICE_DECK_ID,"Opening missed source Sam practice duel");
 assertf(state->x==10&&state->y==14,"Opening source player missed Sam table");marker("SHOT","sam-ready");longjmp(segment_stop,1);
}
static bool feature_host(void *ctx,OwState *state,unsigned feature,unsigned argument)
{
 if(feature==OW_FEATURE_MEDAL){assertf(ow_event(state,argument),"Medal was not recorded before presentation");marker("SHOT","water-medal");phase(DRIVER_MEDAL);}
 bool result=host_feature(ctx,state,feature,argument);
 if(feature==OW_FEATURE_MEDAL){phase(DRIVER_NONE);marker("SHOT","water-medal-complete");}
 return result;
}
static void booster_stop(void *ctx,unsigned booster)
{(void)ctx;assertf((TRAILER_SEGMENT==3||TRAILER_SEGMENT==4)&&booster==BOOSTER_LABORATORY_WATER,"Unexpected reward pack");marker("SHOT","amy-reward-before-packs");longjmp(segment_stop,2);}
static void reset_game(bool tutorial)
{
 memset(&game,0,sizeof(game));strcpy(game.name,"Mark");game.text_speed=2;game_profile=1;
 const uint8_t seed[]={0x57,0x31,0x29};gp_init(&game.cards,seed);gp_name_encode(&game.cards,game.name,game.link_name);
 game.ow.version=OW_SAVE_VERSION;game.ow.rng=0x293157;
 if(!tutorial){ow_set_event(&game.ow,EVENT_MASON_LAB_STATE,MASON_LAB_RECEIVED_STARTER_DECK);ow_set_event(&game.ow,EVENT_JOSHUA_STATE,JOSHUA_DEFEATED);gp_give_starter(&game.cards,1);}
 presentation_init(&presentation,NULL,tutorial);player_running=false;session_active=true;last_world_frame=NULL;
 trailer_host=game_host;trailer_host.text=text_host;trailer_host.choice=choice_host;trailer_host.feature=feature_host;trailer_host.changed=changed_host;
 trailer_host.save=NULL;
}
static void load_map(unsigned map,unsigned x,unsigned y)
{
 game_conversation(0);ow_load_map(&game.ow,&trailer_host,map,x,y,NORTH);assertf(!game.ow.error[0],"Private map preload: %s",game.ow.error);
 marker("SHOT",ow_maps[map].name);hold_world(90);
}
static void walk_to(unsigned target_x,unsigned target_y)
{
 enum {LIMIT=2048};static int16_t previous[LIMIT],cells[64][64];static uint8_t direction[LIMIT],x[LIMIT],y[LIMIT],route[LIMIT];
 memset(cells,0xff,sizeof(cells));unsigned count=1,found=0;previous[0]=-1;x[0]=game.ow.x;y[0]=game.ow.y;cells[y[0]][x[0]]=0;
 for(unsigned read=0;read<count&&!found;read++)for(unsigned d=0;d<4;d++){
  OwState probe=game.ow;probe.x=x[read];probe.y=y[read];
  if(!ow_move(&probe,NULL,d)||probe.map!=game.ow.map||probe.error[0]||probe.x>=64||probe.y>=64||cells[probe.y][probe.x]>=0)continue;
  assertf(count<LIMIT,"Private walk search overflow");unsigned n=count++;x[n]=probe.x;y[n]=probe.y;previous[n]=(int16_t)read;direction[n]=(uint8_t)d;cells[y[n]][x[n]]=(int16_t)n;
  if(probe.x==target_x&&probe.y==target_y){found=n;break;}
 }
 if(!found&&((unsigned)game.ow.x!=target_x||(unsigned)game.ow.y!=target_y)){
  unsigned best=~0u;
  for(unsigned n=0;n<count;n++){unsigned distance=(unsigned)(abs((int)x[n]-(int)target_x)+abs((int)y[n]-(int)target_y));if(distance<best){best=distance;found=n;}}
  assertf(best<=6&&found,"No legal trailer approach in map%u to%u,%u",game.ow.map,target_x,target_y);
 }
 unsigned steps=0;for(unsigned n=found;n;n=(unsigned)previous[n])route[steps++]=direction[n];
 debugf("TRAILER WALK map=%u steps=%u requested=%u,%u reached=%u,%u vi=%u us=%llu\n",game.ow.map,steps,target_x,target_y,x[found],y[found],trailer_vi,now_us());
 while(steps){assertf(ow_move(&game.ow,&trailer_host,route[--steps]),"Legal trailer walk became blocked");for(unsigned f=0;f<8;f++)game_draw();}
 while(presentation_moving(&presentation))game_draw();
 debugf("TRAILER WALK_COMPLETE map=%u x=%u y=%u vi=%u us=%llu\n",game.ow.map,game.ow.x,game.ow.y,trailer_vi,now_us());
 hold_world(90);
}
static void intro_segment(void)
{
 reset_game(true);trailer_host.duel=first_duel;
 int stopped=setjmp(segment_stop);
 if(!stopped)ow_new_game(&game.ow,&trailer_host,0x293157);
 assertf(!game.ow.error[0],"Source opening failed: %s",game.ow.error);
 if(stopped==4)return;
 game_conversation(NPC_SAM);hold_world(120);game_conversation(0);
}
static void clubs_segment(void)
{
 static const unsigned maps[]={LIGHTNING_CLUB,FIRE_CLUB,WATER_CLUB};
 static const unsigned leaders[]={NPC_ISAAC,NPC_KEN,NPC_AMY};reset_game(false);
 for(unsigned i=0;i<3;i++){
  const W3Map *map=&w3_maps[maps[i]];load_map(maps[i],map->width/2,map->height-4);
  const OwNpc *leader=NULL;for(unsigned n=0;n<game.ow.npc_count;n++)if(game.ow.npcs[n].id==leaders[i])leader=&game.ow.npcs[n];
  assertf(leader,"Club leader missing");walk_to(leader->x,leader->y+4);
 }
}

static void duel_event(void *ctx,const TcgEvent *event)
{
 if(event->type==TCG_EV_TEXT)debugf("TRAILER DUEL_TEXT id=%u vi=%u us=%llu\n",event->text,trailer_vi,now_us());
 if(event->type==TCG_EV_DAMAGE&&event->arg[0]==TCG_OPPONENT&&event->arg[1]==0&&hWhoseTurn==TCG_PLAYER){
  unsigned damage=event->arg[2]|event->arg[3]<<8,hp=GetNonTurnDuelistVariable(DUELVARS_ARENA_CARD_HP);if(hp&&damage>=hp){ronald_ko=true;marker("SHOT",TRAILER_SEGMENT==4?"amy-knockout":"ronald-knockout");}
 }
 duel_host.event(ctx,event);
}
static int duel_request(void *ctx,const TcgRequest *request)
{
 static const unsigned types[]={TCG_REQ_DUEL_MENU,TCG_REQ_MENU,TCG_REQ_ACK,TCG_REQ_ACK,TCG_REQ_ACK,TCG_REQ_PRIZE,TCG_REQ_ACK,TCG_REQ_ACK,TCG_REQ_ACK};
 static const unsigned amy_types[]={TCG_REQ_DUEL_MENU,TCG_REQ_MENU,TCG_REQ_ACK,TCG_REQ_ACK,TCG_REQ_ACK,TCG_REQ_ACK,TCG_REQ_ACK,TCG_REQ_PRIZE,TCG_REQ_ACK,TCG_REQ_ACK,TCG_REQ_ACK,TCG_REQ_ACK};
 bool amy=TRAILER_SEGMENT==4;
 assertf(duel_request_count<(amy?12:9)&&request->type==(amy?amy_types:types)[duel_request_count],"Trailer continuation changed request%u type%u",duel_request_count,request->type);duel_request_count++;
 debugf("TRAILER REQUEST index=%u type=%u menu=%u owner=%u shown=%u vi=%u us=%llu\n",duel_request_count,request->type,request->menu,request->duelist,request->shown,trailer_vi,now_us());
 phase(DRIVER_DUEL);int answer=duel_host.request(ctx,request);phase(DRIVER_NONE);
 unsigned expected=duel_request_count==1?1:amy&&duel_request_count==8?3:0;
 assertf(answer==(int)expected,"Controller chose wrong trailer answer%u:%d",duel_request_count,answer);
 if(request->type==TCG_REQ_PRIZE){assertf(ronald_ko,"Prize preceded real KO");ronald_prize=true;marker("SHOT",amy?"amy-prize":"ronald-prize");}
 if(!amy&&duel_request_count==9){assertf(ronald_ko&&ronald_prize,"Legal Ronald sequence incomplete");marker("SHOT","ronald-turn");hold_ui(120);longjmp(segment_stop,3);}
 return answer;
}
extern void __real_tcg_set_host(const TcgHost *host);
void __wrap_tcg_set_host(const TcgHost *host)
{
 if((TRAILER_SEGMENT!=1&&TRAILER_SEGMENT!=4)||!host||!host->request){__real_tcg_set_host(host);return;}
 duel_host=*host;duel_proxy=*host;duel_proxy.event=duel_event;duel_proxy.request=duel_request;__real_tcg_set_host(&duel_proxy);
}
static void ronald_segment(void)
{
 reset_game(false);TcgDuelSetup setup;static uint8_t checkpoint[TCG_SAVE_SIZE];
 assertf(trailer_ronald_load(&setup,checkpoint),"Legal Ronald checkpoint unavailable");setup.duelist[0].name="Mark";setup.duelist[1].name="Ronald";
 marker("SHOT","ronald-board");if(!setjmp(segment_stop))n64_duel_play(&setup,checkpoint);
}
extern void __real_adventure_intro_play(bool opening);
static void ending_segment(void)
{
 reset_game(false);trailer_host.booster=booster_stop;load_map(WATER_CLUB,20,6);
 game.ow.duel_npc=NPC_AMY;game.ow.duel_pending=1;marker("SHOT","amy-after-declared-private-win");
 if(!setjmp(segment_stop))ow_finish_duel(&game.ow,&trailer_host,1);
 assertf(ow_event(&game.ow,EVENT_BEAT_AMY)&&ow_event(&game.ow,EVENT_MEDAL_COUNT)==1,"Source Amy reward failed");
 /* A private progression warp for the final environment shot, not a claim
    that this one declared win completed the campaign. */
 load_map(HALL_OF_HONOR,12,22);walk_to(12,16);
 marker("SHOT","title64");inside_title=true;phase(DRIVER_TITLE);__real_adventure_intro_play(false);
}
static void amy_segment(void)
{
 reset_game(false);trailer_host.booster=booster_stop;load_map(WATER_CLUB,20,6);
 TcgDuelSetup setup;static uint8_t checkpoint[TCG_SAVE_SIZE];
 assertf(trailer_amy_load(&setup,checkpoint),"Legal Amy checkpoint unavailable");
 setup.duelist[0].name="Mark";setup.duelist[1].name="Amy";
 marker("SHOT","amy-board");int result=n64_duel_play(&setup,checkpoint);
 assertf(result==0&&duel_request_count==12&&ronald_ko&&ronald_prize,"Amy did not finish with the legal player win");
 marker("SHOT","amy-won");loaded_map=-1;scene_begin_opaque();glEnable(GL_MULTISAMPLE_ARB);
 game.ow.duel_npc=NPC_AMY;game.ow.duel_pending=1;
 if(!setjmp(segment_stop))ow_finish_duel(&game.ow,&trailer_host,1);
 assertf(ow_event(&game.ow,EVENT_BEAT_AMY)&&ow_event(&game.ow,EVENT_MEDAL_COUNT)==1,"Actual Amy win did not award Water Medal");
 load_map(HALL_OF_HONOR,12,22);walk_to(12,16);
 marker("SHOT","title64");inside_title=true;phase(DRIVER_TITLE);__real_adventure_intro_play(false);
}
void __wrap_adventure_intro_play(bool opening)
{
 if(begun){__real_adventure_intro_play(opening);return;}
 register_VI_handler(input_tick);marker("READY",segment_names[TRAILER_SEGMENT]);black_frames(TRAILER_PREROLL_VI);
 begun=true;marker("BEGIN",segment_names[TRAILER_SEGMENT]);
 if(TRAILER_SEGMENT==0)intro_segment();else if(TRAILER_SEGMENT==1)ronald_segment();else if(TRAILER_SEGMENT==2)clubs_segment();else if(TRAILER_SEGMENT==4)amy_segment();else ending_segment();
 ended=true;marker("END",segment_names[TRAILER_SEGMENT]);phase(DRIVER_NONE);
 for(;;){if(TRAILER_SEGMENT==1)hold_ui(120);else hold_world(120);}
}
int main(void){assert(TRAILER_SEGMENT<5);return trailer_production_main();}
