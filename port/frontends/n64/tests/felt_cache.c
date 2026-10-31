/* Compare the production row cache to the original scalar renderer, including
 * padded rows and untouched pixels outside the felt. Dead stripping keeps this
 * focused test independent of the duel engine and controller mock. */
#define TCGN64_HOST_TEST
#define main tcgn64_frontend_main
#include "../tcgn64.c"
#undef main

uint32_t graphics_make_color(int r,int g,int b,int a)
{return (uint32_t)((r>>3)<<11|(g>>3)<<6|(b>>3)<<1|(a!=0));}

static void reference(surface_t *surface,unsigned raster_width)
{
	static const uint8_t palette[3][3]={{20,48,40},{24,56,48},{28,60,52}};
	for(unsigned y=BOARD_Y*raster_width/SCREEN_W;y<(BOARD_Y+BOARD_H)*raster_width/SCREEN_W;y++) {
		uint16_t *row=(uint16_t *)((uint8_t *)surface->buffer+y*surface->stride);
		for(unsigned x=(LEFT-6)*raster_width/SCREEN_W;x<(RIGHT+6)*raster_width/SCREEN_W;x++) {
			unsigned tx=(x*SCREEN_W/surface->width)&31,ty=(y*SCREEN_H/surface->height)&31;
			unsigned grain=((tx*13+ty*7)^(tx*ty*3))&7;
			const uint8_t *rgb=palette[grain<2?0:grain>5?2:1];
			row[x]=(uint16_t)graphics_make_color(rgb[0],rgb[1],rgb[2],255);
		}
	}
}

static void check(unsigned width,unsigned height,unsigned period)
{
	unsigned stride=width*2+16;size_t bytes=(size_t)stride*height;
	uint8_t *expected=malloc(bytes),*actual=malloc(bytes);assert(expected&&actual);
	memset(expected,0xa5,bytes);memset(actual,0xa5,bytes);
	surface_t expected_surface={.width=width,.height=height,.stride=stride,.buffer=expected};
	surface_t actual_surface=expected_surface;actual_surface.buffer=actual;
	reference(&expected_surface,width);
	g_fb=&actual_surface;g_raster_width=width;draw_felt();
	assert(!memcmp(expected,actual,bytes));
	assert(g_felt.period==period);
	assert(g_felt.row_pixels==(RIGHT-LEFT+12)*width/SCREEN_W);
	/* Exercise cache reuse after arbitrary previous canvas contents. */
	uint16_t *cache=g_felt.rows;
	memset(actual,0xa5,bytes);draw_felt();
	assert(g_felt.rows==cache&&!memcmp(expected,actual,bytes));
	printf("felt %ux%u: exact, %u rows, %zu cached bytes, padded stride\n",
		width,height,period,(size_t)g_felt.row_pixels*period*sizeof(*cache));
	free(expected);free(actual);g_fb=NULL;
}

int main(void)
{
	check(320,240,32);check(480,360,48);check(320,240,32);
	free(g_felt.rows);g_felt=(FeltCache){0};
	puts("felt cache: PASS");return 0;
}
