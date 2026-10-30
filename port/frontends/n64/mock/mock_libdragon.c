/*
 * Host implementation of mock/libdragon.h (see there).
 *
 * Environment:
 *   MOCK_SCRIPT  controller script (default: none = exit after 600 frames)
 *   MOCK_ROMFS   directory that "rom:/" maps to (default: build/filesystem)
 *   MOCK_SHOTS   directory for screenshots (.ppm, default: .)
 *   MOCK_QUIET   1 = do not echo debugf output
 *
 * Script: one command per line, '#' starts a comment. One input step is one
 * joypad_poll() call (one frame of the frontend).
 *   wait N                    N frames without buttons
 *   press BTN[+BTN] [P2]      hold the buttons 2 frames, release 2 frames
 *                             (A B Z START L R UP DOWN LEFT RIGHT CU CD CL CR)
 *   repeat N press BTN [P2]   the same, N times
 *   monkey N SEED [P2|BOTH]   N frames of random button presses
 *   until TEXT N SEED [P2|BOTH]
 *                             random presses until a debugf line contains TEXT
 *                             (fails after N frames)
 *   expect TEXT [MIN]         fail unless MIN (default 1) debugf lines so far contain TEXT
 *   expect_none TEXT          fail if a debugf line so far contains TEXT
 * In TEXT, '~' stands for a space and '|' separates alternatives.
 *   shot NAME                 save the next shown frame as NAME.ppm (takes one frame)
 *   shots_every N             during monkey/until, save a frame every N frames (max 40)
 *   shot_first PREFIX         save the frame after the first debugf line starting with
 *                             PREFIX for each distinct second word (e.g. "ASK card_list")
 *   connect P2 | disconnect P2
 *   quit                      exit(0)
 * The process exits 0 at the end of the script, 3 on a frontend FATAL, 4 on
 * an `until` timeout and 5 on a failed `expect`.
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "libdragon.h"
#undef fopen

#ifdef MOCK_FONT_H
#include MOCK_FONT_H /* libdragon's 8x8 font (__font_data) */
#define HAVE_FONT 1
#endif

#define W 320
#define H 240

static uint16_t g_buf[3][W * H];
static surface_t g_surf[3];
static int g_next, g_held_surface = -1;
static bool g_inited;
static uint32_t g_fg = 0xffffffff, g_bg;
static unsigned long g_frames, g_polls;

static void die(int code, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "MOCK: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	fprintf(stderr, "MOCK: frames=%lu polls=%lu\n", g_frames, g_polls);
	exit(code);
}

/* ------------------------------------------------------------------ display / graphics */

void display_init(resolution_t res, bitdepth_t bit, uint32_t num_buffers, gamma_t gamma, filter_options_t filters)
{
	(void)gamma;
	(void)filters;
	if (res.width != W || res.height != H || bit != DEPTH_16_BPP || num_buffers < 1 || num_buffers > 3)
		die(2, "unsupported display_init");
	for (int i = 0; i < 3; i++)
		g_surf[i] = (surface_t){ 0, W, H, W * 2, g_buf[i] };
	g_inited = true;
}

uint32_t display_get_width(void) { return W; }
uint32_t display_get_height(void) { return H; }
uint32_t display_get_bitdepth(void) { return 2; }

surface_t *display_get(void)
{
	if (!g_inited)
		die(2, "display_get before display_init");
	if (g_held_surface >= 0)
		die(2, "display_get while a surface is still held (missing display_show)");
	g_held_surface = g_next;
	g_next = (g_next + 1) % 3;
	return &g_surf[g_held_surface];
}

/* the frontend may draw on a copy of the held surface (e.g. with a cached alias of its buffer) */
static void check_surface(surface_t *s)
{
	if (g_held_surface < 0 || !s || s->buffer != g_surf[g_held_surface].buffer || s->width != W || s->height != H)
		die(2, "drawing on a surface that is not held");
}

static unsigned long g_writebacks;

