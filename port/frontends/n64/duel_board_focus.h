#ifndef TCGN64_DUEL_BOARD_FOCUS_H
#define TCGN64_DUEL_BOARD_FOCUS_H
#include <stdbool.h>
#include <stdint.h>

/* Presentation coordinates only. Bottom/top are physical screen sides,
   independent of the answering controller. Bits0..5 mean Active, Bench1..5. */
enum DuelBoardRow {DUEL_BOARD_ACTIONS,DUEL_BOARD_BOTTOM_BENCH,
 DUEL_BOARD_BOTTOM_ACTIVE,DUEL_BOARD_TOP_ACTIVE,DUEL_BOARD_TOP_BENCH};
enum DuelBoardDirection {DUEL_BOARD_UP,DUEL_BOARD_DOWN,DUEL_BOARD_LEFT,
 DUEL_BOARD_RIGHT,DUEL_BOARD_BACK};
typedef struct {
 uint16_t lane;
 uint8_t action,saved_action,row,slot;
} DuelBoardFocus;

static inline unsigned duel_board_action_x(unsigned action)
{return 59u+(action/2u)*96u;}
static inline unsigned duel_board_slot_x(unsigned slot)
{return 44u+(slot-1u)*58u;}
static inline bool duel_board_is_bench(unsigned row)
{return row==DUEL_BOARD_BOTTOM_BENCH||row==DUEL_BOARD_TOP_BENCH;}
static inline uint8_t duel_board_row_mask(unsigned row,uint8_t bottom,uint8_t top)
{
 uint8_t side=row<=DUEL_BOARD_BOTTOM_ACTIVE?bottom:top;
 if(row<DUEL_BOARD_BOTTOM_BENCH||row>DUEL_BOARD_TOP_BENCH)return 0;
 return side&(duel_board_is_bench(row)?0x3e:1);
}
static inline unsigned duel_board_distance(unsigned a,unsigned b)
{return a>b?a-b:b-a;}
static inline unsigned duel_board_nearest_slot(uint8_t mask,unsigned lane)
{
 unsigned best=0,distance=UINT16_MAX;
 for(unsigned slot=1;slot<=5;slot++)if(mask&(1u<<slot)) {
  unsigned next=duel_board_distance(duel_board_slot_x(slot),lane);
  if(next<distance){distance=next;best=slot;}
 }
 return best;
}
static inline void duel_board_select_row(DuelBoardFocus *focus,unsigned row,uint8_t bottom,uint8_t top)
{
 focus->row=(uint8_t)row;
 focus->slot=(uint8_t)(duel_board_is_bench(row)?
  duel_board_nearest_slot(duel_board_row_mask(row,bottom,top),focus->lane):0);
 /* Automatic nearest-card fallback retains the desired lane. Crossing a
    sparse opposite bench and coming back therefore restores the same column. */
}
static inline bool duel_board_focus_changed(const DuelBoardFocus *a,const DuelBoardFocus *b)
{
 return a->lane!=b->lane||a->action!=b->action||a->saved_action!=b->saved_action||
  a->row!=b->row||a->slot!=b->slot;
}
static inline void duel_board_focus_init(DuelBoardFocus *focus,unsigned action)
{
 if(action>=6)action=0;
 *focus=(DuelBoardFocus){.lane=(uint16_t)duel_board_action_x(action),
  .action=(uint8_t)action,.saved_action=(uint8_t)action};
}

/* Repair a vanished target after a nested view. The caller can use the same
   occupied masks it builds with get_slot; this helper never reads engine RAM. */
static inline bool duel_board_focus_refresh(DuelBoardFocus *focus,uint8_t bottom,uint8_t top)
{
 DuelBoardFocus before=*focus;
 if(focus->row==DUEL_BOARD_ACTIONS)return false;
 uint8_t mask=duel_board_row_mask(focus->row,bottom,top);
 if(mask&(1u<<focus->slot))return false;
 if(mask)duel_board_select_row(focus,focus->row,bottom,top);
 else {
  bool found=false;
  for(unsigned distance=1;distance<DUEL_BOARD_TOP_BENCH&&!found;distance++) {
   if(focus->row>distance) {
    unsigned row=focus->row-distance;
    if(duel_board_row_mask(row,bottom,top)){duel_board_select_row(focus,row,bottom,top);found=true;}
   }
   if(!found&&focus->row+distance<=DUEL_BOARD_TOP_BENCH) {
    unsigned row=focus->row+distance;
    if(duel_board_row_mask(row,bottom,top)){duel_board_select_row(focus,row,bottom,top);found=true;}
   }
  }
  if(!found){focus->row=DUEL_BOARD_ACTIONS;focus->slot=0;focus->action=focus->saved_action;}
 }
 return duel_board_focus_changed(&before,focus);
}

