#include "video.h"

unsigned n64_video_width=320,n64_video_height=240;
float n64_video_scale=1.0f;

resolution_t n64_video_init(bool high_resolution)
{
 bool expanded=high_resolution&&get_memory_size()>=8*1024*1024;
 n64_video_width=expanded?480:320;n64_video_height=expanded?360:240;
 n64_video_scale=expanded?1.5f:1.0f;
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
