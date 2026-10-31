#define main tcgn64_frontend_main
#include "../../../frontends/n64/tcgn64.c"
#undef main
#include <timer.h>
__attribute__((noinline)) static void draw_felt_original(void)
{
	static uint16_t tile[32][32];static bool ready;
	if(!ready) {
		static const uint8_t palette[3][3]={{20,48,40},{24,56,48},{28,60,52}};
		for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++) {
			unsigned grain=((x*13+y*7)^(x*y*3))&7;
			const uint8_t *rgb=palette[grain<2?0:grain>5?2:1];
			tile[y][x]=(uint16_t)graphics_make_color(rgb[0],rgb[1],rgb[2],255);
		}
		ready=true;
	}
	/* Copy only into the reserved CPU canvas. end_frame presents it through
	   RDP after cache writeback, exactly like the rest of the duel UI. */
	for(int y=ui_y(BOARD_Y);y<ui_y(BOARD_Y+BOARD_H);y++) {
		uint16_t *row=(uint16_t *)((uint8_t *)g_fb->buffer+y*g_fb->stride);
		for(int x=ui_x(LEFT-6);x<ui_x(RIGHT+6);x++)
			row[x]=tile[(y*SCREEN_H/g_fb->height)&31][(x*SCREEN_W/g_fb->width)&31];
	}
}

__attribute__((noinline)) static void draw_cached(void) {draw_felt();}
static void run(unsigned width,unsigned height)
{
 surface_t source=surface_alloc(FMT_RGBA16,width,height),result=surface_alloc(FMT_RGBA16,width,height);
 assert(source.buffer&&result.buffer);
 surface_t reference=source,canvas=result;
 reference.buffer=CachedAddr(source.buffer);canvas.buffer=CachedAddr(result.buffer);
 g_raster_width=width;
 memset(reference.buffer,0xa5,(size_t)reference.stride*height);
 memset(canvas.buffer,0xa5,(size_t)canvas.stride*height);
 g_fb=&reference;draw_felt_original();
 g_fb=&canvas;uint32_t begun=timer_ticks();draw_cached();
 unsigned cold=(unsigned)TICKS_TO_US((uint32_t)(timer_ticks()-begun));
 assert(!memcmp(reference.buffer,canvas.buffer,(size_t)canvas.stride*height));
 debugf("FELT COLD width=%u height=%u render_us=%u cache_bytes=%u exact=1\n",width,height,cold,g_felt.row_pixels*g_felt.period*2);
 for(unsigned pass=0;pass<2;pass++)for(unsigned frame=0;frame<24;frame++) {
  g_fb=pass?&canvas:&reference;
  uint32_t started=timer_ticks();
  if(pass)draw_cached();else draw_felt_original();
  unsigned elapsed=(unsigned)TICKS_TO_US((uint32_t)(timer_ticks()-started));
  debugf("FELT FRAME width=%u height=%u stage=%s render_us=%u\n",width,height,pass?"cached":"original",elapsed);
 }
 assert(!memcmp(reference.buffer,canvas.buffer,(size_t)canvas.stride*height));
 surface_free(&source);surface_free(&result);g_fb=NULL;
}
int main(void)
{
 debug_init_isviewer();debug_init_usblog();timer_init();
 display_init(RESOLUTION_320x240,DEPTH_16_BPP,2,GAMMA_NONE,FILTERS_RESAMPLE);
 debugf("FELT HEAP BEGIN\n");heap_stats_t before;sys_get_heap_stats(&before);
 run(320,240);run(480,360);run(320,240);
 free(g_felt.rows);g_felt=(FeltCache){0};
 heap_stats_t after;sys_get_heap_stats(&after);
 debugf("FELT HEAP before_used=%d after_used=%d\n",before.used,after.used);
 assert(before.used==after.used);
 debugf("FELT BENCH PASS\n");
 for(;;)wait_ms(10);
}
