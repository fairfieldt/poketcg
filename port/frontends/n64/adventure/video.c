#include "video.h"
#include "frame_rate.h"
#include "../body_font.h"
#include <stdio.h>
#include <string.h>

unsigned n64_video_width=320,n64_video_height=240;
float n64_video_scale=1.0f;
static FrameRate frame_rate;
static surface_t fps_badge;
static unsigned badge_value;
static bool badge_ready,badge_sampled;
static uint64_t presented_frames;

resolution_t n64_video_init(bool high_resolution)
{
 bool expanded=high_resolution&&get_memory_size()>=8*1024*1024;
 n64_video_width=expanded?480:320;n64_video_height=expanded?360:240;
 n64_video_scale=expanded?1.5f:1.0f;
 memset(&frame_rate,0,sizeof(frame_rate));badge_ready=false;presented_frames=0;
 resolution_t resolution={.width=(int)n64_video_width,.height=(int)n64_video_height,
  .interlaced=expanded?INTERLACE_HALF:INTERLACE_OFF};
 debugf("VIDEO MODE width=%u height=%u ram=%d highres=%u\n",
  n64_video_width,n64_video_height,get_memory_size(),expanded?1u:0u);
 return resolution;
}

static int coordinate(int logical,unsigned physical,unsigned base)
{
 return logical>=0?(int)(((unsigned)logical*physical+base/2)/base):
  -(int)(((unsigned)(-logical)*physical+base/2)/base);
}
int n64_video_x(int logical){return coordinate(logical,n64_video_width,320);}
int n64_video_y(int logical){return coordinate(logical,n64_video_height,240);}
surface_t n64_video_surface(void)
{return surface_alloc(FMT_RGBA16,n64_video_width,n64_video_height);}

void n64_video_box(surface_t *canvas,int x,int y,int width,int height,uint32_t color)
{
 int left=n64_video_x(x),top=n64_video_y(y);
 int right=n64_video_x(x+width),bottom=n64_video_y(y+height);
 if(left<0)left=0;
 if(top<0)top=0;
 if(right>canvas->width)right=canvas->width;
 if(bottom>canvas->height)bottom=canvas->height;
 if(right>left&&bottom>top)graphics_draw_box(canvas,left,top,right-left,bottom-top,color);
}

void n64_video_blit(const surface_t *source,int x,int y)
{
 rdpq_tex_blit(source,n64_video_x(x),n64_video_y(y),
  &(rdpq_blitparms_t){.scale_x=n64_video_scale,.scale_y=n64_video_scale});
}
void n64_video_blit_physical(const surface_t *source,int x,int y)
{rdpq_tex_blit(source,n64_video_x(x),n64_video_y(y),NULL);}

static void update_fps_badge(void)
{
 int width=n64_video_x(64),height=n64_video_y(14);
 if(fps_badge.buffer&&(fps_badge.width!=width||fps_badge.height!=height)){
  surface_free(&fps_badge);badge_ready=false;
 }
 if(badge_ready&&badge_value==frame_rate.fps&&badge_sampled==frame_rate.ready)return;
 if(!fps_badge.buffer)fps_badge=surface_alloc(FMT_RGBA16,width,height);
 surface_t cached=fps_badge;cached.buffer=CachedAddr(cached.buffer);
 const uint16_t background=graphics_make_color(20,28,38,255);
 const uint16_t foreground=graphics_make_color(248,246,230,255);
 graphics_fill_screen(&cached,background);
 char text[24];
 if(frame_rate.ready)snprintf(text,sizeof(text),"Game %u",frame_rate.fps);
 else snprintf(text,sizeof(text),"Game --");
 int x=n64_video_x(4),y=n64_video_y(2);
 bool high=n64_video_width>320;
 for(const char *p=text;*p;p++){
  unsigned glyph=(unsigned char)*p-BODY_FONT_FIRST;
  const uint8_t *mask=high?body_hi_coverage[glyph]:body_font_coverage[glyph];
  int width=high?BODY_HI_WIDTH:BODY_FONT_WIDTH,height=high?BODY_HI_HEIGHT:BODY_FONT_HEIGHT;
  for(int row=0;row<height&&y+row<cached.height;row++)for(int col=0;col<width&&x+col<cached.width;col++){
   unsigned pixel=(unsigned)(row*width+col),coverage=(mask[pixel/2]>>(pixel&1?0:4))&15;
   if(!coverage)continue;
   uint16_t *out=(uint16_t *)((uint8_t *)cached.buffer+(y+row)*cached.stride)+x+col;
   unsigned r=(((foreground>>11)&31)*coverage+((background>>11)&31)*(15-coverage)+7)/15;
   unsigned g=(((foreground>>6)&31)*coverage+((background>>6)&31)*(15-coverage)+7)/15;
   unsigned b=(((foreground>>1)&31)*coverage+((background>>1)&31)*(15-coverage)+7)/15;
   *out=(uint16_t)((r<<11)|(g<<6)|(b<<1)|1);
  }
  x+=n64_video_x(body_font_advance[glyph]);
 }
 data_cache_hit_writeback_invalidate(cached.buffer,(unsigned long)cached.stride*cached.height);
 badge_value=frame_rate.fps;badge_ready=true;badge_sampled=frame_rate.ready;
}

void n64_video_present(surface_t *frame)
{
 bool sampled=frame_rate_present(&frame_rate,get_ticks(),TICKS_PER_SECOND);
 update_fps_badge();
 /* Never CPU-write an RDP framebuffer or touch it after display_show. */
 rdpq_attach(frame,NULL);rdpq_mode_push();rdpq_set_mode_copy(false);
 rdpq_set_scissor(0,0,n64_video_width,n64_video_height);
 n64_video_blit_physical(&fps_badge,4,2);
 rdpq_mode_pop();rdpq_detach_wait();
 display_show(frame);
 presented_frames++;
#if ADVENTURE_DIAGNOSTIC
 if(sampled)debugf("GAME FPS %u frames=%lu ticks=%llu\n",frame_rate.fps,
  (unsigned long)presented_frames,(unsigned long long)get_ticks());
#else
 (void)sampled;
#endif
}
