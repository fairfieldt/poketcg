#ifndef ADVENTURE_MEDAL_FLOW_H
#define ADVENTURE_MEDAL_FLOW_H
#include <stdbool.h>
#include <stdint.h>
#include "../duel_input.h"
#include "gen/constants.h"
enum {MEDAL_COUNT=8,MEDAL_CELEBRATION_VI=225};
typedef struct {unsigned index;uint8_t owned;bool requested;DuelFreshConfirm confirm;} MedalFlow;
static inline bool medal_flow_init(MedalFlow *flow,unsigned event,unsigned flags)
{
 if(event<EVENT_BEAT_NIKKI||event>EVENT_BEAT_MITCH)return false;
 *flow=(MedalFlow){.index=event-EVENT_BEAT_NIKKI,.owned=(uint8_t)flags};return true;
}
static inline bool medal_owned(const MedalFlow *flow,unsigned index)
{return index<MEDAL_COUNT&&(flow->owned&(0x80u>>index));}
static inline bool medal_flow_input(MedalFlow *flow,unsigned elapsed_vi,uint16_t pressed,uint16_t held,bool song_finished)
{
 if(elapsed_vi<MEDAL_CELEBRATION_VI)return false;
 if(!duel_fresh_confirm_step(&flow->confirm,(held&(INPUT_A|INPUT_B))!=0))return false;
 if(pressed&(INPUT_A|INPUT_B))flow->requested=true;
 return flow->requested&&song_finished;
}
static inline bool medal_highlight(unsigned elapsed_vi)
{return elapsed_vi>=MEDAL_CELEBRATION_VI||!(elapsed_vi&16);}
static inline int medal_grid_x(unsigned index){return 34+(int)(index%4)*64;}
static inline int medal_grid_y(unsigned index){return 146+(int)(index/4)*34;}
#endif