void data_cache_hit_writeback_invalidate(volatile void *addr, unsigned long length)
{
	if (g_held_surface >= 0 && addr == g_surf[g_held_surface].buffer && length >= W * H * 2)
		g_writebacks++;
}

uint32_t graphics_make_color(int r, int g, int b, int a)
{
	if (!g_inited)
		die(2, "graphics_make_color before display_init (its result depends on the bit depth)");
	uint32_t c = (uint32_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (a >> 7));
	return c | c << 16;
}

static bool transparent(uint32_t c) { return (c & 1) == 0; }

static void put(surface_t *s, int x, int y, uint32_t c, const char *who)
{
	if (x < 0 || y < 0 || x >= W || y >= H)
		die(2, "%s draws outside the framebuffer at (%d, %d)", who, x, y);
	((uint16_t *)s->buffer)[y * W + x] = (uint16_t)c;
}

void graphics_fill_screen(surface_t *s, uint32_t c)
{
	check_surface(s);
	for (int i = 0; i < W * H; i++)
		((uint16_t *)s->buffer)[i] = (uint16_t)c;
}

void graphics_draw_box(surface_t *s, int x, int y, int w, int h, uint32_t c)
{
	check_surface(s);
	for (int j = y; j < y + h; j++)
		for (int i = x; i < x + w; i++)
			put(s, i, j, c, "graphics_draw_box");
}

void graphics_set_color(uint32_t fg, uint32_t bg)
{
	g_fg = fg;
	g_bg = bg;
}

void graphics_draw_text(surface_t *s, int x, int y, const char *const msg)
{
	check_surface(s);
	int tx = x, ty = y;
	for (const char *p = msg; *p; p++) {
		if (*p == '\n' || *p == '\r') {
			tx = x;
			ty += 8;
			continue;
		}
		if (*p == ' ') {
			tx += 8;
			continue;
		}
		if ((signed char)*p < 0)
			die(2, "graphics_draw_text with a non-ASCII byte 0x%02x (indexes the font out of bounds on N64)",
			    (unsigned char)*p);
		for (int row = 0; row < 8; row++) {
#ifdef HAVE_FONT
			unsigned char bits = __font_data[(unsigned char)*p * 8 + row];
#else
			unsigned char bits = (row == 0 || row == 7) ? 0 : 0x7e;
#endif
			for (int col = 0; col < 8; col++) {
				bool on = (bits & (0x80 >> col)) != 0;
				if (on)
					put(s, tx + col, ty + row, g_fg, "graphics_draw_text");
				else if (!transparent(g_bg))
					put(s, tx + col, ty + row, g_bg, "graphics_draw_text");
			}
		}
		tx += 8;
	}
}

/* ------------------------------------------------------------------ files, sprites */

static const char *romfs(void)
{
	const char *d = getenv("MOCK_ROMFS");
	return d && *d ? d : "build/filesystem";
}

static void map_path(const char *path, char *out, size_t n)
{
	if (!strncmp(path, "rom:/", 5))
		snprintf(out, n, "%s/%s", romfs(), path + 5);
	else
		snprintf(out, n, "%s", path);
}

FILE *mock_fopen(const char *path, const char *mode)
{
	char p[512];
	map_path(path, p, sizeof(p));
	return fopen(p, mode);
}

int dfs_init(uint32_t base) { (void)base; return DFS_ESUCCESS; }

static unsigned long g_sprites_loaded, g_sprites_live;

sprite_t *sprite_load(const char *fn)
{
	char p[512];
	struct stat st;
	map_path(fn, p, sizeof(p));
	/* libdragon asserts on a missing file: so do we */
	if (stat(p, &st) != 0 || st.st_size < 16)
		die(2, "sprite_load: missing or empty %s (%s)", fn, p);
	sprite_t *s = calloc(1, sizeof(*s));
	s->width = 64;
	s->height = 48;
	s->pixels = calloc(64 * 48, sizeof(uint16_t));
	/* a pattern derived from the file name, so different cards look different */
	uint32_t h = 2166136261u;
	for (const char *c = fn; *c; c++)
		h = (h ^ (unsigned char)*c) * 16777619u;
	for (int y = 0; y < 48; y++)
		for (int x = 0; x < 64; x++) {
			int band = ((x + y + (int)(h & 15)) / 6) & 1;
			uint32_t r = band ? (h >> 8) & 31 : 31, g = band ? (h >> 13) & 31 : 28, b = band ? (h >> 18) & 31 : 20;
			s->pixels[y * 64 + x] = (uint16_t)(r << 11 | g << 6 | b << 1 | 1);
		}
	g_sprites_loaded++;
	g_sprites_live++;
	return s;
}

