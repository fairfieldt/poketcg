#ifndef ADVENTURE_PACK_FLOW_H
#define ADVENTURE_PACK_FLOW_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "../duel_input.h"
enum {PACK_SEALED,PACK_CHECK,PACK_OPENING,PACK_LIST,PACK_DONE};
enum {PACK_OPEN_FRAMES=10,PACK_CARD_COUNT=10};
typedef struct {uint8_t cards[PACK_CARD_COUNT];unsigned phase,index,frame;bool requested;} PackFlow;
static inline void pack_flow_init(PackFlow *flow,const uint8_t cards[PACK_CARD_COUNT])
{*flow=(PackFlow){0};memcpy(flow->cards,cards,PACK_CARD_COUNT);}
/* Return true only when the visible composition changes. Edge inputs have
   already been consumed once by the VI-rate capture shared with the duel. */
static inline bool pack_flow_input(PackFlow *flow,uint16_t pressed)
{
 unsigned phase=flow->phase,index=flow->index;
 switch(flow->phase) {
 case PACK_SEALED:if(pressed&(INPUT_A|INPUT_B|INPUT_START))flow->requested=true;break;
 case PACK_CHECK:if(pressed&(INPUT_A|INPUT_B|INPUT_START)){flow->phase=PACK_OPENING;flow->frame=0;}break;
 case PACK_OPENING:if(pressed&(INPUT_A|INPUT_B|INPUT_START))flow->phase=PACK_LIST;break;
 case PACK_LIST:
  if(pressed&INPUT_B)flow->phase=PACK_DONE;
  else if(pressed&INPUT_UP)flow->index=(flow->index+PACK_CARD_COUNT-1)%PACK_CARD_COUNT;
  else if(pressed&INPUT_DOWN)flow->index=(flow->index+1)%PACK_CARD_COUNT;
  break;
 default:break;
 }
 return phase!=flow->phase||index!=flow->index;
}
static inline bool pack_flow_step(PackFlow *flow)
{
 if(flow->phase!=PACK_OPENING)return false;
 if(++flow->frame>=PACK_OPEN_FRAMES){flow->phase=PACK_LIST;flow->frame=0;return true;}
 return false;
}
static inline bool pack_flow_ready(PackFlow *flow,bool fanfare_finished)
{
 if(flow->phase!=PACK_SEALED||!flow->requested||!fanfare_finished)return false;
 flow->phase=PACK_CHECK;return true;
}
static inline unsigned pack_set(unsigned booster)
{return booster<7?0:booster<14?1:booster<20?2:booster<25?3:0;}
#endif
