#ifndef ADVENTURE_FRAME_RATE_H
#define ADVENTURE_FRAME_RATE_H
#include <stdbool.h>
#include <stdint.h>

/* One sample per newly completed game frame. VI scanout never calls this. */
typedef struct {
 uint64_t start;
 uint32_t intervals;
 unsigned fps;
 bool started, ready;
} FrameRate;

static inline bool frame_rate_present(FrameRate *rate,uint64_t now,uint64_t frequency)
{
 if(!rate->started){rate->start=now;rate->started=true;return false;}
 rate->intervals++;
 uint64_t elapsed=now-rate->start;
 if(elapsed<frequency/2)return false;
 rate->fps=(unsigned)(((uint64_t)rate->intervals*frequency+elapsed/2)/elapsed);
 rate->ready=true;rate->start=now;rate->intervals=0;
 return true;
}
#endif