void sprite_free(sprite_t *s)
{
	if (!s)
		return;
	free(s->pixels);
	free(s);
	g_sprites_live--;
}

void graphics_draw_sprite(surface_t *surf, int x, int y, sprite_t *s)
{
	check_surface(surf);
	if (!s)
		return;
	/* libdragon clips sprites */
	for (int j = 0; j < s->height; j++)
		for (int i = 0; i < s->width; i++)
			if (x + i >= 0 && y + j >= 0 && x + i < W && y + j < H)
				put(surf, x + i, y + j, s->pixels[j * s->width + i], "graphics_draw_sprite");
}

/* ------------------------------------------------------------------ debug log */

static char g_shot_name[256];
static char g_shot_first[32];
static char g_seen[64][96];
static size_t g_nseen;
static char **g_lines;
static size_t g_nlines, g_caplines;
static bool g_fatal;
static const char *g_until;
static bool g_until_hit;

/* does line contain one of the '|' separated alternatives of pattern ('~' = space)? */
static bool matches(const char *line, const char *pattern)
{
	char alt[128];
	const char *p = pattern;
	while (*p) {
		size_t n = strcspn(p, "|");
		if (n >= sizeof(alt))
			n = sizeof(alt) - 1;
		memcpy(alt, p, n);
		alt[n] = 0;
		for (char *c = alt; *c; c++)
			if (*c == '~')
				*c = ' ';
		if (n && strstr(line, alt))
			return true;
		p += n;
		if (*p == '|')
			p++;
	}
	return false;
}

static size_t count_lines(const char *text)
{
	size_t n = 0;
	for (size_t i = 0; i < g_nlines; i++)
		n += matches(g_lines[i], text);
	return n;
}

bool debug_init_usblog(void) { return true; }
bool debug_init_emulog(void) { return true; }

void mock_debugf(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	const char *q = getenv("MOCK_QUIET");
	if (!q || *q != '1')
		fputs(buf, stdout);
	if (g_nlines == g_caplines) {
		g_caplines = g_caplines ? g_caplines * 2 : 1024;
		g_lines = realloc(g_lines, g_caplines * sizeof(*g_lines));
	}
	g_lines[g_nlines++] = strdup(buf);
	if (!strncmp(buf, "FATAL", 5))
		g_fatal = true;
	if (g_until && matches(buf, g_until))
		g_until_hit = true;
	if (g_shot_first[0] && !strncmp(buf, g_shot_first, strlen(g_shot_first))) {
		char key[96] = "", w1[32] = "", w2[48] = "";
		sscanf(buf, "%31s %47s", w1, w2);
		snprintf(key, sizeof(key), "first_%s_%s", w1, w2);
		for (char *c = key; *c; c++)
			if (*c == '/')
				*c = '-';
		bool seen = false;
		for (size_t i = 0; i < g_nseen; i++)
			seen |= !strcmp(g_seen[i], key);
		if (!seen && g_nseen < sizeof(g_seen) / sizeof(g_seen[0])) {
			snprintf(g_seen[g_nseen++], sizeof(g_seen[0]), "%s", key);
			snprintf(g_shot_name, sizeof(g_shot_name), "%s", key);
		}
	}
}

uint64_t get_ticks(void) { return 46875ull * g_frames + 12345; }

/* ------------------------------------------------------------------ screenshots */

static unsigned g_shots_every, g_auto_shots;

