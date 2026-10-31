/*
 * Host-side mock of the small part of libdragon that tcgn64.c uses, so that the
 * frontend's UI logic can run (and be tested) on the build machine.
 *
 * Declarations mirror libdragon's (display.h, graphics.h, sprite.h, joypad.h,
 * dragonfs.h, n64sys.h, debug.h). The implementation (mock_libdragon.c) draws
 * into an in-memory 320x240 RGBA5551 framebuffer with the same semantics as the
 * real CPU graphics functions (but aborts on out-of-bounds drawing, which would
 * corrupt memory on the console), reads scripted controller input and can save
 * screenshots. See mock_libdragon.c for the script format.
 */
#ifndef TCGN64_MOCK_LIBDRAGON_H
#define TCGN64_MOCK_LIBDRAGON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* display.h / surface.h */
typedef struct surface_s {
	uint16_t flags;
	uint16_t width;
	uint16_t height;
	uint16_t stride;
	void *buffer;
} surface_t;

typedef struct { int32_t width, height; int interlaced; } resolution_t;
#define RESOLUTION_320x240 ((resolution_t){ 320, 240, 0 })
typedef enum { DEPTH_16_BPP = 2, DEPTH_32_BPP = 4 } bitdepth_t;
typedef enum { GAMMA_NONE = 0 } gamma_t;
typedef enum { FILTERS_RESAMPLE = 2 } filter_options_t;

void display_init(resolution_t res, bitdepth_t bit, uint32_t num_buffers, gamma_t gamma, filter_options_t filters);
surface_t *display_get(void);
void display_show(surface_t *surf);
uint32_t display_get_width(void);
uint32_t display_get_height(void);
uint32_t display_get_bitdepth(void);

/* graphics.h */
uint32_t graphics_make_color(int r, int g, int b, int a);
void graphics_fill_screen(surface_t *surf, uint32_t c);
void graphics_draw_box(surface_t *surf, int x, int y, int width, int height, uint32_t color);
void graphics_set_color(uint32_t forecolor, uint32_t backcolor);
void graphics_draw_text(surface_t *surf, int x, int y, const char *const msg);
/* Same compact-font rows used by libdragon, for scaled per-glyph tests. */
unsigned mock_font_row(unsigned character,unsigned row);

/* sprite.h */
typedef struct sprite_s {
	uint16_t width;
	uint16_t height;
	uint16_t *pixels;   /* mock only: RGBA5551 pixels */
} sprite_t;
sprite_t *sprite_load(const char *fn);
void sprite_free(sprite_t *sprite);
void graphics_draw_sprite(surface_t *surf, int x, int y, sprite_t *sprite);

/* joypad.h */
typedef enum { JOYPAD_PORT_1 = 0, JOYPAD_PORT_2 = 1, JOYPAD_PORT_3 = 2, JOYPAD_PORT_4 = 3 } joypad_port_t;
typedef union joypad_buttons_u {
	uint16_t raw;
	struct __attribute__((packed)) {
		unsigned a : 1;
		unsigned b : 1;
		unsigned z : 1;
		unsigned start : 1;
		unsigned d_up : 1;
		unsigned d_down : 1;
		unsigned d_left : 1;
		unsigned d_right : 1;
		unsigned y : 1;
		unsigned x : 1;
		unsigned l : 1;
		unsigned r : 1;
		unsigned c_up : 1;
		unsigned c_down : 1;
		unsigned c_left : 1;
		unsigned c_right : 1;
	};
} joypad_buttons_t;
typedef struct __attribute__((packed)) joypad_inputs_s {
	joypad_buttons_t btn;
	int8_t stick_x;
	int8_t stick_y;
	int8_t cstick_x;
	int8_t cstick_y;
	uint8_t analog_l;
	uint8_t analog_r;
} joypad_inputs_t;
void joypad_init(void);
void joypad_poll(void);
bool joypad_is_connected(joypad_port_t port);
joypad_buttons_t joypad_get_buttons_pressed(joypad_port_t port);
joypad_buttons_t joypad_get_buttons_held(joypad_port_t port);
joypad_inputs_t joypad_get_inputs(joypad_port_t port);

/* dragonfs.h: "rom:/" paths are mapped to $MOCK_ROMFS */
#define DFS_DEFAULT_LOCATION 0
#define DFS_ESUCCESS 0
int dfs_init(uint32_t base_fs_loc);
FILE *mock_fopen(const char *path, const char *mode);
#define fopen(path, mode) mock_fopen(path, mode)

/* eeprom.h: backed by the file $MOCK_EEPROM (written through, so that the
 * script can "power off" at any frame); $MOCK_EEPROM_TYPE = 16k (default), 4k, none */
typedef enum { EEPROM_NONE = 0, EEPROM_4K = 1, EEPROM_16K = 2 } eeprom_type_t;
#define EEPROM_BLOCK_SIZE 8
eeprom_type_t eeprom_present(void);
void eeprom_read(uint8_t block, uint8_t *dest);
uint8_t eeprom_write(uint8_t block, const uint8_t *src);

/* n64sys.h */
uint64_t get_ticks(void);
#define CachedAddr(addr) ((void *)(addr))
void data_cache_hit_writeback_invalidate(volatile void *addr, unsigned long length);

/* debug.h */
bool debug_init_usblog(void);
bool debug_init_emulog(void);
#define debug_init_isviewer debug_init_emulog
void mock_debugf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define debugf(...) mock_debugf(__VA_ARGS__)

#endif
