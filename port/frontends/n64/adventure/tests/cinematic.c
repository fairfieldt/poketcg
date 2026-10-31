#include <libdragon.h>
#include <setjmp.h>
#include <string.h>
#include "../game.h"
#include "../dialogue.h"
#include "gen/constants.h"

/* Exercise the actual source opening with the production movement/wait host.
   Stop when Sam's practice duel is requested; the separate native AI test
   exercises the duel engine. Only text acknowledgements are automated here. */
static jmp_buf at_practice;
static void (*snapshot)(surface_t *,const char *);
static bool mason_seen,sam_seen;
static uint16_t last_text;
static void text(void *ctx,OwState *state,uint16_t id,uint8_t npc)
{
 (void)ctx;(void)npc;last_text=id;
 if(state->map==MASON_LABORATORY&&state->x==14&&state->y==8&&!mason_seen) {
  mason_seen=true;
  game_conversation(NPC_DRMASON);
  for(unsigned i=0;i<(ADVENTURE_DIAGNOSTIC==13?40:12);i++)game_draw();
  char formatted[8192];game_story_format(id,formatted,sizeof(formatted));
  surface_t *frame=adventure_story_message_preview("Dr. Mason",formatted);
  if(snapshot)snapshot(frame,"mason-dialogue");
  game_conversation(0);
 }
}
static int choice(void *ctx,OwState *state,unsigned kind,uint16_t id,unsigned count,unsigned initial)
{(void)ctx;(void)state;(void)id;(void)count;(void)initial;return kind==OW_CHOICE_SAM_RULES?7:1;}
static bool feature(void *ctx,OwState *state,unsigned id,unsigned argument)
{(void)ctx;(void)state;(void)id;(void)argument;return true;}
static int duel(void *ctx,OwState *state,const OwDuel *duel)
{
 (void)ctx;
 assertf(duel->npc==NPC_SAM&&duel->deck==SAMS_PRACTICE_DECK_ID,"Opening did not reach Sam's practice duel");
 assertf(state->x==10&&state->y==14,"Player did not reach Sam's table");
 game_conversation(NPC_SAM);
 for(unsigned i=0;i<(ADVENTURE_DIAGNOSTIC==13?40:12);i++)game_draw();
 char formatted[8192];game_story_format(last_text,formatted,sizeof(formatted));
 surface_t *frame=adventure_story_message_preview("Sam",formatted);
 if(snapshot)snapshot(frame,"sam-table");
 game_conversation(0);
 sam_seen=true;longjmp(at_practice,1);
}
bool adventure_cinematic_diagnostic(void (*capture_frame)(surface_t *,const char *))
{
 snapshot=capture_frame;mason_seen=sam_seen=false;last_text=0;
 memset(&game,0,sizeof(game));strcpy(game.name,"MARK");game.text_speed=2;
 OwHost host=game_host;host.text=text;host.choice=choice;host.feature=feature;host.duel=duel;host.save=NULL;
 if(!setjmp(at_practice))ow_new_game(&game.ow,&host,123);
 return mason_seen&&sam_seen&&!game.ow.error[0];
}
