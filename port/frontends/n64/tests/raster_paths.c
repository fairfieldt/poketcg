/* Exact output against the original scalar rasterizers. Keeps padded rows and
 * guard bytes so clipping, transparent texels and untouched pixels are checked. */
#define TCGN64_HOST_TEST
#define TCGN64_CARD_ART
#define main tcgn64_frontend_main
#ifndef TCGN64_SOURCE
#define TCGN64_SOURCE "../tcgn64.c"
#endif
#include TCGN64_SOURCE
#undef main

static uint32_t foreground;
void graphics_set_color(uint32_t fore,uint32_t back){(void)back;foreground=fore;}
unsigned mock_font_row(unsigned character,unsigned row)
{return ((character*37+row*71)^(character<<(row&3)))&255;}
void graphics_draw_box(surface_t *s,int x,int y,int width,int height,uint32_t color)
{
 assert(x>=0&&y>=0&&x+width<=s->width&&y+height<=s->height);
 for(int yy=y;yy<y+height;yy++) {
  uint16_t *dest=(uint16_t *)((uint8_t *)s->buffer+yy*s->stride);
  for(int xx=x;xx<x+width;xx++)dest[xx]=(uint16_t)color;
 }
}
void graphics_draw_text(surface_t *s,int x,int y,const char *text)
{
 for(;*text;text++,x+=8)for(unsigned row=0;row<8;row++) {
  unsigned bits=mock_font_row((unsigned char)*text,row);
  for(unsigned col=0;col<8;col++)if(bits&(0x80>>col))graphics_draw_box(s,x+(int)col,y+(int)row,1,1,foreground);
 }
}

static void reference_compact_text(int x,int y,uint32_t color,const char *text,int limit)
{
 if(!ui_high_res()) {
  char line[SCREEN_W/CW+1];int count=0;
  if(limit>g_fb->width)limit=g_fb->width;
  if(x<0||y<0||y+8>g_fb->height)return;
  while(text[count]&&text[count]!='\n'&&count<SCREEN_W/CW&&x+(count+1)*CW<=limit){line[count]=text[count];count++;}
  line[count]=0;graphics_set_color(color,0);graphics_draw_text(g_fb,x,y,line);return;
 }
 for(;*text&&*text!='\n';text++,x+=CW) {
  unsigned ch=(unsigned char)*text;if(ch<32||ch>126)ch='?';
  if(x+CW>limit)break;
  for(int row=0;row<8;row++) {
   unsigned bits=mock_font_row(ch,(unsigned)row);
   for(int col=0;col<8;col++)if(bits&(0x80>>col)) {
    int px=ui_x(x+col),py=ui_y(y+row);
    int w=ui_x(x+col+1)-px,h=ui_y(y+row+1)-py;
    if(px<0||py<0||px+w>g_fb->width||py+h>g_fb->height)continue;
    graphics_draw_box(g_fb,px,py,w,h,color);
   }
  }
 }
}

static void reference_blit(int x,int y,int width,int height,const uint16_t *pixels,
 int sw,int sh,unsigned stride)
{
 int px=ui_x(x),py=ui_y(y),w=ui_x(x+width)-px,h=ui_y(y+height)-py;
 for(int row=0;row<h;row++) {
  int dy=py+row;if(dy<0||dy>=g_fb->height)continue;
  uint16_t *dest=(uint16_t *)((uint8_t *)g_fb->buffer+dy*g_fb->stride);
  const uint16_t *src=pixels+(row*sh/h)*stride;
  for(int col=0;col<w;col++) {
   int dx=px+col;if(dx<0||dx>=g_fb->width)continue;
   uint16_t color=src[col*sw/w];if(color&1)dest[dx]=color;
  }
 }
}

static void reference_back(int x,int y,int width,int height)
{
 fill(x+2,y+2,width,height,C_PANELSHADE);
 const uint16_t *pixels;int sw,sh;
 if(width<=24){pixels=ui_high_res()?card_back_small_hi:card_back_small;sw=ui_high_res()?36:24;sh=ui_high_res()?51:34;}
 else{pixels=ui_high_res()?card_back_high:card_back_standard;sw=ui_high_res()?48:32;sh=ui_high_res()?66:44;}
 reference_blit(x,y,width,height,pixels,sw,sh,(unsigned)sw);
}

