/* VI-rate edges survive slow rendered frames and are consumed only once. */
#include "../duel_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
 for(unsigned render_period=1;render_period<=12;render_period++) {
  for(unsigned at=0;at<render_period;at++) {
   DuelInputCapture capture={0};
   for(unsigned tick=0;tick<render_period;tick++)
    duel_input_sample(&capture,0,tick==at?INPUT_A:0,true);
   assert(duel_input_take(&capture,0)==INPUT_A);
   assert(duel_input_take(&capture,0)==0);
   /* Continued held input cannot answer the next page. */
   duel_input_sample(&capture,0,INPUT_A,true);duel_input_take(&capture,0);
   for(unsigned tick=0;tick<20;tick++) {
    duel_input_sample(&capture,0,INPUT_A,true);
    assert(duel_input_take(&capture,0)==0);
   }
   duel_input_sample(&capture,0,0,true);
   duel_input_sample(&capture,0,INPUT_A,true);
   assert(duel_input_take(&capture,0)==INPUT_A);
  }
 }
 DuelInputCapture capture={0};
 duel_input_sample(&capture,0,INPUT_UP|INPUT_A,true);
 duel_input_sample(&capture,1,INPUT_B|INPUT_START,true);
 duel_input_sample(&capture,0,0,true);duel_input_sample(&capture,1,0,true);
 assert(duel_input_take(&capture,0)==(INPUT_UP|INPUT_A));
 assert(duel_input_take(&capture,1)==(INPUT_B|INPUT_START));
 duel_input_sample(&capture,1,INPUT_B,true);duel_input_sample(&capture,1,0,false);
 assert(!capture.connected[1]&&duel_input_take(&capture,1)==0);
 puts("INPUT CAPTURE PASS: one-VI taps across1..12-VI redraws, release/hold/freshpress, two independent controllers, unplug reset");
 return 0;
}
