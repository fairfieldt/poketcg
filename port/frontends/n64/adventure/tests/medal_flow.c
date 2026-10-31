#include "../medal_flow.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
 unsigned cases=0;
 for(unsigned index=0;index<8;index++)for(unsigned flags=0;flags<256;flags++){
  MedalFlow flow;assert(medal_flow_init(&flow,EVENT_BEAT_NIKKI+index,flags));
  assert(flow.index==index&&flow.owned==flags&&!flow.requested);
  for(unsigned i=0;i<8;i++)assert(medal_owned(&flow,i)==!!(flags&(0x80u>>i)));
  assert(!medal_owned(&flow,8)&&!medal_owned(&flow,100));
  int x=medal_grid_x(index),y=medal_grid_y(index);
  assert(x>=8&&x+60<=312&&y>=140&&y+30<=210);
  for(unsigned width=320;width<=480;width+=160){int scale=width/160;
   assert((x*scale)/2>=8*scale/2&&((x+60)*scale)/2<(int)width);
   assert(((y+30)*scale)/2<240*scale/2);}
  cases++;
 }
 MedalFlow flow={.index=99,.owned=55,.requested=true},before=flow;
 assert(!medal_flow_init(&flow,EVENT_BEAT_NIKKI-1,255)&&!memcmp(&flow,&before,sizeof(flow)));
 assert(!medal_flow_init(&flow,EVENT_BEAT_MITCH+1,255)&&!memcmp(&flow,&before,sizeof(flow)));
 for(unsigned button=INPUT_A;button<=INPUT_B;button++){
  assert(medal_flow_init(&flow,EVENT_BEAT_AMY,0x10));DuelInputCapture input={0};
  /* Entry captures and discards the preceding dialogue's held button. */
  duel_input_sample(&input,0,(uint16_t)button,true);input.pending[0]=0;
  for(unsigned vi=0;vi<=MEDAL_CELEBRATION_VI+3;vi++){
   duel_input_sample(&input,0,(uint16_t)button,true);
   assert(!medal_flow_input(&flow,vi,duel_input_take(&input,0),(uint16_t)button,true));assert(!flow.requested);
  }
  duel_input_sample(&input,0,0,true);assert(!medal_flow_input(&flow,MEDAL_CELEBRATION_VI+4,duel_input_take(&input,0),0,false));
  duel_input_sample(&input,0,(uint16_t)button,true);
  assert(!medal_flow_input(&flow,MEDAL_CELEBRATION_VI+5,duel_input_take(&input,0),(uint16_t)button,false)&&flow.requested);
  assert(medal_flow_input(&flow,MEDAL_CELEBRATION_VI+6,0,(uint16_t)button,true));cases++;
 }
 assert(medal_highlight(0)&&!medal_highlight(16)&&medal_highlight(32)&&medal_highlight(MEDAL_CELEBRATION_VI));
 puts("MEDAL FLOW PASS:2048 event/collection/layout cases, source225VI timing, fresh A/B and fanfare-before-resume gates");
 assert(cases==2050);return 0;
}