enum {GUARD=64};
static surface_t actual,expected;
static uint8_t *actual_mem,*expected_mem;
static size_t bytes;
static unsigned glyph_cases,blit_cases,back_cases;
static void reset(void){memset(actual_mem,0xa5,bytes);memset(expected_mem,0xa5,bytes);}
static void equal(const char *kind,int x,int y)
{
 if(memcmp(actual_mem,expected_mem,bytes)) {
  fprintf(stderr,"Mismatch %s at %d,%d canvas%ux%u raster%u\n",kind,x,y,actual.width,actual.height,g_raster_width);abort();
 }
}
static void check(unsigned width,unsigned height,unsigned raster)
{
 unsigned stride=width*2+16;bytes=(size_t)stride*height+GUARD*2;
 actual_mem=malloc(bytes);expected_mem=malloc(bytes);assert(actual_mem&&expected_mem);
 actual=(surface_t){.width=width,.height=height,.stride=stride,.buffer=actual_mem+GUARD};
 expected=actual;expected.buffer=expected_mem+GUARD;g_raster_width=raster;
 static const int xs[]={-9,-1,0,1,2,3,19,255,311,312,313,319,320};
 static const int ys[]={-9,-1,0,1,2,3,17,231,232,233,239,240};
 for(unsigned ch=1;ch<128;ch++)for(unsigned phase=0;phase<4;phase++) {
  char text[2]={(char)ch,0};int x=(int)(phase&1),y=(int)(phase>>1);reset();
  g_fb=&expected;reference_compact_text(x,y,0x92d792d7,text,320);
  g_fb=&actual;compact_text(x,y,0x92d792d7,text,320);equal("glyph",x,y);glyph_cases++;
 }
 for(unsigned ix=0;ix<sizeof(xs)/sizeof(*xs);ix++)for(unsigned iy=0;iy<sizeof(ys)/sizeof(*ys);iy++) {
  int x=xs[ix],y=ys[iy];reset();
  const char *text="Mark's turn\nnext";
  g_fb=&expected;reference_compact_text(x,y,0x92d792d7,text,319);
  g_fb=&actual;compact_text(x,y,0x92d792d7,text,319);equal("clipped text",x,y);glyph_cases++;
 }
 uint16_t pixels[69*48];for(unsigned i=0;i<sizeof(pixels)/sizeof(*pixels);i++)pixels[i]=(uint16_t)(i*4117+29);
 static const int sizes[][2]={{64,48},{16,12},{17,13},{32,44},{1,1},{0,0},{320,240}};
 for(unsigned sz=0;sz<sizeof(sizes)/sizeof(*sizes);sz++)for(unsigned ix=0;ix<sizeof(xs)/sizeof(*xs);ix++)for(unsigned iy=0;iy<sizeof(ys)/sizeof(*ys);iy++) {
  int x=xs[ix],y=ys[iy];reset();
  g_fb=&expected;reference_blit(x,y,sizes[sz][0],sizes[sz][1],pixels,64,48,69);
  g_fb=&actual;blit_rgba16_logical(x,y,sizes[sz][0],sizes[sz][1],pixels,64,48,69);equal("sprite",x,y);blit_cases++;
 }
 /* The public sprite helper uses its original accessors and packed stride. */
 sprite_t sprite={.width=64,.height=48,.pixels=pixels};reset();
 g_fb=&expected;reference_blit(1,3,64,48,pixels,64,48,64);
 g_fb=&actual;sprite_logical(1,3,64,48,&sprite);equal("sprite helper",1,3);
 /* The back's existing shadow panel has a full-screen logical clip. */
 if(width==raster&&height==raster*3/4)
 for(unsigned sz=0;sz<3;sz++)for(unsigned ix=0;ix<sizeof(xs)/sizeof(*xs);ix++)for(unsigned iy=0;iy<sizeof(ys)/sizeof(*ys);iy++) {
  static const int back[][2]={{24,34},{28,40},{32,44}};int x=xs[ix],y=ys[iy];reset();
  g_fb=&expected;reference_back(x,y,back[sz][0],back[sz][1]);
  g_fb=&actual;card_back(x,y,back[sz][0],back[sz][1]);equal("card back",x,y);back_cases++;
 }
 printf("raster %ux%u, padded stride%u, logical raster%u: exact\n",width,height,stride,raster);
 free(actual_mem);free(expected_mem);
}
int main(void)
{
 g_col[C_PANELSHADE]=0x12491249;
 check(320,240,320);check(480,360,480);check(113,47,480);
 printf("raster paths: PASS glyphs%u scaled blits%u backs%u; parity, clips, alpha, strides and guards\n",glyph_cases,blit_cases,back_cases);
 return 0;
}