static void save_ppm(const surface_t *s, const char *name)
{
	const char *dir = getenv("MOCK_SHOTS");
	char path[512];
	snprintf(path, sizeof(path), "%s/%s.ppm", dir && *dir ? dir : ".", name);
	FILE *f = fopen(path, "wb");
	if (!f)
		die(2, "cannot write %s", path);
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	const uint16_t *p = s->buffer;
	for (int i = 0; i < W * H; i++) {
		unsigned c = p[i];
		unsigned char rgb[3] = { (unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 6) & 31) * 255 / 31),
		                         (unsigned char)(((c >> 1) & 31) * 255 / 31) };
		fwrite(rgb, 1, 3, f);
	}
	fclose(f);
	printf("MOCK: shot %s (frame %lu)\n", path, g_frames);
}

void display_show(surface_t *s)
{
	check_surface(s);
	if (s != &g_surf[g_held_surface])
		die(2, "display_show of a surface display_get did not return");
	if (g_writebacks != g_frames + 1)
		die(2, "frame %lu shown without writing the data cache back", g_frames);
	g_held_surface = -1;
	g_frames++;
	if (g_shot_name[0]) {
		save_ppm(s, g_shot_name);
		g_shot_name[0] = 0;
	}
	if (g_shots_every && g_frames % g_shots_every == 0 && g_auto_shots < 40) {
		char n[64];
		snprintf(n, sizeof(n), "auto_%06lu", g_frames);
		save_ppm(s, n);
		g_auto_shots++;
	}
	if (g_fatal) {
		save_ppm(s, "fatal");
		const char *e = getenv("MOCK_FATAL_EXIT");
		if (!e || *e != '0')
			die(3, "the frontend reported a FATAL error");
	}
	if (g_frames > 5000000)
		die(2, "frame limit");
}

/* ------------------------------------------------------------------ scripted joypad */

enum { B_A, B_B, B_Z, B_START, B_L, B_R, B_UP, B_DOWN, B_LEFT, B_RIGHT, B_CU, B_CD, B_CL, B_CR, NUM_B };
static const char *const kBtn[NUM_B] = { "A", "B", "Z", "START", "L", "R", "UP", "DOWN", "LEFT", "RIGHT", "CU", "CD", "CL", "CR" };

static FILE *g_script;
static unsigned g_held[2], g_prev[2];
static bool g_connected[2] = { true, true };
static struct {
	enum { S_IDLE, S_WAIT, S_PRESS, S_MONKEY } kind;
	unsigned long left;      /* frames left in this command */
	unsigned buttons;        /* S_PRESS */
	int ports;               /* bit mask */
	int repeat;
	unsigned long rng;
	bool until;
	unsigned long until_max;
	char until_text[128];
	int phase;               /* monkey: frames into the current press */
	unsigned cur;
} g_cmd;

void joypad_init(void)
{
	const char *p = getenv("MOCK_SCRIPT");
	if (p && *p) {
		g_script = fopen(p, "r");
		if (!g_script)
			die(2, "cannot open script %s", p);
	}
}

bool joypad_is_connected(joypad_port_t port) { return port <= JOYPAD_PORT_2 && g_connected[port]; }

static unsigned parse_buttons(const char *s)
{
	unsigned m = 0;
	char tmp[128];
	snprintf(tmp, sizeof(tmp), "%.100s", s);
	for (char *t = strtok(tmp, "+"); t; t = strtok(NULL, "+")) {
		int i;
		for (i = 0; i < NUM_B; i++)
			if (!strcasecmp(t, kBtn[i]))
				break;
		if (i == NUM_B)
			die(2, "unknown button %s", t);
		m |= 1u << i;
	}
	return m;
}

static int parse_ports(const char *s)
{
	if (!s || !*s)
		return 1;
	if (!strcasecmp(s, "P2"))
		return 2;
	if (!strcasecmp(s, "BOTH"))
		return 3;
	return 1;
}

static unsigned long lcg(void)
{
	g_cmd.rng = g_cmd.rng * 6364136223846793005ull + 1442695040888963407ull;
	return g_cmd.rng >> 33;
}

