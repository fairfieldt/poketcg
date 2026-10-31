#include "../frame_rate.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
 const uint64_t frequency=46875000;
 FrameRate rate={0};
 assert(!frame_rate_present(&rate,0,frequency));
 /* The first call starts the window; 15 intervals, not 16 frames, is 30Hz. */
 for(unsigned i=1;i<=15;i++){
  frame_rate_present(&rate,i*frequency/30,frequency);
  if(i<15)assert(!rate.ready);
 }
 assert(rate.ready&&rate.fps==30);
 for(unsigned i=1;i<=8;i++)frame_rate_present(&rate,frequency/2+i*frequency/15,frequency);
 assert(rate.fps==15);
 /* Repeated VI output does not call the presentation seam or count frames. */
 unsigned saved=rate.fps;uint32_t intervals=rate.intervals;
 for(unsigned vi=0;vi<60;vi++){assert(rate.fps==saved);assert(rate.intervals==intervals);}
 /* Actual timer may already be beyond its 32-bit rollover. */
 rate=(FrameRate){0};uint64_t start=UINT64_C(0xfffffff0);
 frame_rate_present(&rate,start,frequency);
 for(unsigned i=1;i<=30;i++)frame_rate_present(&rate,start+i*frequency/60,frequency);
 assert(rate.fps==60);
 rate=(FrameRate){0};frame_rate_present(&rate,0,frequency);
 for(unsigned i=1;i<=10;i++)frame_rate_present(&rate,i*frequency/20,frequency);
 assert(rate.fps==20);
 /* A blocking operation really lowers the rate, followed by recovery. */
 frame_rate_present(&rate,frequency*5/2,frequency);assert(rate.fps==1);
 for(unsigned i=1;i<=15;i++)frame_rate_present(&rate,frequency*5/2+i*frequency/30,frequency);
 assert(rate.fps==30);
 puts("frame rate: 30/15/60/20Hz, startup, stall/recovery, repeated VI and 64-bit timing PASS");
}
