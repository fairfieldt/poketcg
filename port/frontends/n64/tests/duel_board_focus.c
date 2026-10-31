#include "../duel_board_focus.h"
#include <assert.h>
#include <stdio.h>

static unsigned checked;
static void valid(const DuelBoardFocus *focus,uint8_t bottom,uint8_t top)
{
 unsigned side=99,slot=99;
 assert(focus->action<6&&focus->saved_action<6);
 if(duel_board_focus_target(focus,&side,&slot)) {
  assert(side<2&&slot<6&&((side?top:bottom)&(1u<<slot)));
  assert((focus->row==DUEL_BOARD_BOTTOM_BENCH||focus->row==DUEL_BOARD_TOP_BENCH)==(slot!=0));
 }
 checked++;
}
static void action_grid(void)
{
 for(unsigned action=0;action<6;action++) {
  DuelBoardFocus focus;duel_board_focus_init(&focus,action);valid(&focus,0,0);
  assert(!duel_board_focus_target(&focus,NULL,NULL));
  duel_board_focus_move(&focus,DUEL_BOARD_LEFT,0,0);assert(focus.action==(action+4)%6);
  duel_board_focus_move(&focus,DUEL_BOARD_RIGHT,0,0);assert(focus.action==action);
  duel_board_focus_move(&focus,DUEL_BOARD_DOWN,0,0);assert(focus.action==(action^1));
  duel_board_focus_move(&focus,DUEL_BOARD_DOWN,0,0);assert(focus.action==action);
  assert(!duel_board_focus_move(&focus,DUEL_BOARD_BACK,0,0));
  duel_board_focus_move(&focus,DUEL_BOARD_UP,0,0);
  assert(focus.row==DUEL_BOARD_ACTIONS&&focus.action==(action&~1u));
 }
 DuelBoardFocus focus;duel_board_focus_init(&focus,100);assert(focus.action==0);
}
static void all_public_slots(void)
{
 DuelBoardFocus focus;duel_board_focus_init(&focus,0);uint8_t visited[2]={0};
 duel_board_focus_move(&focus,DUEL_BOARD_UP,63,63);
 assert(focus.row==DUEL_BOARD_BOTTOM_BENCH&&focus.slot==1);
 assert(!duel_board_focus_move(&focus,DUEL_BOARD_LEFT,63,63));
 for(unsigned slot=1;slot<=5;slot++) {
  unsigned side,target;assert(duel_board_focus_target(&focus,&side,&target));
  assert(side==0&&target==slot);visited[side]|=(uint8_t)(1u<<target);valid(&focus,63,63);
  if(slot<5)duel_board_focus_move(&focus,DUEL_BOARD_RIGHT,63,63);
 }
 assert(!duel_board_focus_move(&focus,DUEL_BOARD_RIGHT,63,63));
 for(unsigned side=0;side<2;side++) {
  duel_board_focus_move(&focus,DUEL_BOARD_UP,63,63);unsigned actual,target;
  assert(duel_board_focus_target(&focus,&actual,&target)&&actual==side&&target==0);
  visited[actual]|=1;valid(&focus,63,63);
  assert(!duel_board_focus_move(&focus,DUEL_BOARD_LEFT,63,63));
  assert(!duel_board_focus_move(&focus,DUEL_BOARD_RIGHT,63,63));
 }
 duel_board_focus_move(&focus,DUEL_BOARD_UP,63,63);assert(focus.row==DUEL_BOARD_TOP_BENCH&&focus.slot==5);
 assert(!duel_board_focus_move(&focus,DUEL_BOARD_UP,63,63));
 for(unsigned slot=5;slot>=1;slot--) {
  unsigned side,target;assert(duel_board_focus_target(&focus,&side,&target));
  assert(side==1&&target==slot);visited[side]|=(uint8_t)(1u<<target);valid(&focus,63,63);
  if(slot>1)duel_board_focus_move(&focus,DUEL_BOARD_LEFT,63,63);
 }
 assert(visited[0]==63&&visited[1]==63);
 assert(duel_board_focus_move(&focus,DUEL_BOARD_BACK,63,63)&&focus.row==DUEL_BOARD_ACTIONS&&focus.action==0);
}
static void masks(void)
{
 /* Every legal Active/Bench occupancy on both sides, including holes and
    absent Actives. Spatial Up/Down must never land on an empty card and must
    preserve the action lane when no horizontal change was requested. */
 for(unsigned bottom=0;bottom<64;bottom++)for(unsigned top=0;top<64;top++)for(unsigned column=0;column<3;column++) {
  DuelBoardFocus focus;duel_board_focus_init(&focus,column*2);
  duel_board_focus_move(&focus,DUEL_BOARD_UP,(uint8_t)bottom,(uint8_t)top);valid(&focus,(uint8_t)bottom,(uint8_t)top);
  if(!(bottom|top)){assert(focus.row==DUEL_BOARD_ACTIONS);continue;}
  unsigned first=focus.row;
  for(unsigned step=0;step<4;step++) {
   unsigned previous=focus.row;duel_board_focus_move(&focus,DUEL_BOARD_UP,(uint8_t)bottom,(uint8_t)top);
   assert(focus.row>=previous);valid(&focus,(uint8_t)bottom,(uint8_t)top);
  }
  assert(focus.row>=first);
  DuelBoardFocus back=focus;
  duel_board_focus_move(&back,DUEL_BOARD_LEFT,(uint8_t)bottom,(uint8_t)top);
  duel_board_focus_move(&back,DUEL_BOARD_BACK,(uint8_t)bottom,(uint8_t)top);
  assert(back.row==DUEL_BOARD_ACTIONS&&back.action==column*2);
  for(unsigned step=0;step<4&&focus.row;step++) {
   unsigned previous=focus.row;duel_board_focus_move(&focus,DUEL_BOARD_DOWN,(uint8_t)bottom,(uint8_t)top);
   assert(focus.row<previous);valid(&focus,(uint8_t)bottom,(uint8_t)top);
  }
  assert(focus.row==DUEL_BOARD_ACTIONS&&focus.action==column*2);
 }
}
static void sparse_lane(void)
{
 DuelBoardFocus focus;duel_board_focus_init(&focus,4);
 duel_board_focus_move(&focus,DUEL_BOARD_UP,63,3);assert(focus.slot==5);
 for(unsigned i=0;i<3;i++)duel_board_focus_move(&focus,DUEL_BOARD_UP,63,3);
 assert(focus.row==DUEL_BOARD_TOP_BENCH&&focus.slot==1);
 for(unsigned i=0;i<3;i++)duel_board_focus_move(&focus,DUEL_BOARD_DOWN,63,3);
 assert(focus.row==DUEL_BOARD_BOTTOM_BENCH&&focus.slot==5);
 /* An explicit horizontal move sets a new lane, including after visiting a
    sparse opposite bench, while B still restores the saved action cursor. */
 duel_board_focus_move(&focus,DUEL_BOARD_LEFT,63,3);assert(focus.slot==4);
 for(unsigned i=0;i<3;i++)duel_board_focus_move(&focus,DUEL_BOARD_UP,63,3);
 for(unsigned i=0;i<3;i++)duel_board_focus_move(&focus,DUEL_BOARD_DOWN,63,3);
 assert(focus.slot==4);duel_board_focus_move(&focus,DUEL_BOARD_DOWN,63,3);assert(focus.action==4);
 duel_board_focus_init(&focus,2);duel_board_focus_move(&focus,DUEL_BOARD_UP,63,63);
 duel_board_focus_move(&focus,DUEL_BOARD_LEFT,63,63);duel_board_focus_move(&focus,DUEL_BOARD_DOWN,63,63);
 assert(focus.row==DUEL_BOARD_ACTIONS&&focus.action==0); /* Bench2 is nearer the left action column. */
}
static void disappearing_target(void)
{
 DuelBoardFocus focus;duel_board_focus_init(&focus,2);duel_board_focus_move(&focus,DUEL_BOARD_UP,63,63);
 assert(focus.slot==3);assert(duel_board_focus_refresh(&focus,3,63)&&focus.slot==1);valid(&focus,3,63);
 assert(duel_board_focus_refresh(&focus,1,63)&&focus.row==DUEL_BOARD_BOTTOM_ACTIVE);valid(&focus,1,63);
 assert(duel_board_focus_refresh(&focus,0,0)&&focus.row==DUEL_BOARD_ACTIONS&&focus.action==2);
 assert(!duel_board_focus_move(&focus,DUEL_BOARD_UP,0xc0,0x80));
}
int main(void)
{
 action_grid();all_public_slots();masks();sparse_lane();disappearing_target();
 printf("board focus: PASS %u valid states, all 4096 occupancy pairs / 3 action columns, all 12 public slots, sparse lanes/clamps/B/grid/refresh\n",checked);
 return 0;
}