static unsigned monkey_button(void)
{
	/* weights: mostly A and directions so that duels make progress */
	static const struct { int b, w; } t[] = {
		{ B_A, 34 }, { B_B, 7 }, { B_START, 3 }, { B_Z, 1 }, { B_L, 2 }, { B_R, 2 },
		{ B_UP, 13 }, { B_DOWN, 14 }, { B_LEFT, 12 }, { B_RIGHT, 12 },
	};
	int total = 0;
	for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		total += t[i].w;
	int r = (int)(lcg() % (unsigned long)total);
	for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		if (r < t[i].w)
			return 1u << t[i].b;
		r -= t[i].w;
	}
	return 1u << B_A;
}

static void next_command(void)
{
	char line[256];
	for (;;) {
		if (!g_script || !fgets(line, sizeof(line), g_script)) {
			if (!g_script && g_frames < 600) {
				g_cmd.kind = S_WAIT;
				g_cmd.left = 600;
				return;
			}
			die(0, "end of script");
		}
		char *hash = strchr(line, '#');
		if (hash)
			*hash = 0;
		char a[128] = "", b[128] = "", c[128] = "", d[128] = "", e[128] = "";
		int n = sscanf(line, "%127s %127s %127s %127s %127s", a, b, c, d, e);
		if (n <= 0)
			continue;
		printf("MOCK: [frame %lu] %s", g_frames, line[strlen(line) - 1] == '\n' ? line : strcat(line, "\n"));
		memset(&g_cmd, 0, sizeof(g_cmd));
		if (!strcmp(a, "wait")) {
			g_cmd.kind = S_WAIT;
			g_cmd.left = strtoul(b, NULL, 0);
			return;
		}
		if (!strcmp(a, "press") || !strcmp(a, "repeat")) {
			g_cmd.kind = S_PRESS;
			g_cmd.repeat = 1;
			if (!strcmp(a, "repeat")) {
				g_cmd.repeat = atoi(b);
				snprintf(b, sizeof(b), "%s", d); /* repeat N press BTN [P2] */
				snprintf(c, sizeof(c), "%s", e);
			}
			g_cmd.buttons = parse_buttons(b);
			g_cmd.ports = parse_ports(c);
			g_cmd.left = 4;
			return;
		}
		if (!strcmp(a, "monkey") || !strcmp(a, "until")) {
			g_cmd.kind = S_MONKEY;
			if (!strcmp(a, "until")) {
				g_cmd.until = true;
				snprintf(g_cmd.until_text, sizeof(g_cmd.until_text), "%s", b);
				g_until = g_cmd.until_text;
				g_until_hit = false;
				snprintf(b, sizeof(b), "%s", c);
				snprintf(c, sizeof(c), "%s", d);
				snprintf(d, sizeof(d), "%s", e);
			}
			g_cmd.left = strtoul(b, NULL, 0);
			g_cmd.until_max = g_cmd.left;
			g_cmd.rng = strtoul(c, NULL, 0) * 2654435761ull + 1;
			g_cmd.ports = parse_ports(d);
			return;
		}
		if (!strcmp(a, "expect")) {
			size_t min = n >= 3 ? strtoul(c, NULL, 0) : 1, got = count_lines(b);
			printf("MOCK: expect '%s' >= %zu: %zu\n", b, min, got);
			if (got < min)
				die(5, "expectation failed: '%s' seen %zu times, wanted >= %zu", b, got, min);
			continue;
		}
		if (!strcmp(a, "shot")) {
			/* the next frame shows the state after the previous input: capture it, press nothing */
			snprintf(g_shot_name, sizeof(g_shot_name), "%s", b);
			g_cmd.kind = S_WAIT;
			g_cmd.left = 1;
			return;
		}
		if (!strcmp(a, "expect_none")) {
			size_t got = count_lines(b);
			printf("MOCK: expect_none '%s': %zu\n", b, got);
			if (got)
				die(5, "expectation failed: '%s' seen %zu times, wanted none", b, got);
			continue;
		}
		if (!strcmp(a, "shot_first")) {
			snprintf(g_shot_first, sizeof(g_shot_first), "%.31s", b);
			continue;
		}
		if (!strcmp(a, "shots_every")) {
			g_shots_every = (unsigned)strtoul(b, NULL, 0);
			continue;
		}
		if (!strcmp(a, "connect") || !strcmp(a, "disconnect")) {
			g_connected[parse_ports(b) == 2 ? 1 : 0] = !strcmp(a, "connect");
			continue;
		}
		if (!strcmp(a, "quit"))
			die(0, "quit");
		die(2, "unknown script command: %s", a);
	}
}

