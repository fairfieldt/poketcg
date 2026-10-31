#include "../pack_flow.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
 const uint8_t cards[10]={10,11,12,13,14,15,16,17,18,1};PackFlow flow;
 pack_flow_init(&flow,cards);DuelInputCapture input={0};
 duel_input_sample(&input,0,INPUT_A,true);
 assert(!pack_flow_input(&flow,duel_input_take(&input,0))&&flow.phase==PACK_SEALED&&flow.requested);
 assert(!pack_flow_ready(&flow,false));
 assert(pack_flow_ready(&flow,true)&&flow.phase==PACK_CHECK);
 /* The receipt acknowledgement cannot also acknowledge checking the pack. */
 duel_input_sample(&input,0,INPUT_A,true);assert(!pack_flow_input(&flow,duel_input_take(&input,0)));
 assert(flow.phase==PACK_CHECK);
 duel_input_sample(&input,0,0,true);duel_input_sample(&input,0,INPUT_A,true);
 assert(pack_flow_input(&flow,duel_input_take(&input,0))&&flow.phase==PACK_OPENING);
 for(unsigned i=0;i<PACK_OPEN_FRAMES;i++) {
  duel_input_sample(&input,0,INPUT_A,true);assert(!pack_flow_input(&flow,duel_input_take(&input,0)));
  pack_flow_step(&flow);
 }
 assert(flow.phase==PACK_LIST&&flow.index==0);
 /* Holding the open button must not inspect a card. */
 duel_input_sample(&input,0,INPUT_A,true);pack_flow_input(&flow,duel_input_take(&input,0));assert(flow.index==0);
 for(unsigned i=1;i<10;i++) {
  duel_input_sample(&input,0,0,true);duel_input_sample(&input,0,INPUT_DOWN,true);
  assert(pack_flow_input(&flow,duel_input_take(&input,0)));assert(flow.index==i&&flow.cards[i]==cards[i]);
 }
 assert(flow.phase==PACK_LIST&&flow.index==9);
 assert(pack_flow_input(&flow,INPUT_DOWN)&&flow.index==0);
 assert(pack_flow_input(&flow,INPUT_UP)&&flow.index==9);
 assert(!pack_flow_input(&flow,INPUT_A)); /* inspection is caller-owned */
 assert(pack_flow_input(&flow,INPUT_B)&&flow.phase==PACK_DONE);
 pack_flow_init(&flow,cards);pack_flow_input(&flow,INPUT_A);pack_flow_ready(&flow,true);
 pack_flow_input(&flow,INPUT_A);assert(flow.phase==PACK_OPENING);
 pack_flow_input(&flow,INPUT_START);assert(flow.phase==PACK_LIST); /* skippable transition */
 assert(!memcmp(flow.cards,cards,sizeof(cards)));
 for(unsigned i=0;i<29;i++)assert(pack_set(i)==(i<7?0:i<14?1:i<20?2:i<25?3:0));
 puts("pack flow: PASS exact ten-card order, fanfare gate, fresh presses, skippable opening/list/inspect/exit and all29 source set mappings");
 return 0;
}