/* Only directions and B/back are accepted. A/START inspection and eventual
   engine command answers belong to the caller's existing request loop. */
static inline bool duel_board_focus_move(DuelBoardFocus *focus,enum DuelBoardDirection direction,
 uint8_t bottom,uint8_t top)
{
 DuelBoardFocus before=*focus;duel_board_focus_refresh(focus,bottom,top);
 if(focus->row==DUEL_BOARD_ACTIONS) {
  if(direction==DUEL_BOARD_LEFT)focus->action=(uint8_t)((focus->action+4)%6);
  else if(direction==DUEL_BOARD_RIGHT)focus->action=(uint8_t)((focus->action+2)%6);
  else if(direction==DUEL_BOARD_DOWN)focus->action^=1;
  else if(direction==DUEL_BOARD_UP) {
   if(focus->action&1)focus->action--;
   else {
    focus->saved_action=focus->action;focus->lane=(uint16_t)duel_board_action_x(focus->action);
    for(unsigned row=DUEL_BOARD_BOTTOM_BENCH;row<=DUEL_BOARD_TOP_BENCH;row++)
     if(duel_board_row_mask(row,bottom,top)){duel_board_select_row(focus,row,bottom,top);break;}
   }
  }
  if(focus->row==DUEL_BOARD_ACTIONS)focus->lane=(uint16_t)duel_board_action_x(focus->action);
 } else if(direction==DUEL_BOARD_BACK) {
  focus->row=DUEL_BOARD_ACTIONS;focus->slot=0;focus->action=focus->saved_action;
  focus->lane=(uint16_t)duel_board_action_x(focus->action);
 } else if(direction==DUEL_BOARD_UP) {
  for(unsigned row=focus->row+1;row<=DUEL_BOARD_TOP_BENCH;row++)
   if(duel_board_row_mask(row,bottom,top)){duel_board_select_row(focus,row,bottom,top);break;}
 } else if(direction==DUEL_BOARD_DOWN) {
  bool found=false;
  for(unsigned row=focus->row-1;row>=DUEL_BOARD_BOTTOM_BENCH;row--)
   if(duel_board_row_mask(row,bottom,top)){duel_board_select_row(focus,row,bottom,top);found=true;break;}
  if(!found) {
   unsigned best=0,distance=UINT16_MAX;
   for(unsigned column=0;column<3;column++) {
    unsigned next=duel_board_distance(duel_board_action_x(column*2),focus->lane);
    if(next<distance){distance=next;best=column*2;}
   }
   focus->row=DUEL_BOARD_ACTIONS;focus->slot=0;focus->action=(uint8_t)best;
   focus->lane=(uint16_t)duel_board_action_x(best);
  }
 } else if(duel_board_is_bench(focus->row)) {
  uint8_t mask=duel_board_row_mask(focus->row,bottom,top);
  if(direction==DUEL_BOARD_LEFT) {
   for(unsigned slot=focus->slot-1;slot>=1;slot--)if(mask&(1u<<slot)) {
    focus->slot=(uint8_t)slot;focus->lane=(uint16_t)duel_board_slot_x(slot);break;
   }
  } else if(direction==DUEL_BOARD_RIGHT) {
   for(unsigned slot=focus->slot+1;slot<=5;slot++)if(mask&(1u<<slot)) {
    focus->slot=(uint8_t)slot;focus->lane=(uint16_t)duel_board_slot_x(slot);break;
   }
  }
 }
 return duel_board_focus_changed(&before,focus);
}

/* side0 is the bottom/TCG_PLAYER area; side1 is the top/TCG_OPPONENT area. */
static inline bool duel_board_focus_target(const DuelBoardFocus *focus,unsigned *side,unsigned *slot)
{
 if(focus->row<DUEL_BOARD_BOTTOM_BENCH||focus->row>DUEL_BOARD_TOP_BENCH)return false;
 if(side)*side=focus->row>DUEL_BOARD_BOTTOM_ACTIVE;
 if(slot)*slot=focus->slot;
 return true;
}
#endif