void joypad_poll(void)
{
	g_polls++;
	g_prev[0] = g_held[0];
	g_prev[1] = g_held[1];
	g_held[0] = g_held[1] = 0;
	for (;;) {
		if (g_cmd.kind == S_IDLE || (g_cmd.kind != S_MONKEY && g_cmd.left == 0)) {
			if (g_cmd.kind == S_PRESS && --g_cmd.repeat > 0) {
				g_cmd.left = 4;
			} else {
				next_command();
				continue;
			}
		}
		break;
	}
	switch (g_cmd.kind) {
	case S_WAIT:
		g_cmd.left--;
		break;
	case S_PRESS:
		if (g_cmd.left > 2)
			for (int p = 0; p < 2; p++)
				if (g_cmd.ports & (1 << p))
					g_held[p] = g_cmd.buttons;
		g_cmd.left--;
		break;
	case S_MONKEY:
		if (g_cmd.until && g_until_hit) {
			printf("MOCK: until '%s' reached after %lu frames\n", g_cmd.until_text, g_cmd.until_max - g_cmd.left);
			g_until = NULL;
			g_cmd.kind = S_IDLE;
			joypad_poll(); /* start the next command in this same frame */
			g_polls--;
			return;
		}
		if (g_cmd.left == 0) {
			if (g_cmd.until)
				die(4, "until '%s': not reached after %lu frames", g_cmd.until_text, g_cmd.until_max);
			g_cmd.kind = S_IDLE;
			joypad_poll();
			g_polls--;
			return;
		}
		g_cmd.left--;
		if (g_cmd.phase == 0)
			g_cmd.cur = monkey_button();
		if (g_cmd.phase < 2)
			for (int p = 0; p < 2; p++)
				if (g_cmd.ports & (1 << p))
					g_held[p] = g_cmd.cur;
		g_cmd.phase = (g_cmd.phase + 1) % 4;
		break;
	default:
		break;
	}
}

static joypad_buttons_t to_buttons(unsigned m)
{
	joypad_buttons_t b = { 0 };
	b.a = (m >> B_A) & 1;
	b.b = (m >> B_B) & 1;
	b.z = (m >> B_Z) & 1;
	b.start = (m >> B_START) & 1;
	b.l = (m >> B_L) & 1;
	b.r = (m >> B_R) & 1;
	b.d_up = (m >> B_UP) & 1;
	b.d_down = (m >> B_DOWN) & 1;
	b.d_left = (m >> B_LEFT) & 1;
	b.d_right = (m >> B_RIGHT) & 1;
	b.c_up = (m >> B_CU) & 1;
	b.c_down = (m >> B_CD) & 1;
	b.c_left = (m >> B_CL) & 1;
	b.c_right = (m >> B_CR) & 1;
	return b;
}

joypad_buttons_t joypad_get_buttons_pressed(joypad_port_t port)
{
	if (port > JOYPAD_PORT_2 || !g_connected[port])
		return to_buttons(0);
	return to_buttons(g_held[port] & ~g_prev[port]);
}

joypad_buttons_t joypad_get_buttons_held(joypad_port_t port)
{
	if (port > JOYPAD_PORT_2 || !g_connected[port])
		return to_buttons(0);
	return to_buttons(g_held[port]);
}

joypad_inputs_t joypad_get_inputs(joypad_port_t port)
{
	joypad_inputs_t in = { 0 };
	in.btn = joypad_get_buttons_held(port);
	return in;
}
