/*
 * tcgn64: Nintendo 64 frontend (libdragon) for the portable Pokemon TCG duel engine.
 *
 * The engine runs a whole duel inside tcg_run_duel() and calls back into the
 * frontend (see port/src/tcg.h):
 *
 *   event()    is shown on screen (text box, coin tosses, damage, ...); a few
 *              frames are rendered for pacing, A/B fast-forwards them.
 *   request()  runs its own render/input loop until the player answers, one UI
 *              per TcgRequestType (what each needs mirrors tcgcli's human_answer).
 *
 * Rendering uses libdragon's CPU graphics API (graphics_draw_box / _text /
 * _sprite on the display surface, built-in 8x8 font) at 320x240x16bpp, input
 * comes from joypad_*. Card art (optional) is converted from the disassembly's
 * src/gfx/cards/NAME.png with mksprite at build time and loaded from the ROM
 * filesystem. The same file also builds on the host against mock/libdragon.h,
 * which renders into memory and feeds scripted controller input (make hosttest).
 *
 * Controls: A select/confirm, B cancel/back, D-pad or stick move, START card
 * details, Z check screen (duel menu), L/R page or switch tab.
 * Hot-seat: each duelist answers with their own controller (controller 2 for
 * the second duelist; controller 1 when no second controller is connected).
 */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libdragon.h>
#ifndef TCGN64_HOST_TEST
#include <font.h>
#endif

#include "tcg.h"
#include "common.h"
#include "gen/decks.h"
#include "gen/text.h"
#include "../common/save_store.h"
#include "save_store_n64.h"
#include "banner_font.h"
#include "body_font.h"
#include "card_back.h"
#include "duel_input.h"
#include "coin_art.h"
#ifdef TCGN64_ADVENTURE
#include "adventure/audio.h"
#endif

#ifdef TCGN64_CARD_ART
#include "cardart_table.h"
#endif

/* ------------------------------------------------------------------ layout */

#define SCREEN_W 320
#define SCREEN_H 240
#define CW 8                        /* font cell width */
#define LH 10                       /* text line height */
#define LEFT 16                     /* margins keep everything inside the TV-safe area */
#define RIGHT (SCREEN_W - 16)
#define COLS ((RIGHT - LEFT) / CW)  /* 36 text columns */
#define BOARD_Y 8
#define BOARD_H 182                 /* y 8..189: board or full panels */
#define TBOX_Y 191                  /* text box: y 191..231 */
#define TBOX_LINES 4
#define TLH 9                       /* text box line height */
#define TBOX_LINE(i) (TBOX_Y + 3 + (i) * TLH)
#define SLOT_INFO_Y 90              /* between the two halves of the board */
#define BODY_LH 13                  /* measured proportional duel text */
#define RULE_COLS 96
#define PRACTICE_PAGE_LINES 11
#define LIST_LH 14

enum {
	C_BG, C_MID, C_PANEL, C_PANEL2, C_BORDER, C_TEXT, C_DIM, C_HI, C_SEL, C_RED, C_GREEN,
	C_BLACK, C_HPBG, C_CARDBACK, C_FATAL,
	C_PAPER, C_INK, C_EDGE, C_PAPERLIGHT, C_PAPERSHADE, C_STORYSEL,
	C_PANELLIGHT, C_PANELSHADE,
	C_E0,                /* energy colors: FIRE GRASS LIGHTNING WATER FIGHTING PSYCHIC COLORLESS */
	C_T0 = C_E0 + 7,     /* darker type colors for card backgrounds */
	NUM_COLORS = C_T0 + 7
};

static const uint8_t kRGB[NUM_COLORS][3] = {
	[C_BG] = { 0x10, 0x30, 0x28 },     [C_MID] = { 0x30, 0x60, 0x50 },
	[C_PANEL] = { 0x18, 0x28, 0x40 },  [C_PANEL2] = { 0x24, 0x3d, 0x5b },
	[C_BORDER] = { 0x4a, 0x73, 0x9c }, [C_TEXT] = { 0xf7, 0xf7, 0xde },
	[C_DIM] = { 0xa9, 0xbc, 0xcc },    [C_HI] = { 0xff, 0xdd, 0x70 },
	[C_SEL] = { 0x28, 0x50, 0x7c },    [C_RED] = { 0xf0, 0x50, 0x40 },
	[C_GREEN] = { 0x40, 0xd0, 0x50 },  [C_BLACK] = { 0x00, 0x00, 0x00 },
	[C_HPBG] = { 0x40, 0x10, 0x10 },   [C_CARDBACK] = { 0x30, 0x50, 0xc0 },
	[C_FATAL] = { 0x80, 0x10, 0x10 },
	[C_PAPER] = { 0xf7, 0xf7, 0xde }, [C_INK] = { 0x18, 0x30, 0x50 },
	[C_EDGE] = { 0x4a, 0x73, 0x9c }, [C_PAPERLIGHT] = { 0xff, 0xff, 0xf7 },
	[C_PAPERSHADE] = { 0xa9, 0xbc, 0xcc }, [C_STORYSEL] = { 0x20, 0x48, 0x78 },
	[C_PANELLIGHT] = { 0x67, 0x90, 0xb1 }, [C_PANELSHADE] = { 0x0a, 0x12, 0x1e },
	[C_E0 + 0] = { 0xe8, 0x48, 0x30 }, [C_E0 + 1] = { 0x48, 0xb8, 0x40 },
	[C_E0 + 2] = { 0xf0, 0xd0, 0x20 }, [C_E0 + 3] = { 0x40, 0x88, 0xf0 },
	[C_E0 + 4] = { 0xc0, 0x78, 0x40 }, [C_E0 + 5] = { 0xb0, 0x58, 0xd0 },
	[C_E0 + 6] = { 0xe0, 0xe0, 0xe0 },
	[C_T0 + 0] = { 0x70, 0x20, 0x18 }, [C_T0 + 1] = { 0x20, 0x58, 0x20 },
	[C_T0 + 2] = { 0x70, 0x60, 0x10 }, [C_T0 + 3] = { 0x18, 0x38, 0x78 },
	[C_T0 + 4] = { 0x60, 0x38, 0x18 }, [C_T0 + 5] = { 0x50, 0x28, 0x68 },
	[C_T0 + 6] = { 0x50, 0x50, 0x50 },
};

/* FIRE GRASS LIGHTNING WATER FIGHTING PSYCHIC COLORLESS (the engine's color order) */
static const char kTypeLetter[] = "RGLWFPC";
static const char *const kTypeName[7] = { "Fire", "Grass", "Lightning", "Water", "Fighting", "Psychic", "Colorless" };

/* ------------------------------------------------------------------ frontend state */

enum { MODE_VS_AI, MODE_VS_BOT, MODE_HOTSEAT, MODE_PRACTICE, NUM_MODES };
enum { PK_HUMAN, PK_AI, PK_BOT };       /* who answers a duelist's requests */

static uint32_t g_col[NUM_COLORS];
static unsigned g_raster_width=SCREEN_W;
static surface_t *g_fb;
static unsigned g_frame;
static int g_mode;
static int g_kind[2];                   /* PK_* of TCG_PLAYER / TCG_OPPONENT */
static char g_name[2][16];
static bool g_have_ai;                  /* engine AI linked (buildinfo.txt) */
static char g_buildinfo[6][COLS + 1];
static int g_buildinfo_lines;
static uint32_t g_rng = 0x2545f491;

/* text log (TCG_EV_TEXT) */
#define LOG_LINES 40
static char g_log[LOG_LINES][COLS + 1];
static int g_log_n;                     /* lines in the log */
static int g_log_newest;                /* first line of the newest message */
static char g_practice_checklist[1024];
static bool g_practice_checklist_on;
static bool g_practice_reading_on;
static char g_practice_reading[24][RULE_COLS];
static int g_practice_reading_count;

/* things events asked us to show */
static int g_screen;                    /* last TCG_SCREEN_* */
static uint8_t g_screen_arg;            /* its arg[2] */
static uint8_t g_boxmsg = 0xff;
static struct { bool on; uint8_t id, deck, owner, page; } g_overlay;  /* TCG_EV_CARD */
static struct { uint8_t res[16], owner; int n, total, number; bool ready; } g_coin;
static struct { int timer; uint8_t side, loc; unsigned amount; } g_dmg;
static uint8_t g_thinking;
static uint8_t g_asking;                /* duelist answering the current request (0: none) */
static int g_denied;                    /* frames left of the "invalid choice" flash */
static bool g_save_ok;                  /* a 16 Kbit EEPROM is there (save_store_open) */
static uint8_t g_turn_holder;
static int g_turn;
static int g_result = -1;            /* -1 playing, otherwise winner side / 2 draw */

/* ------------------------------------------------------------------ small utilities */

static uint32_t rnd(uint32_t n)
{
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 17;
	g_rng ^= g_rng << 5;
	return n ? g_rng % n : 0;
}

static int side_of(uint8_t duelist) { return duelist == TCG_OPPONENT; }

/* bounded string copy (like strlcpy) */
static void copy_str(char *dst, size_t n, const char *src)
{
	size_t i = 0;
	if (!n)
		return;
	for (; src[i] && i < n - 1; i++)
		dst[i] = src[i];
	dst[i] = 0;
}

/* engine UTF-8 text -> 7-bit ASCII for the 8x8 debug font */
static void sanitize(const char *in, char *out, int n)
{
	static const struct { const char *from, *to; } map[] = {
		{ "\xc3\xa9", "e" }, { "\xc3\x89", "E" },
		{ "\xe2\x99\x82", "(M)" }, { "\xe2\x99\x80", "(F)" },
		{ "\xe2\x80\x9c", "\"" }, { "\xe2\x80\x9d", "\"" },
		{ "\xe2\x80\x98", "'" }, { "\xe2\x80\x99", "'" },
		{ "<FIRE>", "[R]" }, { "<GRASS>", "[G]" }, { "<LIGHTNING>", "[L]" }, { "<WATER>", "[W]" },
		{ "<FIGHTING>", "[F]" }, { "<PSYCHIC>", "[P]" }, { "<COLORLESS>", "[C]" },
		{ "<Lv>", "Lv" }, { "<No>", "No." },
	};
	int pos = 0;
	if (n <= 0)
		return;
	while (in && *in && pos < n - 1) {
		unsigned char c = (unsigned char)*in;
		if (c >= 0x80 || c == '<') {
			bool done = false;
			for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
				size_t l = strlen(map[i].from);
				if (!strncmp(in, map[i].from, l)) {
					for (const char *t = map[i].to; *t && pos < n - 1; t++)
						out[pos++] = *t;
					in += l;
					done = true;
					break;
				}
			}
			if (done)
				continue;
		}
		if (c == '\n') {
			out[pos++] = '\n';
			in++;
		} else if (c == '\t') {
			out[pos++] = ' ';
			in++;
		} else if (c < 0x20 || c == 0x7f) {
			in++; /* stray control byte */
		} else if (c < 0x80) {
			out[pos++] = (char)c;
			in++;
		} else {
			int len = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
			while (len-- && *in)
				in++;
			out[pos++] = '?';
		}
	}
	out[pos] = 0;
}

/* split text into lines of at most `width` columns (wrapping at spaces) */
static int wrap(const char *s, int width, char (*lines)[COLS + 1], int max)
{
	int n = 0;
	if (width > COLS)
		width = COLS;
	while (*s && n < max) {
		int len = 0, brk = -1;
		while (s[len] && s[len] != '\n' && len < width) {
			if (s[len] == ' ')
				brk = len;
			len++;
		}
		int take = len, skip = 0;
		if (s[len] && s[len] != '\n' && brk > 0) {
			take = brk; /* wrap at the last space */
			skip = 1;
		}
		memcpy(lines[n], s, (size_t)take);
		lines[n][take] = 0;
		n++;
		s += take + skip;
		if (*s == '\n' && !skip)
			s++;
	}
	return n;
}

static void text_of(uint16_t id, char *out, int n)
{
	sanitize(tcg_text(id), out, n);
}

/* card description text with the Game Boy line breaks removed (reflowed for the wider screen) */
static void description(uint16_t id, char *out, int n)
{
	text_of(id, out, n);
	for (char *p = out; *p; p++)
		if (*p == '\n' && (p == out || p[-1] != '\n') && p[1] != '\n')
			*p = ' ';
}

/* both description pages of an attack, reflowed */
static void attack_description(const struct AtkData *atk, char *out, int n)
{
	out[0] = 0;
	for (int page = 0; page < 2; page++) {
		uint16_t t = rd16(&atk->Description[page * 2]);
		size_t len = strlen(out);
		if (!t || (int)len + 2 >= n)
			continue;
		if (len)
			out[len++] = ' ';
		description(t, out + len, n - (int)len);
	}
}

static void formatted_text(uint16_t id, char *out, int n)
{
	char tmp[512];
	tcg_format_text(id, tmp, sizeof(tmp));
	sanitize(tmp, out, n);
}

/* ------------------------------------------------------------------ cards and board state */

static const struct CardData *card_data(uint8_t id)
{
	return (const struct CardData *)tcg_card_data(id);
}

static bool is_pokemon(const struct CardData *c) { return c->Type < TYPE_ENERGY; }
static bool is_energy(const struct CardData *c) { return c->Type >= TYPE_ENERGY && c->Type < TYPE_TRAINER; }

static void card_name(uint8_t id, char *out, int n)
{
	text_of(rd16(card_data(id)->Name), out, n);
}

static int card_color(const struct CardData *c)
{
	if (c->Type < TYPE_ENERGY)
		return c->Type & 7;
	if (c->Type < TYPE_ENERGY_DOUBLE_COLORLESS)
		return c->Type - TYPE_ENERGY;
	return COLORLESS;
}

typedef struct Slot {
	uint8_t deck;            /* deck index */
	uint8_t id;              /* card id */
	uint8_t hp, maxhp;
	uint8_t energy[7];       /* attached energy units per color */
	uint8_t total;
	uint8_t color;
	bool pluspower, defender;
} Slot;

static int num_in_play(uint8_t duelist)
{
	int n = tcg_duelvars(duelist)[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA];
	return n > MAX_PLAY_AREA_POKEMON ? MAX_PLAY_AREA_POKEMON : n;
}

static bool get_slot(uint8_t duelist, int loc, Slot *s)
{
	const uint8_t *v = tcg_duelvars(duelist);
	memset(s, 0, sizeof(*s));
	if (loc < 0 || loc >= num_in_play(duelist))
		return false;
	s->deck = v[DUELVARS_ARENA_CARD + loc];
	if (s->deck >= DECK_SIZE)
		return false;
	s->id = tcg_card_id(duelist, s->deck);
	const struct CardData *c = card_data(s->id);
	s->hp = v[DUELVARS_ARENA_CARD_HP + loc];
	s->maxhp = is_pokemon(c) ? c->HP : 10; /* Clefairy Doll / Mysterious Fossil */
	s->color = (uint8_t)card_color(c);
	if (v[DUELVARS_ARENA_CARD_CHANGED_TYPE + loc] & HAS_CHANGED_COLOR)
		s->color = v[DUELVARS_ARENA_CARD_CHANGED_TYPE + loc] & 0x7;
	if (s->color > COLORLESS)
		s->color = COLORLESS;
	for (int i = 0; i < DECK_SIZE; i++) {
		if (v[DUELVARS_CARD_LOCATIONS + i] != CARD_LOCATION_ARENA + loc)
			continue;
		const struct CardData *e = card_data(tcg_card_id(duelist, (uint8_t)i));
		if (!is_energy(e))
			continue;
		if (e->Type == TYPE_ENERGY_DOUBLE_COLORLESS) {
			s->energy[COLORLESS] += 2;
			s->total += 2;
		} else {
			s->energy[card_color(e)]++;
			s->total++;
		}
	}
	s->pluspower = v[DUELVARS_ARENA_CARD_ATTACHED_PLUSPOWER + loc] != 0;
	s->defender = v[DUELVARS_ARENA_CARD_ATTACHED_DEFENDER + loc] != 0;
	return true;
}

static void status_text(uint8_t duelist, char *out, int n)
{
	uint8_t st = tcg_duelvars(duelist)[DUELVARS_ARENA_CARD_STATUS];
	const char *a = "";
	switch (st & CNF_SLP_PRZ) {
	case CONFUSED: a = "CNF "; break;
	case ASLEEP: a = "SLP "; break;
	case PARALYZED: a = "PRZ "; break;
	}
	const char *p = (st & DOUBLE_POISONED) == DOUBLE_POISONED ? "PSNx2" : (st & POISONED) ? "PSN" : "";
	snprintf(out, (size_t)n, "%s%s", a, p);
}

static int prizes_left(uint8_t duelist)
{
	uint8_t m = tcg_duelvars(duelist)[DUELVARS_PRIZES];
	int n = 0;
	for (; m; m >>= 1)
		n += m & 1;
	return n;
}

/* does the attached energy pay for this attack (same rules as CheckIfEnoughEnergiesForGivenAttack)? */
static bool can_pay(const struct AtkData *atk, const Slot *s)
{
	int needed = 0;
	for (int c = 0; c < 6; c++) {
		int want = c & 1 ? atk->EnergyCost[c / 2] & 0xf : atk->EnergyCost[c / 2] >> 4;
		if (want > s->energy[c])
			return false;
		needed += want;
	}
	int colorless = atk->EnergyCost[3] >> 4;
	return s->total - needed >= colorless;
}

/* ------------------------------------------------------------------ drawing */

/* The layout remains in 320x240 TV coordinates. Rasterization and the cached
 * canvas use the active physical resolution, including true 480x360 glyphs. */
static int ui_x(int x) { return x*(int)g_raster_width/SCREEN_W; }
/* Dialogues and popups can use partial-height or partial-width canvases. */
static int ui_y(int y) { return y*(int)g_raster_width/SCREEN_W; }
static bool ui_high_res(void) { return g_raster_width==480; }

static void fill(int x, int y, int w, int h, int c)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > SCREEN_W) w = SCREEN_W - x;
	if (y + h > SCREEN_H) h = SCREEN_H - y;
	if (w <= 0 || h <= 0)
		return;
	int px=ui_x(x),py=ui_y(y);
	graphics_draw_box(g_fb,px,py,ui_x(x+w)-px,ui_y(y+h)-py,g_col[c]);
}

static void rect(int x, int y, int w, int h, int c)
{
	fill(x, y, w, 1, c);
	fill(x, y + h - 1, w, 1, c);
	fill(x, y, 1, h, c);
	fill(x + w - 1, y, 1, h, c);
}

static void panel(int x, int y, int w, int h)
{
	fill(x, y, w, h, C_PANEL);
	rect(x, y, w, h, C_BORDER);
	fill(x + 1, y + 1, w - 2, 1, C_PANELLIGHT);
	fill(x + 1, y + 1, 1, h - 2, C_PANELLIGHT);
	fill(x + 1, y + h - 2, w - 2, 1, C_PANELSHADE);
	fill(x + w - 2, y + 1, 1, h - 2, C_PANELSHADE);
}

static void paper_panel(int x, int y, int w, int h)
{
	fill(x, y, w, h, C_PAPER);
	rect(x, y, w, h, C_EDGE);rect(x + 1, y + 1, w - 2, h - 2, C_EDGE);
	fill(x + 2, y + 2, w - 4, 1, C_PAPERLIGHT);
	fill(x + 2, y + 2, 1, h - 4, C_PAPERLIGHT);
	fill(x + 2, y + h - 3, w - 4, 1, C_PAPERSHADE);
	fill(x + w - 3, y + 2, 1, h - 4, C_PAPERSHADE);
}

/* draw at most `max` characters of one line (clipped to the screen) */
static void compact_text(int x,int y,uint32_t color,const char *text,int limit)
{
	if(!ui_high_res()) {
		char line[SCREEN_W/CW+1];int count=0;
		if(limit>g_fb->width)limit=g_fb->width;
		if(x<0||y<0||y+8>g_fb->height)return;
		while(text[count]&&text[count]!='\n'&&count<SCREEN_W/CW&&x+(count+1)*CW<=limit) {
			line[count]=text[count];count++;
		}
		line[count]=0;graphics_set_color(color,0);graphics_draw_text(g_fb,x,y,line);return;
	}
	for(;*text&&*text!='\n';text++,x+=CW) {
		unsigned ch=(unsigned char)*text;if(ch<32||ch>126)ch='?';
		if(x+CW>limit)break;
		for(int row=0;row<8;row++) {
#ifdef TCGN64_HOST_TEST
			unsigned bits=mock_font_row(ch,(unsigned)row);
#else
			unsigned bits=__font_data[ch*8+(unsigned)row];
#endif
			for(int col=0;col<8;col++)if(bits&(0x80>>col)) {
				int px=ui_x(x+col),py=ui_y(y+row);
				int w=ui_x(x+col+1)-px,h=ui_y(y+row+1)-py;
				if(px<0||py<0||px+w>g_fb->width||py+h>g_fb->height)continue;
				graphics_draw_box(g_fb,px,py,w,h,color);
			}
		}
	}
}

static void print_n(int x, int y, int c, const char *s, int max)
{
	char buf[COLS + 8];
	int n = 0;
	if (y < 0 || y + 8 > SCREEN_H || x < 0)
		return;
	int room = (SCREEN_W - x) / CW;
	if (max > room)
		max = room;
	if (max > (int)sizeof(buf) - 1)
		max = (int)sizeof(buf) - 1;
	for (; *s && *s != '\n' && n < max; s++) {
		unsigned char ch = (unsigned char)*s;
		buf[n++] = ch >= 0x20 && ch < 0x7f ? (char)ch : '?';
	}
	buf[n] = 0;
	if (!n)
		return;
	if(c != C_BLACK && x + n * CW < SCREEN_W && y + 8 < SCREEN_H) {
		compact_text(x+1,y+1,g_col[c==C_INK?C_PAPERSHADE:C_PANELSHADE],buf,SCREEN_W);
	}
	compact_text(x,y,g_col[c],buf,SCREEN_W);
}

static void print(int x, int y, int c, const char *s) { print_n(x, y, c, s, COLS + 4); }

static void printf_at(int x, int y, int c, const char *fmt, ...)
{
	char buf[96];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	print(x, y, c, buf);
}

/* Proportional text is confined to duel headings, choices and inspection.
 * The bench and the engine log retain their compact, fixed-cell face. */
static int body_advance(unsigned ch)
{
	if(ch<32||ch>126)ch='?';
	return body_font_advance[ch-BODY_FONT_FIRST];
}

static int body_width(const char *text)
{
	int width=0;
	for(;*text&&*text!='\n';text++)width+=body_advance((unsigned char)*text);
	return width;
}

static void raster_glyph(int x,int y,uint16_t foreground,const uint8_t *mask,int width,int height,int limit)
{
	for(int row=0;row<height;row++)for(int col=0;col<width;col++) {
		int p=row*width+col;
		unsigned coverage=(mask[p/2]>>(p&1?0:4))&15;
		int px=x+col,py=y+row;
		if(!coverage||px<0||px>=limit||px>=g_fb->width||py<0||py>=g_fb->height)continue;
		uint16_t *pixel=(uint16_t *)((uint8_t *)g_fb->buffer+py*g_fb->stride)+px;
		uint16_t background=*pixel;
		unsigned r=(((foreground>>11)&31)*coverage+((background>>11)&31)*(15-coverage)+7)/15;
		unsigned g=(((foreground>>6)&31)*coverage+((background>>6)&31)*(15-coverage)+7)/15;
		unsigned b=(((foreground>>1)&31)*coverage+((background>>1)&31)*(15-coverage)+7)/15;
		*pixel=(uint16_t)((r<<11)|(g<<6)|(b<<1)|1);
	}
}

static void body_run(int x,int y,uint32_t color,const char *text,int limit)
{
	for(;*text&&*text!='\n';text++) {
		unsigned ch=(unsigned char)*text;if(ch<32||ch>126)ch='?';
		int advance=body_advance(ch);
		if(x+advance>limit)break;
		unsigned glyph=ch-BODY_FONT_FIRST;
		if(ui_high_res())raster_glyph(ui_x(x),ui_y(y),(uint16_t)color,body_hi_coverage[glyph],BODY_HI_WIDTH,BODY_HI_HEIGHT,ui_x(limit));
		else raster_glyph(x,y,(uint16_t)color,body_font_coverage[glyph],BODY_FONT_WIDTH,BODY_FONT_HEIGHT,limit);
		x+=advance;
	}
}

static void body_mask(int x,int y,int color,const char *text,int limit)
{ body_run(x,y,g_col[color],text,limit); }

static void body(int x,int y,int color,const char *text,int width)
{
	int limit=x+width;if(limit>SCREEN_W)limit=SCREEN_W;
	/* Paper already supplies contrast; dark panels receive a one-pixel shade. */
	if(color!=C_INK&&color!=C_BLACK)body_mask(x+1,y+1,C_PANELSHADE,text,limit);
	body_mask(x,y,color,text,limit);
}

static void bodyf(int x,int y,int color,int width,const char *fmt,...)
{
	char text[128];va_list ap;va_start(ap,fmt);
	vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);
	body(x,y,color,text,width);
}

/* Pixel-width wrapping also bounds the destination in bytes. A single long
 * word is split, never discarded; spaces/newlines retain ordinary wrapping. */
static int body_wrap(const char *text,int width,char lines[][RULE_COLS],int capacity)
{
	int count=0;
	while(*text&&count<capacity) {
		int length=0,pixels=0,brk=-1;
		while(text[length]&&text[length]!='\n'&&length<RULE_COLS-1) {
			int next=body_advance((unsigned char)text[length]);
			if(pixels+next>width&&length)break;
			if(text[length]==' ')brk=length;
			pixels+=next;length++;
		}
		int take=length,skip=0;
		if(text[length]&&text[length]!='\n'&&brk>0){take=brk;skip=1;}
		memcpy(lines[count],text,(size_t)take);lines[count++][take]=0;
		text+=take+skip;
		if(*text=='\n'&&!skip)text++;
	}
	return count;
}

static void focus_row(int x,int y,int width,int height)
{
	fill(x,y,width,height,C_HI);
	fill(x+1,y+1,width-2,height-2,C_PAPER);
	/* A solid arrow reads without relying on the gold hue. */
	for(int i=0;i<4;i++)fill(x+3+i,y+height/2-3+i,1,7-2*i,C_INK);
}

static void chosen_corners(int x,int y,int width,int height)
{
	/* Kept after a card has been chosen, independent of the action cursor. */
	for(int i=0;i<2;i++) {
		int px=i?x+width-1:x,py=i?y+height-1:y;
		fill(i?px-7:px,py,8,2,C_HI);
		fill(px,i?py-7:py,2,8,C_HI);
	}
}

static void preview_ellipsis(char *text,int width)
{
	size_t length=strlen(text);
	while(length&&body_width(text)+body_width("...")>width)text[--length]=0;
	while(length&&text[length-1]==' ')text[--length]=0;
	if(length+3<RULE_COLS)strcat(text,"...");
}

/* colored 8x8 energy symbol with its letter */
static void energy_icon(int x, int y, int color)
{
	fill(x, y, 8, 8, C_E0 + color);
	char s[2] = { kTypeLetter[color], 0 };
	print_n(x, y, color == LIGHTNING || color == COLORLESS ? C_BLACK : C_TEXT, s, 1);
}

static int energy_icons(int x, int y, const uint8_t *energy, int max)
{
	int n = 0;
	for (int c = 0; c < 7; c++)
		for (int i = 0; i < energy[c] && n < max; i++, n++)
			energy_icon(x + n * 9, y, c);
	return n;
}

/* energy cost of an attack as icons; returns the width */
static int cost_icons(int x, int y, const struct AtkData *atk)
{
	uint8_t e[7] = { 0 };
	for (int c = 0; c < 6; c++)
		e[c] = c & 1 ? atk->EnergyCost[c / 2] & 0xf : atk->EnergyCost[c / 2] >> 4;
	e[COLORLESS] = atk->EnergyCost[3] >> 4;
	return energy_icons(x, y, e, 8) * 9;
}

static void hp_bar(int x, int y, int w, int hp, int maxhp)
{
	fill(x, y, w, 4, C_HPBG);
	if (maxhp > 0 && hp > 0) {
		int f = hp * w / maxhp;
		if (f > w)
			f = w;
		fill(x, y, f, 4, hp * 4 <= maxhp ? C_RED : C_GREEN);
	}
}

/* ---- card art (optional) */

#ifdef TCGN64_CARD_ART
#define ART_CACHE 24
static struct { uint8_t id; sprite_t *spr; unsigned used; } g_art[ART_CACHE];

static const char *art_name(uint8_t id)
{
	uint16_t gfx = rd16(card_data(id)->Gfx);
	int lo = 0, hi = NUM_CARD_ART - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (kCardArt[mid].gfx == gfx)
			return kCardArt[mid].name;
		if (kCardArt[mid].gfx < gfx)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return NULL;
}

static sprite_t *art_get(uint8_t id)
{
	static unsigned clock;
	int victim = -1;
	for (int i = 0; i < ART_CACHE; i++) {
		if (g_art[i].spr && g_art[i].id == id) {
			g_art[i].used = ++clock;
			return g_art[i].spr;
		}
	}
	/* a free entry, else the least recently used one */
	for (int i = 0; i < ART_CACHE; i++) {
		if (!g_art[i].spr) {
			victim = i;
			break;
		}
		if (victim < 0 || g_art[i].used < g_art[victim].used)
			victim = i;
	}
	const char *name = art_name(id);
	if (!name)
		return NULL;
	char path[64];
	snprintf(path, sizeof(path), "rom:/cards/%s.sprite", name);
	if (g_art[victim].spr)
		sprite_free(g_art[victim].spr);
	g_art[victim].spr = sprite_load(path);
	g_art[victim].id = id;
	g_art[victim].used = ++clock;
	return g_art[victim].spr;
}
#endif

#ifdef TCGN64_CARD_ART
/* Source card art is the original 64x48 image. Scale that image into the
 * physical destination; every other UI element is rasterized at destination
 * resolution instead of enlarging a completed 320x240 framebuffer. */
static void sprite_logical(int x,int y,int width,int height,sprite_t *sprite)
{
#ifdef TCGN64_HOST_TEST
	const uint16_t *pixels=sprite->pixels;unsigned stride=sprite->width;
#else
	surface_t source=sprite_get_pixels(sprite);
	const uint16_t *pixels=source.buffer;unsigned stride=source.stride/2;
#endif
	int px=ui_x(x),py=ui_y(y),w=ui_x(x+width)-px,h=ui_y(y+height)-py;
	for(int row=0;row<h;row++) {
		int dy=py+row;if(dy<0||dy>=g_fb->height)continue;
		uint16_t *dest=(uint16_t *)((uint8_t *)g_fb->buffer+dy*g_fb->stride);
		const uint16_t *src=pixels+(row*sprite->height/h)*stride;
		for(int col=0;col<w;col++) {
			int dx=px+col;if(dx<0||dx>=g_fb->width)continue;
			uint16_t color=src[col*sprite->width/w];if(color&1)dest[dx]=color;
		}
	}
}
#endif

/* 64x48 card picture (or a placeholder with the type letter) */
static void card_art(int x, int y, uint8_t id)
{
#ifdef TCGN64_CARD_ART
	sprite_t *s = art_get(id);
	if (s) {
		if(ui_high_res())sprite_logical(x,y,64,48,s);
		else graphics_draw_sprite(g_fb,x,y,s);
		rect(x - 1, y - 1, 66, 50, C_BORDER);
		return;
	}
#endif
	const struct CardData *c = card_data(id);
	int color = card_color(c);
	fill(x, y, 64, 48, C_T0 + color);
	rect(x - 1, y - 1, 66, 50, C_BORDER);
	energy_icon(x + 28, y + 20, color);
}

static void bench_art(int x,int y,uint8_t id)
{
#ifdef TCGN64_CARD_ART
	sprite_t *sprite=art_get(id);
	if(sprite) {
		/* Cartridge card sprites and the offscreen UI canvas are RGBA16.
		   Quarter-size art identifies a bench card without hiding its stats. */
		sprite_logical(x,y,16,12,sprite);
		return;
	}
#endif
	fill(x,y,16,12,C_T0+card_color(card_data(id)));
	energy_icon(x+4,y+2,card_color(card_data(id)));
}

static void damage_number(int x,int y,unsigned amount)
{
	char text[12];snprintf(text,sizeof(text),"-%u",amount);
	print(x-1,y,C_BLACK,text);print(x+1,y,C_BLACK,text);
	print(x,y-1,C_BLACK,text);print(x,y+1,C_BLACK,text);print(x,y,C_TEXT,text);
}

typedef struct {
	uint16_t *rows;
	unsigned width, height, raster_width, row_pixels, period;
} FeltCache;
static FeltCache g_felt;

static void draw_felt(void)
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
	int left=ui_x(LEFT-6),right=ui_x(RIGHT+6);
	if(!g_felt.rows||g_felt.width!=g_fb->width||g_felt.height!=g_fb->height||
	   g_felt.raster_width!=g_raster_width) {
		/* floor(y*240/height) modulo 32 repeats every 32 physical rows at
		 * 320x240, or 48 at 480x360. Cache those rows (19,200/43,200 bytes)
		 * rather than a complete board-sized backing. */
		unsigned numerator=32u*g_fb->height,divisor=SCREEN_H;
		for(unsigned remainder;divisor;numerator=divisor,divisor=remainder)
			remainder=numerator%divisor;
		unsigned period=32u*g_fb->height/numerator;
		unsigned row_pixels=(unsigned)(right-left);
		free(g_felt.rows);
		g_felt=(FeltCache){.width=g_fb->width,.height=g_fb->height,
			.raster_width=g_raster_width,.row_pixels=row_pixels,.period=period};
		g_felt.rows=malloc(period*row_pixels*sizeof(*g_felt.rows));
		assert(g_felt.rows);
		uint8_t columns[row_pixels];
		for(unsigned x=0;x<row_pixels;x++)
			columns[x]=((x+(unsigned)left)*SCREEN_W/g_fb->width)&31;
		for(unsigned y=0;y<period;y++) {
			const uint16_t *tile_row=tile[(y*SCREEN_H/g_fb->height)&31];
			for(unsigned x=0;x<row_pixels;x++)
				g_felt.rows[y*row_pixels+x]=tile_row[columns[x]];
		}
	}
	/* Copy only into the reserved CPU canvas. end_frame presents it through
	   RDP after cache writeback, exactly like the rest of the duel UI.
	   The steady loop has no pixel scaling, division, or modulo. */
	int top=ui_y(BOARD_Y),bottom=ui_y(BOARD_Y+BOARD_H);
	unsigned cached_y=(unsigned)top;
	while(cached_y>=g_felt.period)cached_y-=g_felt.period;
	const uint16_t *source=g_felt.rows+cached_y*g_felt.row_pixels;
	const uint16_t *end=g_felt.rows+g_felt.period*g_felt.row_pixels;
	uint8_t *destination=(uint8_t *)g_fb->buffer+top*g_fb->stride+left*sizeof(*source);
	for(int y=top;y<bottom;y++) {
		memcpy(destination,source,g_felt.row_pixels*sizeof(*source));
		destination+=g_fb->stride;source+=g_felt.row_pixels;
		if(source==end)source=g_felt.rows;
	}
}

/* ---- board */

static void draw_side(uint8_t duelist, bool top, int hl_loc)
{
	const uint8_t *v = tcg_duelvars(duelist);
	int side = side_of(duelist);
	char buf[64], name[24];
	/* top half y 8..89 (bar, bench, active), bottom half y 99..189 (active, bench, bar) */
	int bar_y = top ? BOARD_Y : BOARD_Y + 173;
	int bench_y = top ? BOARD_Y + 10 : BOARD_Y + 141;
	int act_y = top ? BOARD_Y + 42 : BOARD_Y + 93;
	int act_art_y = top ? BOARD_Y + 40 : BOARD_Y + 91;

	/* info bar */
	bool turn = g_turn_holder == duelist;
	fill(LEFT - 2, bar_y - 1, RIGHT - LEFT + 4, 9, turn ? C_PANEL2 : C_BG);
	printf_at(LEFT, bar_y, turn ? C_HI : C_TEXT, "%-7.7s", g_name[side]);
	int deck = DECK_SIZE - v[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK];
	if (deck < 0)
		deck = 0;
	printf_at(LEFT + 8 * CW, bar_y, C_DIM, "Hand%d Deck%d Disc%d",
	          v[DUELVARS_NUMBER_OF_CARDS_IN_HAND], deck, v[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE]);
	int pz = prizes_left(duelist);
	print(LEFT + 31 * CW, bar_y, C_DIM, "P");
	for (int i = 0; i < 6; i++)
		fill(LEFT + 32 * CW + i * 6, bar_y + 1, 5, 7, i < pz ? C_CARDBACK : C_MID);

	/* bench: 5 mini cards */
	for (int b = 1; b <= MAX_BENCH_POKEMON; b++) {
		int x = LEFT + (b - 1) * 58;
		Slot s;
		if (!get_slot(duelist, b, &s)) {
			/* Quiet corner marks keep empty slots below occupied cards. */
			fill(x,bench_y,7,1,C_MID);fill(x,bench_y,1,5,C_MID);
			fill(x+49,bench_y+29,7,1,C_MID);fill(x+55,bench_y+25,1,5,C_MID);
			continue;
		}
		fill(x, bench_y, 56, 30, C_T0 + s.color);
		rect(x, bench_y, 56, 30, hl_loc == b ? C_HI : C_BORDER);
		if (hl_loc == b)
			rect(x + 1, bench_y + 1, 54, 28, C_HI);
		card_name(s.id, name, sizeof(name));
		print_n(x + 2, bench_y + 2, C_TEXT, name, 6);
		bench_art(x+2,bench_y+11,s.id);
		printf_at(x + 24, bench_y + 11, s.hp * 4 <= s.maxhp ? C_RED : C_TEXT, "%d", s.hp);
		hp_bar(x+22,bench_y+20,30,s.hp,s.maxhp);
		uint8_t e[7];
		memcpy(e, s.energy, sizeof(e));
		/* compact energy line: up to 6 small squares */
		int k = 0;
		for (int c = 0; c < 7; c++)
			for (int i = 0; i < e[c] && k < 9; i++, k++)
				fill(x + 2 + k * 6, bench_y + 25, 5, 3, C_E0 + c);
		if (g_dmg.timer && side_of(g_dmg.side) == side && g_dmg.loc == b) {
			fill(x + 20, bench_y + 9, 34, 12, C_RED);
			damage_number(x + 22, bench_y + 11, g_dmg.amount);
		}
	}

	/* active Pokemon */
	Slot s;
	if (!get_slot(duelist, 0, &s)) {
		print(LEFT, act_y + LH, C_DIM, "(no active Pokemon)");
		return;
	}
	const struct CardData *c = card_data(s.id);
	/* Felt -> blue player mat -> paper Active card. The art keeps its full
	 * 64x48 allocation; the quieter name strip gives text a stable backing. */
	fill(LEFT-4,act_y+1,RIGHT-LEFT+8,47,C_PANELSHADE);
	fill(LEFT-4,act_y-1,RIGHT-LEFT+8,47,C_PANEL2);
	fill(LEFT-2,act_y,RIGHT-LEFT-68,45,C_PAPER);
	fill(LEFT-2,act_y,RIGHT-LEFT-68,13,C_PAPERSHADE);
	if(hl_loc==0) {
		rect(LEFT-4,act_y-1,RIGHT-LEFT+8,47,C_HI);
		chosen_corners(LEFT-5,act_y-2,RIGHT-LEFT+10,49);
	}
	card_name(s.id, name, sizeof(name));
	energy_icon(LEFT, act_y+2, s.color);
	body(LEFT+12,act_y+2,C_INK,name,156);
	if(is_pokemon(c)&&c->Level)printf_at(LEFT+174,act_y+2,C_INK,"Lv%d",c->Level);
	hp_bar(LEFT,act_y+19,96,s.hp,s.maxhp);
	bodyf(LEFT+104,act_y+17,s.hp*4<=s.maxhp?C_RED:C_INK,112,"HP %d/%d",s.hp,s.maxhp);
	int n = energy_icons(LEFT, act_y + 27, s.energy, 12);
	if (!n)
		print(LEFT, act_y + 27, C_INK, "no energy");
	if(s.total>12)printf_at(LEFT+12*9,act_y+27,C_INK,"+%d",s.total-12);
	status_text(duelist, buf, sizeof(buf));
	int sx = LEFT;
	if (buf[0]) {
		fill(sx - 1, act_y + 36, (int)strlen(buf) * CW + 2, 9, C_RED);
		print(sx, act_y + 36, C_TEXT, buf);
		sx += ((int)strlen(buf) + 1) * CW;
	}
	if (s.pluspower) {
		print(sx, act_y + 36, C_INK, "+Power");
		sx += 7 * CW;
	}
	if (s.defender)
		print(sx, act_y + 36, C_INK, "Defender");
	card_art(RIGHT - 64, act_art_y, s.id);
	if (g_dmg.timer && side_of(g_dmg.side) == side && g_dmg.loc == 0) {
		fill(RIGHT - 52, act_art_y + 16, 40, 16, C_RED);
		damage_number(RIGHT - 48, act_art_y + 20, g_dmg.amount);
	}
}

/* the text box: the latest lines of the log (older messages dimmed); with a
 * hint, the last line shows the controls instead */
static void draw_textbox_hint(const char *hint)
{
	int lines = hint ? TBOX_LINES - 1 : TBOX_LINES;
	panel(LEFT - 6, TBOX_Y, RIGHT - LEFT + 12, TBOX_LINES * TLH + 5);
	int first = g_log_n - lines;
	if (first < 0)
		first = 0;
	/* keep the start of a long newest message visible */
	if (g_log_n - g_log_newest > lines)
		first = g_log_newest;
	for (int i = first; i < g_log_n && i < first + lines; i++)
		print(LEFT, TBOX_LINE(i - first), i >= g_log_newest ? C_TEXT : C_DIM, g_log[i]);
	if (hint) {
		int w = (int)strlen(hint);
		print(RIGHT - w * CW, TBOX_LINE(TBOX_LINES - 1), C_HI, hint);
		if (g_mode == MODE_HOTSEAT && g_asking && w <= COLS - 5) /* whose controller is listening */
			print(LEFT, TBOX_LINE(TBOX_LINES - 1), C_GREEN, g_name[side_of(g_asking)]);
	}
}

static void draw_textbox(void) { draw_textbox_hint(NULL); }

static bool practice_checklist_visible(void)
{
	return g_mode==MODE_PRACTICE&&g_screen==TCG_SCREEN_EMPTY&&
		g_practice_checklist_on&&g_practice_checklist[0];
}

static void draw_practice_checklist(void)
{
	char heading[96],lines[18][RULE_COLS];
	const char *instructions=strchr(g_practice_checklist,'\n');
	size_t length=instructions?(size_t)(instructions-g_practice_checklist):strlen(g_practice_checklist);
	if(length>=sizeof(heading))length=sizeof(heading)-1;
	memcpy(heading,g_practice_checklist,length);heading[length]=0;
	paper_panel(LEFT-6,BOARD_Y,RIGHT-LEFT+12,BOARD_H);
	body(LEFT+3,BOARD_Y+8,C_INK,heading,RIGHT-LEFT-6);
	fill(LEFT+2,BOARD_Y+24,RIGHT-LEFT-4,1,C_EDGE);
	if(instructions) {
		while(*instructions=='\n')instructions++;
		int count=body_wrap(instructions,RIGHT-LEFT-10,lines,18);
		for(int i=0;i<count&&BOARD_Y+32+i*BODY_LH+BODY_FONT_HEIGHT<BOARD_Y+BOARD_H-18;i++)
			body(LEFT+4,BOARD_Y+32+i*BODY_LH,C_INK,lines[i],RIGHT-LEFT-10);
	}
	body(LEFT+4,BOARD_Y+BOARD_H-15,C_EDGE,"Dr. Mason",RIGHT-LEFT-24);
}

static bool practice_reading_visible(void)
{
	return g_mode==MODE_PRACTICE&&g_practice_reading_on;
}

static void set_practice_reading(const TcgEvent *ev)
{
	char message[1536];sanitize(ev->str?ev->str:"",message,sizeof(message));
	if(ev->text==LetsPlayTheGamePracticeDuelText&&g_practice_checklist[0]) {
		const char *steps=strchr(g_practice_checklist,'\n');
		if(steps) {
			while(*steps=='\n')steps++;
			size_t used=strlen(message);
			snprintf(message+used,sizeof(message)-used,"\n\n%s",steps);
		}
	}
	g_practice_reading_count=body_wrap(message,RIGHT-LEFT-12,g_practice_reading,24);
}

static void draw_practice_reading(int first)
{
	paper_panel(LEFT-6,BOARD_Y,RIGHT-LEFT+12,BOARD_H);
	body(LEFT+4,BOARD_Y+8,C_INK,"Dr. Mason",100);
	if(g_practice_checklist_on&&g_practice_checklist[0]) {
		char heading[96];size_t length=strcspn(g_practice_checklist,"\n");
		if(length>=sizeof(heading))length=sizeof(heading)-1;
		memcpy(heading,g_practice_checklist,length);heading[length]=0;
		int width=body_width(heading);body(RIGHT-4-width,BOARD_Y+8,C_INK,heading,width);
	}
	fill(LEFT+3,BOARD_Y+24,RIGHT-LEFT-6,1,C_EDGE);
	for(int i=0;i<PRACTICE_PAGE_LINES&&first+i<g_practice_reading_count;i++)
		body(LEFT+4,BOARD_Y+32+i*12,C_INK,g_practice_reading[first+i],RIGHT-LEFT-12);
}

static void draw_practice_continue(int first)
{
	panel(LEFT-6,TBOX_Y,RIGHT-LEFT+12,TBOX_LINES*TLH+5);
	print(LEFT,TBOX_LINE(1),C_HI,"A/B: continue");
	int pages=(g_practice_reading_count+PRACTICE_PAGE_LINES-1)/PRACTICE_PAGE_LINES;
	if(pages>1)printf_at(RIGHT-6*CW,TBOX_LINE(1),C_DIM,"%d / %d",first/PRACTICE_PAGE_LINES+1,pages);
}

/* the board: opponent (TCG_OPPONENT) on top, player at the bottom */
static void draw_board(uint8_t hl_duelist, int hl_loc)
{
	draw_felt();
	fill(LEFT - 4, SLOT_INFO_Y + 4, RIGHT - LEFT + 8, 1, C_MID);
	draw_side(TCG_OPPONENT, true, hl_duelist == TCG_OPPONENT ? hl_loc : -1);
	draw_side(TCG_PLAYER, false, hl_duelist == TCG_PLAYER ? hl_loc : -1);
	if (g_thinking && hl_loc < 0) { /* TCG_EV_THINKING: the AI is deciding */
		char buf[40];
		snprintf(buf, sizeof(buf), "%s is thinking%s", g_name[side_of(g_thinking)], (g_frame / 16) & 1 ? "..." : "");
		fill(LEFT - 2, SLOT_INFO_Y - 1, (int)strlen(buf) * CW + 4, 10, C_BLACK);
		print(LEFT, SLOT_INFO_Y, C_HI, buf);
	}
}

/* The display surfaces are uncached memory, where the CPU drawing functions
 * would do one slow uncached store per pixel: draw through the cached alias of
 * the buffer instead and write the data cache back before showing the frame. */
static surface_t *g_disp;
static surface_t g_fb_cached;
#ifdef TCGN64_ADVENTURE
static surface_t *g_last_ui_frame;
static surface_t g_ui_canvas;
static bool g_automated_ui;
static unsigned g_automated_events;
static unsigned g_message_speed=2;
#if ADVENTURE_DIAGNOSTIC == 3
static const char *g_review_label;
static unsigned g_review_seen;
#endif
#endif

static void begin_frame(void)
{
	g_disp = display_get();
	g_raster_width=g_disp->width;
	/* The adventure presents a completed CPU canvas through RDP, keeping
	 * partial CPU drawing away from the displayed framebuffer chain. */
#ifdef TCGN64_ADVENTURE
	g_fb_cached = g_ui_canvas;
#else
	g_fb_cached = *g_disp;
#endif
	g_fb_cached.buffer = CachedAddr(g_fb_cached.buffer);
	g_fb = &g_fb_cached;
	graphics_fill_screen(g_fb, g_col[C_BG]);
}

static void end_frame(void)
{
	if (g_denied) {
		g_denied--;
		rect(0, 0, SCREEN_W, SCREEN_H, C_RED);
		rect(1, 1, SCREEN_W - 2, SCREEN_H - 2, C_RED);
		rect(2, 2, SCREEN_W - 4, SCREEN_H - 4, C_RED);
	}
	int pending = g_save_ok ? save_store_pending() : 0;
	if (pending) /* in the bottom margin, below the text box */
		print(RIGHT - 7 * CW, SCREEN_H - 8, (g_frame / 8) & 1 ? C_HI : C_DIM, "saving.");
	data_cache_hit_writeback_invalidate(g_fb->buffer, (unsigned long)g_fb->stride * g_fb->height);
#ifdef TCGN64_ADVENTURE
	rdpq_attach(g_disp,NULL);
	rdpq_mode_push();rdpq_set_mode_copy(false);
	rdpq_tex_blit(g_fb,0,0,NULL);
	rdpq_mode_pop();rdpq_detach_wait();
	/* Finish reading the canvas before the CPU reuses it for another frame. */
#endif
	display_show(g_disp);
#ifdef TCGN64_ADVENTURE
	g_last_ui_frame=g_disp;
#endif
	g_fb = NULL;
	g_disp = NULL;
	if (pending) {
		/* one EEPROM block per frame: its ~15 ms write overlaps the next frame */
		save_store_poll();
		if (pending == 1)
			debugf("SAVED (%lu EEPROM block writes since power-on)\n", (unsigned long)save_store_blocks_written());
	}
	g_frame++;
#ifdef TCGN64_ADVENTURE
	adventure_audio_poll();
#endif
	if (g_dmg.timer)
		g_dmg.timer--;
}

/* ------------------------------------------------------------------ input */

typedef struct Keys { bool a, b, z, start, l, r, up, down, left, right; } Keys;

static Keys g_keys[2];
static uint8_t g_rep[2][4];
static volatile DuelInputCapture g_input_capture;
static bool g_input_service;
#if ADVENTURE_DIAGNOSTIC
static volatile unsigned g_input_vi_samples;
#endif

static void input_sample(void)
{
#if ADVENTURE_DIAGNOSTIC
	if(g_input_service)g_input_vi_samples++;
#endif
	/* libdragon already reads Joybus asynchronously once per VI. Snapshot that
	 * completed result at VI rate instead of losing taps between UI redraws. */
	joypad_poll();
	for(unsigned p=0;p<2;p++) {
		joypad_port_t port=p?JOYPAD_PORT_2:JOYPAD_PORT_1;
		bool connected=joypad_is_connected(port);
		joypad_inputs_t in=joypad_get_inputs(port);joypad_buttons_t b=in.btn;
		uint16_t held=(b.a?INPUT_A:0)|(b.b?INPUT_B:0)|(b.z?INPUT_Z:0)|(b.start?INPUT_START:0)|
			(b.l||b.c_left?INPUT_L:0)|(b.r||b.c_right?INPUT_R:0)|
			(b.d_up||b.c_up||in.stick_y>40?INPUT_UP:0)|(b.d_down||b.c_down||in.stick_y< -40?INPUT_DOWN:0)|
			(b.d_left||in.stick_x< -40?INPUT_LEFT:0)|(b.d_right||in.stick_x>40?INPUT_RIGHT:0);
		duel_input_sample(&g_input_capture,p,held,connected);
	}
}

static void input_reset(void)
{
#ifndef TCGN64_HOST_TEST
	disable_interrupts();
#endif
	g_input_capture=(DuelInputCapture){0};memset(g_keys,0,sizeof(g_keys));memset(g_rep,0,sizeof(g_rep));
#ifndef TCGN64_HOST_TEST
	enable_interrupts();
#endif
}
static void input_discard_pending(void)
{
#ifndef TCGN64_HOST_TEST
	disable_interrupts();
#endif
	g_input_capture.pending[0]=g_input_capture.pending[1]=0;
	memset(g_keys,0,sizeof(g_keys));memset(g_rep,0,sizeof(g_rep));
#ifndef TCGN64_HOST_TEST
	enable_interrupts();
#endif
}
static void input_begin(void)
{
	if(g_input_service)return;
#ifndef TCGN64_HOST_TEST
	/* This SDK mutates the VI handler list without its own critical section. */
	disable_interrupts();
#endif
	input_reset();input_sample();
#if ADVENTURE_DIAGNOSTIC
	g_input_vi_samples=0;
#endif
	/* A button already held while entering the duel is not a new action. */
	g_input_capture.pending[0]=g_input_capture.pending[1]=0;
#ifndef TCGN64_HOST_TEST
	register_VI_handler(input_sample);
#endif
	g_input_service=true;
#ifndef TCGN64_HOST_TEST
	enable_interrupts();
#endif
}
#ifdef TCGN64_ADVENTURE
static void input_end(void)
{
	if(!g_input_service)return;
#ifndef TCGN64_HOST_TEST
	disable_interrupts();
	unregister_VI_handler(input_sample);
#endif
	g_input_service=false;input_reset();
#ifndef TCGN64_HOST_TEST
	enable_interrupts();
#endif
#if ADVENTURE_DIAGNOSTIC
	debugf("DUEL INPUT VI samples=%u\n",g_input_vi_samples);
#endif
}
#endif

static void input_poll(void)
{
#ifdef TCGN64_HOST_TEST
	input_sample();
#else
	if(!g_input_service)input_sample();
	disable_interrupts();
#endif
	DuelInputCapture sampled=g_input_capture;
	for(unsigned p=0;p<2;p++)sampled.pending[p]=duel_input_take(&g_input_capture,p);
#ifndef TCGN64_HOST_TEST
	enable_interrupts();
#endif
	for (int p = 0; p < 2; p++) {
		if (!sampled.connected[p]) {
			g_keys[p]=(Keys){0};memset(g_rep[p],0,sizeof(g_rep[p]));
			continue;
		}
		Keys k=g_keys[p];uint16_t edge=sampled.pending[p],down=sampled.held[p];
		k.a|=(edge&INPUT_A)!=0;k.b|=(edge&INPUT_B)!=0;k.z|=(edge&INPUT_Z)!=0;
		k.start|=(edge&INPUT_START)!=0;k.l|=(edge&INPUT_L)!=0;k.r|=(edge&INPUT_R)!=0;
		bool held[4] = {
			(down&INPUT_UP)!=0,(down&INPUT_DOWN)!=0,(down&INPUT_LEFT)!=0,(down&INPUT_RIGHT)!=0,
		};
		bool fire[4];
		for (int d = 0; d < 4; d++) {
			if (!held[d]) {
				g_rep[p][d] = 0;
				fire[d] = false;
				continue;
			}
			int t = g_rep[p][d];
			if (t < 255)
				g_rep[p][d]++;
			fire[d] = t == 0 || (t >= 18 && (t - 18) % 4 == 0); /* auto-repeat */
		}
		k.up|=fire[0]||(edge&INPUT_UP);k.down|=fire[1]||(edge&INPUT_DOWN);
		k.left|=fire[2]||(edge&INPUT_LEFT);k.right|=fire[3]||(edge&INPUT_RIGHT);
		g_keys[p] = k;
	}
}

/* Consume one input snapshot. Never reuse a press for the following request. */
static Keys keys(int mask)
{
	Keys k = { 0 };
#ifndef TCGN64_HOST_TEST
	disable_interrupts();
#endif
	/* controller 2 unplugged (or never there): controller 1 takes over */
	if (mask == 2 && !joypad_is_connected(JOYPAD_PORT_2))
		mask = 1;
	for (int p = 0; p < 2; p++) {
		uint16_t edge=duel_input_take(&g_input_capture,(unsigned)p);
		if (!(mask & (1 << p)))
			continue;
		const Keys *q = &g_keys[p];
		k.a |= q->a; k.b |= q->b; k.z |= q->z; k.start |= q->start; k.l |= q->l; k.r |= q->r;
		k.up |= q->up; k.down |= q->down; k.left |= q->left; k.right |= q->right;
		/* Include edges sampled after the render poll, and discard the other
		 * port's edges in the same transaction so they cannot leak turns. */
		k.a|=(edge&INPUT_A)!=0;k.b|=(edge&INPUT_B)!=0;k.z|=(edge&INPUT_Z)!=0;
		k.start|=(edge&INPUT_START)!=0;k.l|=(edge&INPUT_L)!=0;k.r|=(edge&INPUT_R)!=0;
		k.up|=(edge&INPUT_UP)!=0;k.down|=(edge&INPUT_DOWN)!=0;
		k.left|=(edge&INPUT_LEFT)!=0;k.right|=(edge&INPUT_RIGHT)!=0;
		for(unsigned d=0;d<4;d++)if((edge&(INPUT_UP<<d))&&!g_rep[p][d])g_rep[p][d]=1;
	}
	memset(g_keys,0,sizeof(g_keys));
#ifndef TCGN64_HOST_TEST
	enable_interrupts();
#endif
	return k;
}

/* the controller(s) that answer for a duelist */
static int ports_for(uint8_t duelist)
{
	if (g_mode == MODE_HOTSEAT && duelist == TCG_OPPONENT && joypad_is_connected(JOYPAD_PORT_2))
		return 2;
	return 1;
}

static void end_frame_poll(void)
{
	end_frame();
	input_poll();
}

#if ADVENTURE_DIAGNOSTIC == 3
/* Capture the first frame of a real, engine-requested UI screen. The review
 * ROM alone acknowledges it afterwards; production input is untouched. */
static bool review_snapshot(void)
{
	if(!g_review_label)return false;
	surface_t *frame=g_last_ui_frame;
	const uint16_t *pixels=UncachedAddr(frame->buffer);
	debugf("FRAMEBUFFER %s %u %u\n",g_review_label,frame->width,frame->height);
	for(unsigned y=0;y<frame->height;y++)for(unsigned x=0;x<frame->width;x+=32) {
		char line[129];
		unsigned count=frame->width-x;if(count>32)count=32;
		for(unsigned i=0;i<count;i++)snprintf(line+i*4,5,"%04x",pixels[y*(frame->stride/2)+x+i]);
		debugf("PIXELS %u %u %s\n",x,y,line);
	}
	debugf("FRAMEBUFFER END\n");g_review_label=NULL;
	return true;
}
#endif

/* render n frames of the board (pacing for events); A/B skips */
static void draw_match_plaque(bool result);

enum { COIN_SPIN_FRAMES=24, COIN_TOSS_FRAMES=30 };

static void draw_coin_art(int cx,int cy,int width,int height,bool heads)
{
	int left=ui_x(cx-width/2),top=ui_y(cy-height/2);
	int pixels=ui_x(cx-width/2+width)-left,rows=ui_y(cy-height/2+height)-top;
	assert(pixels>0&&pixels<=72&&rows>0);
	assert(left>=0&&top>=0&&left+pixels<=g_fb->width&&top+rows<=g_fb->height);
	uint8_t columns[72];
	for(int x=0;x<pixels;x++)columns[x]=(unsigned)x*COIN_ART_W/(unsigned)pixels;
	for(int y=0;y<rows;y++) {
		const uint16_t *source=coin_art[heads]+((unsigned)y*COIN_ART_H/(unsigned)rows)*COIN_ART_W;
		uint16_t *destination=(uint16_t *)((uint8_t *)g_fb->buffer+(top+y)*g_fb->stride)+left;
		for(int x=0;x<pixels;x++)if(source[columns[x]]&1)destination[x]=source[columns[x]];
	}
}

static void draw_coin_panel(int animation_frame)
{
	bool moving=animation_frame>=0&&animation_frame<COIN_SPIN_FRAMES;
	bool revealed=animation_frame<0||animation_frame>=COIN_TOSS_FRAMES-2;
	bool heads=g_coin.ready||!g_coin.n?true:g_coin.res[g_coin.n-1]!=0;
	int width=48,y=111;
	if(moving) {
		static const uint8_t widths[]={48,36,6,36,48,36,6,36};
		unsigned phase=(unsigned)animation_frame%8;
		width=widths[phase];heads=phase<4;
		y-=animation_frame*(COIN_SPIN_FRAMES-animation_frame)*28/144;
	} else if(animation_frame>=COIN_SPIN_FRAMES&&animation_frame<COIN_TOSS_FRAMES) {
		static const int8_t bounce[]={4,2,0,-1,0,0};
		y+=bounce[animation_frame-COIN_SPIN_FRAMES];
	}
	paper_panel(62,26,196,156);
	char heading[32];
	unsigned number=(unsigned)(g_coin.number+(g_coin.ready?1:0));
	if(g_coin.total>1)snprintf(heading,sizeof(heading),"Coin %u of %d",number?number:1,g_coin.total);
	else copy_str(heading,sizeof(heading),"Coin toss");
	body(160-body_width(heading)/2,36,C_INK,heading,184);
	fill(145,137,30,2,C_PAPERSHADE);fill(150,139,20,1,C_PAPERSHADE);
	draw_coin_art(160,y,width,48,heads);
	if(g_coin.ready) {
		if(g_kind[side_of(g_coin.owner)]==PK_HUMAN)body(138,145,C_INK,"A: toss",80);
	} else if(g_coin.n&&revealed) {
		const char *label=g_coin.res[g_coin.n-1]?"Heads":"Tails";
		body(160-body_width(label)/2,145,C_INK,label,100);
	}
	/* The current result joins the history only when it is revealed. */
	int available=g_coin.n-((animation_frame>=0&&!revealed&&g_coin.n)?1:0);
	int shown=available>12?12:available,start=available-shown;
	for(int i=0;i<shown;i++) {
		int x=160-shown*11/2+i*11;
		bool result=g_coin.res[start+i]!=0;
		fill(x,166,9,9,result?C_HI:C_PANELLIGHT);rect(x,166,9,9,C_EDGE);
		print(x+1,167,C_INK,result?"H":"T");
	}
}

static void coin_record(const TcgEvent *ev)
{
	if(g_coin.n==(int)sizeof(g_coin.res)) {
		memmove(g_coin.res,g_coin.res+1,sizeof(g_coin.res)-1);g_coin.n--;
	}
	g_coin.res[g_coin.n++]=ev->arg[0]==HEADS;
	g_coin.number=ev->arg[1];g_coin.total=ev->arg[2];g_coin.owner=ev->arg[3];g_coin.ready=false;
}

static bool coin_fast_forward(int ports)
{
	Keys pressed=keys(ports);
	if(pressed.a||pressed.b)return true;
	if(!wSkipDelayAllowed)return false;
	if(ports==2&&!joypad_is_connected(JOYPAD_PORT_2))ports=1;
	for(int p=0;p<2;p++)if((ports&(1<<p))&&joypad_get_buttons_held((joypad_port_t)p).b)return true;
	return false;
}

static void animate_coin(void)
{
	if(wAnimationsDisabled)return;
#ifdef TCGN64_ADVENTURE
	if(g_automated_ui)return;
	adventure_audio_event(1,SFX_COIN_TOSS);
#endif
	int ports=ports_for(g_coin.owner);
	for(int frame=0;frame<COIN_TOSS_FRAMES;frame++) {
		begin_frame();draw_board(0,-1);draw_textbox();draw_coin_panel(frame);end_frame_poll();
		if(coin_fast_forward(ports))break;
	}
}

static void show_frames(int n)
{
#ifdef TCGN64_ADVENTURE
	if(g_automated_ui)n=((++g_automated_events&7)==0)?1:0;
#endif
	for (int i = 0; i < n; i++) {
		begin_frame();
		if(practice_reading_visible()) {
			draw_practice_reading(0);draw_practice_continue(0);
		} else {
			if(practice_checklist_visible())draw_practice_checklist();else draw_board(0,-1);
			draw_textbox();
		}
		if(g_result>=0)draw_match_plaque(true);
		else if(!g_turn_holder&&g_screen==TCG_SCREEN_EMPTY)draw_match_plaque(false);
		if(g_screen==TCG_SCREEN_COIN_TOSS)draw_coin_panel(-1);
		end_frame_poll();
		/* A one-frame event is a redraw, not a separate input prompt. Preserve
		 * its press for the immediately following acknowledgement or menu. */
		if(n==1)continue;
		Keys k = keys(3);
		if (k.a || k.b)
			break;
	}
}

/* ------------------------------------------------------------------ text log */

static void log_message(const char *utf8)
{
	char clean[640];
	char lines[24][COLS + 1];
	sanitize(utf8, clean, sizeof(clean));
	int n = wrap(clean, COLS, lines, 24);
	if (n == 0)
		return;
	if (n > LOG_LINES)
		n = LOG_LINES;
	if (g_log_n + n > LOG_LINES) {
		int drop = g_log_n + n - LOG_LINES;
		memmove(g_log[0], g_log[drop], (size_t)(g_log_n - drop) * sizeof(g_log[0]));
		g_log_n -= drop;
	}
	g_log_newest = g_log_n;
	for (int i = 0; i < n; i++)
		memcpy(g_log[g_log_n++], lines[i], sizeof(g_log[0]));
	for (char *p = clean; *p; p++)
		if (*p == '\n')
			*p = '/';
	debugf("TEXT %s\n", clean);
}

static void log_clear(void)
{
	g_log_n = 0;
	g_log_newest = 0;
	g_practice_checklist[0]=0;g_practice_checklist_on=false;
	g_practice_reading_on=false;
}

/* ------------------------------------------------------------------ card page */

/* Inspection preserves both printed attack-description pages. Long rules
 * have ordinary pages rather than disappearing below the board viewport. */
#define RULE_LINES 96
#define RULES_PER_PAGE 7
static struct RuleLine { char text[RULE_COLS]; const struct AtkData *attack; int color; } g_rules[RULE_LINES];
static int g_rule_count;

static void rule_text(const char *text,int color)
{
	char lines[48][RULE_COLS];
	int count=body_wrap(text,RIGHT-LEFT-8,lines,48);
	for(int i=0;i<count&&g_rule_count<RULE_LINES;i++) {
		struct RuleLine *line=&g_rules[g_rule_count++];
		copy_str(line->text,sizeof(line->text),lines[i]);line->attack=NULL;line->color=color;
	}
}

static int card_rules(const struct CardData *card)
{
	g_rule_count=0;
	if(is_pokemon(card)) {
		const struct AtkData *attacks[2]={&card->Atk1,&card->Atk2};
		for(int i=0;i<2;i++) {
			const struct AtkData *attack=attacks[i];
			if(!rd16(attack->Name))continue;
			struct RuleLine *line=&g_rules[g_rule_count++];
			text_of(rd16(attack->Name),line->text,sizeof(line->text));
			line->attack=attack;line->color=C_INK;
			char desc[1400];attack_description(attack,desc,sizeof(desc));
			rule_text(desc,C_INK);
			if(i==0&&rd16(card->Atk2.Name)&&g_rule_count<RULE_LINES)rule_text(" ",C_INK);
		}
	} else {
		rule_text("Card effect",C_INK);
		char desc[1400];description(rd16(card->NonPokemonDescription),desc,sizeof(desc));
		rule_text(desc,C_INK);
	}
	return (g_rule_count+RULES_PER_PAGE-1)/RULES_PER_PAGE;
}

static int draw_card_page_at(uint8_t id,uint8_t owner,int deck,const char *title,int page)
{
	const struct CardData *c=card_data(id);
	char name[64],buf[128];
	int pages=card_rules(c);if(pages<1)pages=1;
	if(page>=pages)page=pages-1;
	if(page<0)page=0;
	paper_panel(LEFT-6,BOARD_Y,RIGHT-LEFT+12,BOARD_H);
	int x=LEFT+2,y=BOARD_Y+5;
	card_name(id,name,sizeof(name));
	body(x,y,C_INK,name,RIGHT-LEFT-76);
	card_art(RIGHT-66,y+1,id);
	if(is_pokemon(c)) {
		int hp=c->HP;
		if(deck>=0&&deck<DECK_SIZE)for(int loc=0;loc<num_in_play(owner);loc++) {
			Slot slot;if(get_slot(owner,loc,&slot)&&slot.deck==deck)hp=slot.hp;
		}
		y+=BODY_LH;
		energy_icon(x,y+2,card_color(c));
		bodyf(x+13,y,C_INK,190,"%s  HP %d/%d  Lv%d",kTypeName[card_color(c)],hp,c->HP,c->Level);
		y+=BODY_LH;
		static const char *const stages[]={"Basic","Stage 1","Stage 2","Stage 2"};
		if(c->Stage!=BASIC) {
			char pre[40];text_of(rd16(c->PreEvoName),pre,sizeof(pre));
			snprintf(buf,sizeof(buf),"%s from %s",stages[c->Stage&3],pre);
		} else copy_str(buf,sizeof(buf),"Basic Pokemon");
		body(x,y,C_INK,buf,206);
		y+=BODY_LH+2;
		int wx=x;body(wx,y,C_INK,"Weak",32);wx+=34;
		bool any=false;
		for(int k=0;k<6;k++)if(c->Weakness&(0x80>>k)){energy_icon(wx,y+2,k);wx+=9;any=true;}
		if(!any){body(wx,y,C_INK,"--",12);wx+=14;}
		else {body(wx,y,C_INK,"x2",15);wx+=17;}
		wx+=7;body(wx,y,C_INK,"Resist",38);wx+=39;any=false;
		for(int k=0;k<6;k++)if(c->Resistance&(0x80>>k)){energy_icon(wx,y+2,k);wx+=9;any=true;}
		if(!any){body(wx,y,C_INK,"--",12);wx+=14;}
		else {body(wx,y,C_INK,"-30",20);wx+=22;}
		wx+=7;
		if(c->RetreatCost==UNABLE_RETREAT)body(wx,y,C_INK,"Retreat: unable",112);
		else bodyf(wx,y,C_INK,112,"Retreat %d",c->RetreatCost);
	} else {
		body(x,y+BODY_LH,C_INK,is_energy(c)?(c->Type==TYPE_ENERGY_DOUBLE_COLORLESS?"Special Energy":"Basic Energy"):"Trainer",200);
		if(is_energy(c))energy_icon(x,y+2*BODY_LH+2,card_color(c));
	}
	fill(x,BOARD_Y+61,RIGHT-LEFT-4,1,C_PAPERSHADE);
	for(int row=0;row<RULES_PER_PAGE&&page*RULES_PER_PAGE+row<g_rule_count;row++) {
		struct RuleLine *line=&g_rules[page*RULES_PER_PAGE+row];
		int ly=BOARD_Y+66+row*BODY_LH;
		if(line->attack) {
			fill(x-1,ly-1,RIGHT-LEFT-2,BODY_LH,C_PAPERSHADE);
			int cost=line->attack->Category==POKEMON_POWER?0:cost_icons(x+1,ly+2,line->attack);
			if(line->attack->Category==POKEMON_POWER)body(x+2,ly,C_INK,"Power:",40);
			int namex=x+cost+(line->attack->Category==POKEMON_POWER?42:3);
			body(namex,ly,C_INK,line->text,RIGHT-namex-30);
			if(line->attack->Damage) {
				static const char suffix[]={0,'+','-','x'};
				snprintf(buf,sizeof(buf),"%d%c",line->attack->Damage,suffix[line->attack->Category&3]);
				body(RIGHT-2-body_width(buf),ly,C_INK,buf,32);
			}
		} else body(x+2,ly,line->color,line->text,RIGHT-x-4);
	}
	if(title)body(x,BOARD_Y+BOARD_H-14,C_INK,title,190);
	if(pages>1)bodyf(RIGHT-96,BOARD_Y+BOARD_H-14,C_INK,94,"L/R  %d / %d",page+1,pages);
	return pages;
}

static void draw_card_page(uint8_t id,uint8_t owner,int deck,const char *title)
{ (void)draw_card_page_at(id,owner,deck,title,0); }

/* view a card until A/B/START; page input never changes the caller's cursor. */
static void view_card(uint8_t id,uint8_t owner,int deck,int ports)
{
	int page=0;
	for(;;) {
		begin_frame();
		int pages=draw_card_page_at(id,owner,deck,NULL,page);
		draw_textbox_hint(pages>1?"L/R: page  B: back":"B: back");
		end_frame_poll();Keys k=keys(ports);
		if(k.a||k.b||k.start)return;
		if((k.r||k.right||k.down)&&page+1<pages)page++;
		if((k.l||k.left||k.up)&&page>0)page--;
	}
}

static void view_slot(uint8_t duelist, int loc, int ports)
{
	Slot s;
	if (get_slot(duelist, loc, &s))
		view_card(s.id, duelist, s.deck, ports);
}

/* ------------------------------------------------------------------ lists */

/* a scrolling list panel; label(i) fills a line, returns the color */
typedef int (*LabelFn)(void *ctx, int i, char *out, int n, int *icon);

static void draw_list(int x,int y,int w,int rows,const char *title,int n,int cur,int *top,
                      LabelFn label,void *ctx)
{
	panel(x,y,w,rows*LIST_LH+24);
	if(title)body(x+7,y+4,C_HI,title,w-14);
	if(cur<*top)*top=cur;
	if(cur>=*top+rows)*top=cur-rows+1;
	if(*top<0)*top=0;
	int ly=y+21;
	if(!n)body(x+15,ly,C_DIM,"(none)",w-22);
	for(int i=*top;i<n&&i<*top+rows;i++,ly+=LIST_LH) {
		char text[64];int icon=-1;
		int color=label(ctx,i,text,sizeof(text),&icon);
		if(i==cur)focus_row(x+3,ly-1,w-6,LIST_LH);
		int tx=x+15;
		if(icon>=0){energy_icon(tx,ly+2,icon);tx+=11;}
		body(tx,ly,i==cur?C_INK:color,text,x+w-8-tx);
	}
	if(*top>0)print(x+w-12,y+4,C_HI,"^");
	if(*top+rows<n)print(x+w-12,y+rows*LIST_LH+13,C_HI,"v");
}

/* up/down/L/R navigation; returns true if the cursor moved */
static bool list_nav(Keys k, int n, int *cur)
{
	int old = *cur;
	if (n <= 0)
		return false;
	if (k.up)
		*cur = *cur > 0 ? *cur - 1 : n - 1;
	if (k.down)
		*cur = *cur < n - 1 ? *cur + 1 : 0;
	if (k.l || k.left)
		*cur = *cur >= 8 ? *cur - 8 : 0;
	if (k.r || k.right)
		*cur = *cur + 8 < n ? *cur + 8 : n - 1;
	return *cur != old;
}

typedef struct CardListCtx { const uint8_t *items; uint8_t owner; int stride; } CardListCtx;

static int label_card(void *vctx, int i, char *out, int n, int *icon)
{
	CardListCtx *ctx = vctx;
	uint8_t deck = ctx->items[i * ctx->stride];
	if (deck >= DECK_SIZE) {
		snprintf(out, (size_t)n, "?");
		return C_DIM;
	}
	uint8_t id = tcg_card_id(ctx->owner, deck);
	const struct CardData *c = card_data(id);
	char name[32];
	card_name(id, name, sizeof(name));
	*icon = card_color(c);
	if (is_pokemon(c))
		copy_str(out, (size_t)n, name);
	else if (is_energy(c))
		copy_str(out, (size_t)n, name);
	else {
		*icon = -1;
		snprintf(out, (size_t)n, "T %s", name);
	}
	return C_TEXT;
}

/* short summary of a card in a side panel (list details) */
static void draw_card_summary(int x,int y,int w,uint8_t id)
{
	const struct CardData *c=card_data(id);
	char name[64],buf[128],lines[16][RULE_COLS];
	card_name(id,name,sizeof(name));
	int count=body_wrap(name,w-8,lines,3);
	for(int i=0;i<count&&i<2;i++)body(x+4,y+i*BODY_LH,C_INK,lines[i],w-8);
	/* Name, stats and illustration occupy stable slots for all card classes. */
	if(is_pokemon(c)) {
		energy_icon(x+4,y+29,card_color(c));
		bodyf(x+17,y+27,C_INK,w-21,"HP %d  Lv%d",c->HP,c->Level);
	} else body(x+4,y+27,C_INK,is_energy(c)?"Energy":"Trainer",w-8);
	card_art(x+(w-64)/2,y+43,id);
	int ly=y+97;
	if(is_pokemon(c)) {
		const struct AtkData *attacks[2]={&c->Atk1,&c->Atk2};
		/* A rule-bearing attack/power earns the space below its name. Plain
		 * damage attacks can both fit. Full inspection always retains both. */
		int featured=rd16(c->Atk1.Name)?0:1;
		if(featured==0&&!rd16(c->Atk1.Description)&&rd16(c->Atk2.Description))featured=1;
		char desc[1400];attack_description(attacks[featured],desc,sizeof(desc));
		if(desc[0]) {
			const struct AtkData *attack=attacks[featured];
			if(attack->Category==POKEMON_POWER)body(x+4,ly,C_INK,"Pokemon Power",w-8);
			else {
				cost_icons(x+4,ly+2,attack);
				if(attack->Damage)bodyf(x+w-30,ly,C_INK,26,"%d",attack->Damage);
			}
			text_of(rd16(attack->Name),name,sizeof(name));
			body(x+4,ly+BODY_LH,C_INK,name,w-8);
			count=body_wrap(desc,w-8,lines,16);
			if(count>3)preview_ellipsis(lines[2],w-8);
			for(int i=0;i<count&&i<3;i++)body(x+4,ly+2*BODY_LH+i*BODY_LH,C_INK,lines[i],w-8);
			return;
		}
		for(int i=0;i<2;i++) {
			const struct AtkData *attack=attacks[i];
			if(!rd16(attack->Name))continue;
			if(attack->Category==POKEMON_POWER)body(x+4,ly,C_INK,"Pokemon Power",w-8);
			else {
				cost_icons(x+4,ly+2,attack);
				if(attack->Damage)bodyf(x+w-30,ly,C_INK,26,"%d",attack->Damage);
			}
			text_of(rd16(attack->Name),name,sizeof(name));
			body(x+4,ly+BODY_LH,C_INK,name,w-8);
			ly+=2*BODY_LH+3;
		}
	} else {
		description(rd16(c->NonPokemonDescription),buf,sizeof(buf));
		count=body_wrap(buf,w-8,lines,16);
		int visible=(BOARD_Y+BOARD_H-5-ly-BODY_FONT_HEIGHT)/BODY_LH+1;
		if(count>visible&&visible>0)preview_ellipsis(lines[visible-1],w-8);
		for(int i=0;i<count&&ly+BODY_FONT_HEIGHT<BOARD_Y+BOARD_H-4;i++,ly+=BODY_LH)
			body(x+4,ly,C_INK,lines[i],w-8);
	}
}

/* A fixed preview location makes unlike card types easy to compare. Nine
 * larger rows replace thirteen compressed rows; L/R still page the cursor. */
static void draw_card_list_screen(const char *title,const char *info,const uint8_t *items,int n,
                                  uint8_t owner,int cur,int *top)
{
	CardListCtx ctx={items,owner,1};
	fill(LEFT-6,BOARD_Y,RIGHT-LEFT+12,BOARD_H,C_BG);
	draw_list(LEFT-6,BOARD_Y,164,9,title?title:"Cards",n,cur,top,label_card,&ctx);
	paper_panel(LEFT+161,BOARD_Y,RIGHT-LEFT-155,BOARD_H);
	if(n>0&&cur<n&&items[cur]<DECK_SIZE)
		draw_card_summary(LEFT+163,BOARD_Y+5,RIGHT-LEFT-159,tcg_card_id(owner,items[cur]));
	if(info&&info[0]) {
		char lines[8][RULE_COLS];int count=body_wrap(info,150,lines,8);
		for(int i=0;i<count&&i<2;i++)body(LEFT-1,BOARD_Y+153+i*BODY_LH,C_DIM,lines[i],150);
	} else bodyf(LEFT-1,BOARD_Y+154,C_DIM,150,"%d / %d",n?cur+1:0,n);
}

/* ------------------------------------------------------------------ requests */

static int banner_text_width(const char *text)
{
	int width=0;
	for(;*text;text++) {
		unsigned ch=(unsigned char)*text;
		if(ch<32||ch>126)ch='?';
		width+=banner_font_advance[ch-BANNER_FONT_FIRST];
	}
	return width;
}

static void banner_run(int x,int y,uint32_t color,const char *text,int limit)
{
	for(;*text&&*text!='\n';text++) {
		unsigned ch=(unsigned char)*text;if(ch<32||ch>126)ch='?';
		unsigned glyph=ch-BANNER_FONT_FIRST;
		int advance=banner_font_advance[glyph];
		if(x+advance>limit)break;
		if(ui_high_res())raster_glyph(ui_x(x),ui_y(y),(uint16_t)color,banner_hi_coverage[glyph],BANNER_HI_WIDTH,BANNER_HI_HEIGHT,ui_x(limit));
		else raster_glyph(x,y,(uint16_t)color,banner_font_coverage[glyph],BANNER_FONT_WIDTH,BANNER_FONT_HEIGHT,limit);
		x+=advance;
	}
}

static void banner_text(int x,int y,int color,const char *text)
{ banner_run(x,y,g_col[color],text,SCREEN_W); }

static void draw_duel_banner(const char *text)
{
	int text_width=banner_text_width(text),w=text_width+28;
	if(w<156)w=156;
	if(w>SCREEN_W-24)w=SCREEN_W-24;
	int x=(SCREEN_W-w)/2,y=TBOX_Y-36,h=32;
	fill(x+3,y+3,w,h,C_PANELSHADE);
	fill(x,y,w,h,C_EDGE);
	fill(x+2,y+2,w-4,h-4,C_PAPER);
	fill(x+1,y+1,w-2,1,C_PAPERLIGHT);
	fill(x+1,y+1,1,h-2,C_PAPERLIGHT);
	fill(x+1,y+h-2,w-2,1,C_PAPERSHADE);
	fill(x+w-2,y+1,1,h-2,C_PAPERSHADE);
	rect(x+5,y+5,w-10,h-10,C_PAPERSHADE);
	int tx=x+(w-text_width)/2;
	if(text_width>w-20) {
		int width=body_width(text);tx=x+(w-width)/2;if(tx<x+8)tx=x+8;
		body(tx,y+10,C_INK,text,w-16);
	} else {
		banner_text(tx+1,y+6,C_PAPERSHADE,text);
		banner_text(tx,y+5,C_INK,text);
	}
}

static void card_back(int x,int y,int width,int height)
{
	fill(x+2,y+2,width,height,C_PANELSHADE);
	const uint16_t *source;int sw,sh;
	/* Both display modes receive a direct reduction of the published back.
	 * The small plaque keeps its own filtered LOD instead of losing the logo
	 * to a second nearest-neighbor reduction of the Prize image. */
	if(width<=24) {
		source=ui_high_res()?card_back_small_hi:card_back_small;
		sw=ui_high_res()?CARD_BACK_SMALL_HI_W:CARD_BACK_SMALL_W;
		sh=ui_high_res()?CARD_BACK_SMALL_HI_H:CARD_BACK_SMALL_H;
	} else {
		source=ui_high_res()?card_back_high:card_back_standard;
		sw=ui_high_res()?CARD_BACK_HIGH_W:CARD_BACK_STANDARD_W;
		sh=ui_high_res()?CARD_BACK_HIGH_H:CARD_BACK_STANDARD_H;
	}
	int px=ui_x(x),py=ui_y(y),w=ui_x(x+width)-px,h=ui_y(y+height)-py;
	for(int row=0;row<h;row++) {
		int dy=py+row;if(dy<0||dy>=g_fb->height)continue;
		uint16_t *dest=(uint16_t *)((uint8_t *)g_fb->buffer+dy*g_fb->stride);
		for(int col=0;col<w;col++) {
			int dx=px+col;if(dx<0||dx>=g_fb->width)continue;
			uint16_t color=source[(row*sh/h)*sw+col*sw/w];
			if(color&1)dest[dx]=color;
		}
	}
}

/* Start and result share the paper turn-plaque language. They occupy waits
 * already requested by the engine; there is no additional timer or input gate. */
static void draw_match_plaque(bool result)
{
	char text[64];
	if(result) {
		if(g_result==2)copy_str(text,sizeof(text),"Draw");
		else snprintf(text,sizeof(text),"%s wins!",g_name[g_result]);
	} else snprintf(text,sizeof(text),"%s vs %s",g_name[0],g_name[1]);
	int x=24,y=76,width=272,height=58;
	fill(x+3,y+3,width,height,C_PANELSHADE);paper_panel(x,y,width,height);
	card_back(x+10,y+11,24,34);
	int tw=banner_text_width(text);
	if(tw<=width-56) {
		int tx=x+42+(width-52-tw)/2;
		banner_text(tx+1,y+18,C_PAPERSHADE,text);banner_text(tx,y+17,C_INK,text);
	} else body(x+43,y+22,C_INK,text,width-52);
}

static const char *who(uint8_t duelist) { return g_name[side_of(duelist)]; }

static int ui_ack(const TcgRequest *r, int ports)
{
	char lines[22][COLS + 1];
	int msg_lines = g_log_n - g_log_newest;
	bool long_msg = msg_lines > TBOX_LINES;
	bool checklist=practice_checklist_visible();
	bool reading=practice_reading_visible();
	int speech_first=0;
	int nl = 0;
	if (long_msg) {
		nl = msg_lines > 18 ? 18 : msg_lines;
		for (int i = 0; i < nl; i++)
			memcpy(lines[i], g_log[g_log_newest + i], sizeof(lines[0]));
	}
	/* a non-interactive card list (e.g. "you received these cards") */
	uint8_t list[DECK_SIZE];
	int list_n = 0;
	if (g_screen == TCG_SCREEN_CARD_LIST && !g_overlay.on) {
		const uint8_t *t = &tcg_wram()[ADDR_wDuelTempList - WRAM_START];
		while (list_n < DECK_SIZE && t[list_n] < DECK_SIZE)
			list_n++;
		if (t[list_n] != 0xff || list_n != g_screen_arg)
			list_n = 0;
		memcpy(list, t, (size_t)list_n);
	}
	int top = 0, card_page = 0;
	debugf("ASK ack/%s %s\n", g_overlay.on ? "card" : long_msg ? "long" : list_n ? "list" :
	       g_screen == TCG_SCREEN_COIN_TOSS ? "coin" : g_screen == TCG_SCREEN_BOX_MESSAGE ? "box" : "text", who(r->duelist));
	for (;;) {
		begin_frame();
		if(reading) {
			draw_practice_reading(speech_first);
		} else if (checklist) {
			draw_practice_checklist();
		} else if (g_overlay.on) {
			draw_card_page_at(g_overlay.id, g_overlay.owner, g_overlay.deck, NULL, card_page);
		} else if (long_msg) {
			panel(LEFT - 6, BOARD_Y, RIGHT - LEFT + 12, BOARD_H);
			for (int i = 0; i < nl; i++)
				print(LEFT, BOARD_Y + 4 + i * LH, C_TEXT, lines[i]);
			/* the practice duel labels Dr. Mason's explanations (wTextBoxLabel) */
			const uint8_t *label = &tcg_wram()[ADDR_wTextBoxLabel - WRAM_START];
			if (g_mode == MODE_PRACTICE && rd16(label)) {
				char lb[24];
				text_of(rd16(label), lb, sizeof(lb));
				int w = (int)strlen(lb) * CW + 8;
				fill(RIGHT + 2 - w, BOARD_Y + BOARD_H - 14, w, 10, C_PANEL2);
				print(RIGHT + 6 - w, BOARD_Y + BOARD_H - 13, C_HI, lb);
			}
		} else if (list_n) {
			int cur = hCurMenuItem < list_n ? hCurMenuItem : 0; /* the card the engine is talking about */
			draw_card_list_screen(NULL, NULL, list, list_n, r->shown, cur, &top);
		} else {
			draw_board(0, -1);
			if(g_result>=0)draw_match_plaque(true);
			else if(!g_turn_holder&&g_screen==TCG_SCREEN_EMPTY)draw_match_plaque(false);
			else if (g_screen == TCG_SCREEN_COIN_TOSS) {
				draw_coin_panel(-1);
			} else if (g_screen == TCG_SCREEN_BOX_MESSAGE && g_boxmsg <= BOXMSG_COIN_TOSS) {
				static const char *const box[] = { "%s's turn", "%s's turn", "Between turns", "Decision",
					"Bench Pokemon", "Arena Pokemon", "Coin toss" };
				char b[40];
				uint8_t d = g_boxmsg == BOXMSG_OPPONENTS_TURN ? TCG_OPPONENT : TCG_PLAYER;
				snprintf(b, sizeof(b), box[g_boxmsg], who(d));
				draw_duel_banner(b);
			}
		}
		if(reading)draw_practice_continue(speech_first);else draw_textbox();
		if (!reading&&(g_frame / 16) & 1)
			print(RIGHT-CW,checklist?BOARD_Y+BOARD_H-15:TBOX_LINE(TBOX_LINES-1),C_HI,"v");
		end_frame_poll();
#if ADVENTURE_DIAGNOSTIC == 3
		if(review_snapshot())return 0;
#endif
		Keys k = keys(ports);
		if(g_overlay.on) {
			int pages=(g_rule_count+RULES_PER_PAGE-1)/RULES_PER_PAGE;
			if((k.r||k.right||k.down)&&card_page+1<pages)card_page++;
			if((k.l||k.left||k.up)&&card_page>0)card_page--;
		}
		if (k.a || k.b) {
			if(reading&&speech_first+PRACTICE_PAGE_LINES<g_practice_reading_count){speech_first+=PRACTICE_PAGE_LINES;continue;}
			break;
		}
		if (k.start && !g_overlay.on && !long_msg&&!checklist&&!reading)
			view_slot(r->shown, 0, ports);
	}
	g_overlay.on = false;
	return 0;
}

static void check_screen(int ports);

static void draw_duel_choices(uint8_t duelist,int cur)
{
	static const char *const items[6]={"Hand","Attack","Check","PKMN Pwr","Retreat","Done"};
	draw_board(0, -1);
	panel(LEFT - 6, TBOX_Y, RIGHT - LEFT + 12, TBOX_LINES * TLH + 5);
	print(LEFT,TBOX_LINE(0),C_DIM,who(duelist));
	print(RIGHT-21*CW,TBOX_LINE(0),C_HI,"START: card  Z: check");
	for (int i = 0; i < 6; i++) {
		int x=LEFT+8+(i/2)*96,y=TBOX_Y+14+(i&1)*14;
		if(i==cur)focus_row(x-10,y-1,91,14);
		body(x+2,y,i==cur?C_INK:C_TEXT,items[i],78);
	}
}

static int ui_duel_menu(const TcgRequest *r, int ports)
{
	int cur = r->cursor < 6 ? r->cursor : 0;
	for (;;) {
		begin_frame();
		draw_duel_choices(r->duelist,cur);
		end_frame_poll();
#if ADVENTURE_DIAGNOSTIC == 3
		if(review_snapshot())return cur;
#endif
		Keys k = keys(ports);
		if (k.up || k.down)
			cur ^= 1;
		if (k.left)
			cur = (cur + 4) % 6;
		if (k.right)
			cur = (cur + 2) % 6;
		if (k.start)
			view_slot(r->shown, 0, ports);
		if (k.z)
			check_screen(ports);
		if (k.a) {
			if (cur == 2) /* the engine's check menu is a no-op shim: browse here */
				check_screen(ports);
			return cur;
		}
	}
}

static int ui_card_list(const TcgRequest *r, int ports)
{
	char title[64], info[96];
	uint8_t items[DECK_SIZE];
	int n = r->num_items <= DECK_SIZE ? r->num_items : DECK_SIZE;
	if (r->items)
		memcpy(items, r->items, (size_t)n);
	else
		n = 0;
	if (r->text)
		formatted_text(r->text, title, sizeof(title));
	else
		snprintf(title, sizeof(title), "Cards");
	info[0] = 0;
	if (r->text2)
		formatted_text(r->text2, info, sizeof(info));
	int cur = r->cursor < n ? r->cursor : 0, top = 0;
	const char *hints = r->arg[0] ? "A: view  B: back" : r->arg[1] ? "A: choose  B: back  START: info"
	                                                                : "A: select  B: back  START: info";
	for (;;) {
		begin_frame();
		draw_card_list_screen(title, info, items, n, r->shown, cur, &top);
		draw_textbox_hint(hints);
		end_frame_poll();
#if ADVENTURE_DIAGNOSTIC == 3
		if(review_snapshot())return cur;
#endif
		Keys k = keys(ports);
		list_nav(k, n, &cur);
		if (k.a && n > 0)
			return cur;
		if (k.b)
			return TCG_CANCEL;
		if (k.start && n > 0 && items[cur] < DECK_SIZE)
			view_card(tcg_card_id(r->shown, items[cur]), r->shown, items[cur], ports);
	}
}

/* two labels out of a text such as "  Play\n  Check" or "...:\n   Yours   Opponent's" */
static void two_labels(const char *text, char *a, char *b, int n, const char *da, const char *db)
{
	char clean[256];
	sanitize(text, clean, sizeof(clean));
	copy_str(a, (size_t)n, da);
	copy_str(b, (size_t)n, db);
	char words[2][32];
	int found = 0;
	/* last line: split on runs of 2+ spaces; otherwise the last two lines */
	char *last = strrchr(clean, '\n');
	char *line = last ? last + 1 : clean;
	char *p = line;
	while (*p && found < 2) {
		while (*p == ' ')
			p++;
		int len = 0;
		while (p[len] && !(p[len] == ' ' && (p[len + 1] == ' ' || !p[len + 1])))
			len++;
		if (len > 0) {
			snprintf(words[found], sizeof(words[0]), "%.*s", len, p);
			found++;
		}
		p += len;
	}
	if (found == 2) {
		copy_str(a, (size_t)n, words[0]);
		copy_str(b, (size_t)n, words[1]);
		return;
	}
	if (last && found == 1) {
		*last = 0;
		char *prev = strrchr(clean, '\n');
		prev = prev ? prev + 1 : clean;
		while (*prev == ' ')
			prev++;
		copy_str(a, (size_t)n, prev);
		copy_str(b, (size_t)n, words[0]);
	}
}

static void draw_choice_popup(int x,int y,const char *title,const char *const *labels,int n,int cur)
{
	int width=0;
	for(int i=0;i<n;i++)if(body_width(labels[i])>width)width=body_width(labels[i]);
	if(title&&body_width(title)>width)width=body_width(title);
	if(width>256)width=256;
	int pw=width+30,ph=(n+(title?1:0))*LIST_LH+10;
	if(x+pw>RIGHT+6)x=RIGHT+6-pw;
	if(y+ph!=TBOX_Y-2)y=TBOX_Y-2-ph;
	fill(x+2,y+2,pw,ph,C_PANELSHADE);panel(x,y,pw,ph);
	int ly=y+5;
	if(title){body(x+8,ly,C_DIM,title,width+12);ly+=LIST_LH;}
	for(int i=0;i<n;i++,ly+=LIST_LH) {
		if(i==cur)focus_row(x+3,ly-1,pw-6,LIST_LH);
		body(x+17,ly,i==cur?C_INK:C_TEXT,labels[i],pw-22);
	}
}

static int ui_item_menu(const TcgRequest *r, int ports)
{
	char a[32], b[32];
	two_labels(r->text ? tcg_text(r->text) : "", a, b, sizeof(a), "Play", "Check");
	const char *labels[2] = { a, b };
	uint8_t deck = r->arg[0] < DECK_SIZE ? r->arg[0] : 0;
	uint8_t id = tcg_card_id(r->shown, deck);
	int cur = 0;
	for (;;) {
		begin_frame();
		draw_card_page(id, r->shown, deck, NULL);
		chosen_corners(RIGHT-69,BOARD_Y+3,70,54);
		draw_choice_popup(RIGHT - 100, BOARD_Y + 130, NULL, labels, 2, cur);
		draw_textbox_hint("A: choose  B: back  START: card");
		end_frame_poll();
		Keys k = keys(ports);
		if(k.start)view_card(id,r->shown,deck,ports);
		if (k.up || k.down || k.left || k.right)
			cur ^= 1;
		if (k.a)
			return cur;
		if (k.b)
			return TCG_CANCEL;
	}
}

/* one-line description of a play area slot, between the two halves of the board */
static void slot_hint(uint8_t duelist, int loc)
{
	Slot s;
	if (!get_slot(duelist, loc, &s))
		return;
	const struct CardData *c = card_data(s.id);
	char name[24], buf[64];
	card_name(s.id, name, sizeof(name));
	if (rd16(c->Atk1.Name) && c->Atk1.Category == POKEMON_POWER) {
		char pw[24];
		text_of(rd16(c->Atk1.Name), pw, sizeof(pw));
		snprintf(buf, sizeof(buf), "%s %d/%d Power: %s", name, s.hp, s.maxhp, pw);
	} else {
		snprintf(buf, sizeof(buf), "%s %d/%d", name, s.hp, s.maxhp);
	}
	int w = (int)strlen(buf) > 27 ? 27 : (int)strlen(buf);
	fill(LEFT - 2, SLOT_INFO_Y - 1, w * CW + 4, 10, C_BLACK);
	print_n(LEFT, SLOT_INFO_Y, C_HI, buf, 27);
}

/* cursor over a duelist's play area (slots start .. start+n-1); returns the slot or -1 */
static int pick_slot(uint8_t duelist, int start, int n, int cur, bool can_cancel, int ports, const char *hints)
{
	if (n <= 0)
		n = 1;
	if (cur < start || cur >= start + n)
		cur = start;
	for (;;) {
		begin_frame();
		draw_board(duelist, cur);
		slot_hint(duelist, cur);
		draw_textbox_hint(hints);
		end_frame_poll();
		Keys k = keys(ports);
		if (k.left || k.up)
			cur = cur > start ? cur - 1 : start + n - 1;
		if (k.right || k.down)
			cur = cur < start + n - 1 ? cur + 1 : start;
		if (k.a)
			return cur;
		if (k.b && can_cancel)
			return -1;
		if (k.start)
			view_slot(duelist, cur, ports);
	}
}

static int ui_play_area(const TcgRequest *r, int ports)
{
	int loc = pick_slot(r->shown, r->arg[0], r->num_items, r->cursor, true, ports,
	                    r->arg[1] ? "A: view  B: back" : "A: choose  B: back  START: info");
	return loc < 0 ? TCG_CANCEL : loc;
}

static int ui_menu_play_area(const TcgRequest *r, int ports)
{
	/* item i is play area location arg[0] + i (arg[0] = wExcludeArenaPokemon: 1 for the bench menus) */
	int start = r->arg[0] <= 1 ? r->arg[0] : 0;
	int loc = pick_slot(r->shown, start, r->num_items, start + r->cursor, true, ports,
	                    "A: choose  B: back  START: info");
	return loc < 0 ? TCG_CANCEL : loc - start;
}

static int ui_attacks(const TcgRequest *r, int ports)
{
	int n = r->num_items > 2 ? 2 : r->num_items;
	int cur = r->cursor < n ? r->cursor : 0;
	uint8_t pairs[4] = { 0 };
	if (r->items)
		memcpy(pairs, r->items, (size_t)(n * 2));
	Slot slot;
	bool have_slot = get_slot(r->shown, 0, &slot);
	for (;;) {
		begin_frame();
		draw_board(0, -1);
		int py = BOARD_Y + 88, ph = TBOX_Y - 1 - (BOARD_Y + 88);
		panel(LEFT - 6, py, RIGHT - LEFT + 12, ph);
		char name[32];
		uint8_t deck = pairs[0] < DECK_SIZE ? pairs[0] : 0;
		uint8_t id = tcg_card_id(r->shown, deck);
		card_name(id, name, sizeof(name));
		bodyf(LEFT,py+3,C_DIM,204,"%s's attacks",name);
		if (have_slot) /* the attacker's energy */
			energy_icons(RIGHT - 8 * 9, py + 4, slot.energy, 8);
		const struct CardData *c = card_data(id);
		int y = py + 19;
		for (int i = 0; i < n; i++, y += LIST_LH + 1) {
			const struct AtkData *atk = pairs[i * 2 + 1] ? &c->Atk2 : &c->Atk1;
			if (i == cur)
				focus_row(LEFT-4,y-1,RIGHT-LEFT+8,LIST_LH);
			int w = cost_icons(LEFT + 8, y + 2, atk);
			text_of(rd16(atk->Name), name, sizeof(name));
			bool ok = !have_slot || r->shown != g_turn_holder || can_pay(atk, &slot);
			int tx=LEFT+12+(w>36?w:36);
			body(tx,y,i==cur?C_INK:ok?C_TEXT:C_DIM,name,RIGHT-tx-30);
			if (atk->Damage) {
				static const char suffix[] = { 0, '+', '-', 'x' };
				char d[8];
				snprintf(d, sizeof(d), "%d%c", atk->Damage, suffix[atk->Category & 3]);
				body(RIGHT-4-body_width(d),y,i==cur?C_INK:C_TEXT,d,32);
			}
		}
		/* description of the attack under the cursor */
		const struct AtkData *atk = pairs[cur * 2 + 1] ? &c->Atk2 : &c->Atk1;
		char lines[8][COLS + 1], desc[400];
		attack_description(atk, desc, sizeof(desc));
		int nl = wrap(desc, COLS, lines, 6);
		for (int i = 0; i < nl && y < py + ph - 9; i++, y += 9)
			print(LEFT, y + 2, C_TEXT, lines[i]);
		draw_textbox_hint("A: attack  B: back  START: card");
		end_frame_poll();
		Keys k = keys(ports);
		if (k.up || k.down)
			cur = n > 1 ? cur ^ 1 : 0;
		if (k.a && n > 0)
			return cur;
		if (k.b)
			return TCG_CANCEL;
		if (k.start)
			view_card(id, r->shown, deck, ports);
	}
}

static int label_text(void *ctx, int i, char *out, int n, int *icon)
{
	const char *const *labels = ctx;
	copy_str(out, (size_t)n, labels[i]);
	return C_TEXT;
}

/* a popup list of labels; returns the index or TCG_CANCEL */
static int choose(const char *title,const char *const *labels,const int *icons,int n,int cur,bool can_cancel,int ports)
{
	if(cur<0||cur>=n)cur=0;
	for(;;) {
		begin_frame();draw_board(0,-1);
		int width=0;
		for(int i=0;i<n;i++)if(body_width(labels[i])>width)width=body_width(labels[i]);
		if(title&&body_width(title)>width)width=body_width(title);
		if(width>256)width=256;
		int rows=n>10?10:n,top=cur>=rows?cur-rows+1:0;
		int pw=width+40;if(pw>300)pw=300;
		int x=(SCREEN_W-pw)/2,y=BOARD_Y+10;
		if(icons) {
			panel(x,y,pw,rows*LIST_LH+24);
			body(x+7,y+4,C_HI,title?title:"",pw-14);
			for(int i=top;i<n&&i<top+rows;i++) {
				int ly=y+21+(i-top)*LIST_LH;
				if(i==cur)focus_row(x+3,ly-1,pw-6,LIST_LH);
				if(icons[i]>=0)energy_icon(x+15,ly+2,icons[i]);
				body(x+27,ly,i==cur?C_INK:C_TEXT,labels[i],pw-32);
			}
		} else draw_list(x,y,pw,rows,title,n,cur,&top,label_text,(void *)labels);
		draw_textbox_hint(can_cancel?"A: choose  B: back":"A: choose");
		end_frame_poll();Keys k=keys(ports);
		if(k.up)cur=cur>0?cur-1:n-1;
		if(k.down)cur=cur<n-1?cur+1:0;
		if(k.a)return cur;
		if(k.b&&can_cancel)return TCG_CANCEL;
	}
}

static int ui_menu_generic(const TcgRequest *r, int ports)
{
	const uint8_t *w = tcg_wram();
	int n = r->num_items;
	if (n <= 0)
		return TCG_CANCEL;
	/* HandleColorChangeScreen (Shift, Conversion): 6 colors in ShiftListItemToColor order */
	if (n == 6 && w[ADDR_wMenuCursorXOffset - WRAM_START] == 1 && w[ADDR_wMenuCursorYOffset - WRAM_START] == 1 &&
	    w[ADDR_wMenuYSeparation - WRAM_START] == 2) {
		static const int order[6] = { GRASS, FIRE, WATER, LIGHTNING, FIGHTING, PSYCHIC };
		const char *labels[6];
		for (int i = 0; i < 6; i++)
			labels[i] = kTypeName[order[i]];
		return choose("Choose a color", labels, order, 6, r->cursor, true, ports);
	}
	static char buf[16][16];
	const char *labels[16];
	if (n > 16)
		n = 16;
	for (int i = 0; i < n; i++) {
		snprintf(buf[i], sizeof(buf[i]), "Option %d", i + 1);
		labels[i] = buf[i];
	}
	return choose("Choose", labels, NULL, n, r->cursor, true, ports);
}

/* TwoItemHorizontalMenu (e.g. Prophecy: "Please select the Deck: Yours Opponent's"):
 * 0 = left, 1 = right item, both named at the end of the text; no cancel */
static int ui_menu_two_items(const TcgRequest *r, int ports)
{
	char a[32], b[32];
	const char *labels[2] = { a, b };
	two_labels(r->text ? tcg_text(r->text) : g_log_n ? g_log[g_log_n - 1] : "", a, b, sizeof(a), "Left", "Right");
	int cur = r->cursor < 2 ? r->cursor : 0;
	for (;;) {
		begin_frame();
		draw_board(0, -1);
		draw_textbox_hint("A: choose");
		draw_choice_popup(RIGHT - 120, TBOX_Y - 40, NULL, labels, 2, cur);
		end_frame_poll();
		Keys k = keys(ports);
		if (k.up || k.down || k.left || k.right)
			cur ^= 1;
		if (k.a)
			return cur;
	}
}

/* Peek (Mankey's Pokemon Power), first step: whose play area; no cancel */
static int ui_peek_area(const TcgRequest *r, int ports)
{
	char a[40], b[40];
	snprintf(a, sizeof(a), "%.15s's play area", who(r->shown));
	snprintf(b, sizeof(b), "%.15s's play area", who(r->shown == TCG_PLAYER ? TCG_OPPONENT : TCG_PLAYER));
	const char *labels[2] = { a, b };
	int cur = r->cursor < 2 ? r->cursor : 0;
	for (;;) {
		begin_frame();
		draw_board(0, -1);
		draw_textbox_hint("A: choose");
		draw_choice_popup(LEFT + 60, BOARD_Y + 60, "Peek at", labels, 2, cur);
		end_frame_poll();
		Keys k = keys(ports);
		if (k.up || k.down || k.left || k.right)
			cur ^= 1;
		if (k.a)
			return cur;
	}
}

/* Peek, second step: a prize card (0-5, arg[0] = prizes left), the hand (6, only
 * if arg[1]: the other duelist's area) or the top of the deck (7); B goes back */
static int ui_peek_target(const TcgRequest *r, int ports)
{
	uint8_t prizes = r->arg[0];
	bool hand = r->arg[1] != 0;
	int cur = r->cursor < 8 ? r->cursor : 7;
	char title[48];
	snprintf(title, sizeof(title), "%s: which card?", who(r->shown));
	for (;;) {
		begin_frame();
		draw_board(0, -1);
		int x0 = LEFT + 40, y0 = BOARD_Y + 36;
		panel(x0 - 10, y0 - 18, 236, 128);
		print(x0, y0 - 12, C_HI, title);
		for (int i = 0; i < 8; i++) {
			int x = i < 6 ? x0 + (i % 3) * 40 : x0 + 130, y = i < 6 ? y0 + (i / 3) * 48 : y0 + (i - 6) * 48;
			bool ok = i < 6 ? (prizes & (1 << i)) != 0 : i == 7 || hand;
			if (ok)
				card_back(x+2,y,28,40);
			else
				rect(x, y, 32, 40, C_MID);
			if (i >= 6)
				print(x + 38, y + 16, ok ? C_TEXT : C_DIM, i == 6 ? "Hand" : "Deck");
			if (i == cur) {
				rect(x - 2, y - 2, 36, 44, C_HI);
				rect(x - 3, y - 3, 38, 46, C_HI);
			}
		}
		draw_textbox_hint("A: look  B: back");
		end_frame_poll();
		Keys k = keys(ports);
		/* prizes in 2 rows of 3, then hand (row 1) and deck (row 2) on the right */
		if (k.left)
			cur = cur == 6 ? 2 : cur == 7 ? 5 : cur % 3 ? cur - 1 : (cur < 3 ? 6 : 7);
		else if (k.right)
			cur = cur == 6 ? 0 : cur == 7 ? 3 : cur % 3 == 2 ? (cur < 3 ? 6 : 7) : cur + 1;
		else if (k.up || k.down)
			cur = cur >= 6 ? 6 + ((cur - 6) ^ 1) : (cur + 3) % 6;
		if (k.a && (cur < 6 ? (prizes & (1 << cur)) != 0 : cur == 7 || hand))
			return cur;
		if (k.b)
			return TCG_CANCEL;
	}
}

static int ui_menu_card_list(const TcgRequest *r, int ports)
{
	uint8_t items[DECK_SIZE];
	int n = r->num_items <= DECK_SIZE ? r->num_items : DECK_SIZE;
	if (!r->items)
		return TCG_CANCEL;
	memcpy(items, r->items, (size_t)n);
	int cur = r->cursor < n ? r->cursor : 0, top = 0;
	for (;;) {
		begin_frame();
		draw_card_list_screen("Choose a card", NULL, items, n, r->shown, cur, &top);
		draw_textbox_hint("A: choose  B: back  START: info");
		end_frame_poll();
		Keys k = keys(ports);
		list_nav(k, n, &cur);
		if (k.a && n > 0)
			return cur;
		if (k.b)
			return TCG_CANCEL;
		if (k.start && n > 0 && items[cur] < DECK_SIZE)
			view_card(tcg_card_id(r->shown, items[cur]), r->shown, items[cur], ports);
	}
}

static int ui_yes_no(const TcgRequest *r, int ports)
{
	const char *labels[2] = { "Yes", "No" };
	int cur = r->cursor ? 0 : 1; /* ui item 0 = yes / first item = answer 1 */
	for (;;) {
		begin_frame();
		draw_board(0, -1);
		draw_textbox_hint("A: choose  B: no");
		draw_choice_popup(RIGHT - 80, TBOX_Y - 40, NULL, labels, 2, cur);
		end_frame_poll();
		Keys k = keys(ports);
		if (k.up || k.down || k.left || k.right)
			cur ^= 1;
		if (k.a)
			return cur == 0 ? 1 : 0;
		if (k.b)
			return 0; /* B = No */
	}
}

static int ui_energy_discard(const TcgRequest *r, int ports)
{
	char title[48];
	uint8_t items[DECK_SIZE];
	int n = r->num_items <= DECK_SIZE ? r->num_items : DECK_SIZE;
	if (r->items)
		memcpy(items, r->items, (size_t)n);
	else
		n = 0;
	snprintf(title, sizeof(title), "Discard energy  %d/%d", r->arg[0], r->arg[1]);
	int cur = 0, top = 0;
	for (;;) {
		begin_frame();
		draw_card_list_screen(title, NULL, items, n, r->shown, cur, &top);
		draw_textbox_hint("A: discard  B: back");
		end_frame_poll();
		Keys k = keys(ports);
		list_nav(k, n, &cur);
		if (k.a && n > 0)
			return cur;
		if (k.b)
			return TCG_CANCEL;
	}
}

static void draw_prize_choices(uint8_t mask,int cur)
{
	draw_board(0, -1);
	int x0=LEFT+70,y0=BOARD_Y+48;
	paper_panel(x0-14,y0-27,184,151);
	banner_text(x0+3,y0-23,C_INK,"Take a Prize");
	for (int i = 0; i < 6; i++) {
		int x = x0 + (i % 3) * 54, y = y0 + (i / 3) * 52;
		if (mask & (1 << i))
		card_back(x+6,y,32,44);
		else
		rect(x+6,y,32,44,C_PAPERSHADE);
		if(i==cur) {
		rect(x+3,y-3,38,50,C_INK);
		chosen_corners(x+2,y-4,40,52);
		}
	}
	draw_textbox_hint("A: take");
}

static int ui_prize(const TcgRequest *r, int ports)
{
	uint8_t mask = r->arg[0];
	int cur = r->cursor < 6 ? r->cursor : 0;
	for (;;) {
		begin_frame();
		draw_prize_choices(mask,cur);
		end_frame_poll();
		Keys k = keys(ports);
		if (k.left)
			cur = cur % 3 ? cur - 1 : cur + 2;
		if (k.right)
			cur = cur % 3 == 2 ? cur - 2 : cur + 1;
		if (k.up || k.down)
			cur = (cur + 3) % 6;
		if (k.a && (mask & (1 << cur)))
			return cur;
	}
}

/* answer: a color 0-5 (FIRE..PSYCHIC); listed in the engine's menu order (items) if given */
static int ui_color(const TcgRequest *r, int ports)
{
	int colors[6], n = 0, cur = 0;
	if (r->items && r->num_items >= 1 && r->num_items <= 6) {
		for (int i = 0; i < r->num_items; i++)
			if (r->items[i] <= PSYCHIC)
				colors[n++] = r->items[i];
	}
	if (n == 0)
		for (n = 0; n < 6; n++)
			colors[n] = n;
	const char *labels[6];
	for (int i = 0; i < n; i++) {
		labels[i] = kTypeName[colors[i]];
		if (colors[i] == r->cursor)
			cur = i;
	}
	int i = choose("Choose a color", labels, colors, n, cur, true, ports);
	return i < 0 ? TCG_CANCEL : colors[i];
}

/* WaitForButtonAorB (e.g. Devolution Spray: A = one more stage, B = stop) */
static int ui_confirm(const TcgRequest *r, int ports)
{
	const uint8_t *w = tcg_wram();
	int loc = w[ADDR_wCurPlayAreaSlot - WRAM_START];
	if (loc >= num_in_play(r->shown))
		loc = -1;
	for (;;) {
		begin_frame();
		draw_board(r->shown, loc);
		draw_textbox_hint("A: continue  B: stop");
		end_frame_poll();
		Keys k = keys(ports);
		if (k.a)
			return 0;
		if (k.b)
			return TCG_CANCEL;
		if (k.start && loc >= 0)
			view_slot(r->shown, loc, ports);
	}
}

static int ui_number(const TcgRequest *r, int ports)
{
	int n = r->num_items ? r->num_items : 1;
	int v = r->cursor < n ? r->cursor : 0;
	for (;;) {
		begin_frame();
		draw_board(0, -1);
		panel(SCREEN_W / 2 - 60, BOARD_Y + 60, 120, 40);
		print(SCREEN_W / 2 - 52, BOARD_Y + 66, C_DIM, "Choose a number");
		printf_at(SCREEN_W / 2 - 12, BOARD_Y + 82, C_HI, "< %d >", v);
		draw_textbox_hint("A: ok  B: back");
		end_frame_poll();
		Keys k = keys(ports);
		if (k.up || k.right)
			v = v < n - 1 ? v + 1 : 0;
		if (k.down || k.left)
			v = v > 0 ? v - 1 : n - 1;
		if (k.a)
			return v;
		if (k.b)
			return TCG_CANCEL;
	}
}

/* ---- the check screen (the engine's OpenDuelCheckMenu is a no-op shim) */

typedef struct CheckCtx { uint8_t duelist; bool discard; uint8_t cards[DECK_SIZE]; int n; } CheckCtx;

static void check_collect(CheckCtx *c)
{
	const uint8_t *v = tcg_duelvars(c->duelist);
	c->n = 0;
	if (c->discard) {
		for (int i = 0; i < DECK_SIZE; i++)
			if (v[DUELVARS_CARD_LOCATIONS + i] == CARD_LOCATION_DISCARD_PILE)
				c->cards[c->n++] = (uint8_t)i;
	} else {
		for (int loc = 0; loc < num_in_play(c->duelist); loc++)
			if (v[DUELVARS_ARENA_CARD + loc] < DECK_SIZE)
				c->cards[c->n++] = v[DUELVARS_ARENA_CARD + loc];
	}
}

static void check_screen(int ports)
{
	static const char *const tabs[4] = { "%s's play area", "%s's play area", "%s's discard pile", "%s's discard pile" };
	int tab = 0, cur = 0, top = 0;
	for (;;) {
		CheckCtx c;
		c.duelist = tab & 1 ? TCG_OPPONENT : TCG_PLAYER;
		c.discard = tab >= 2;
		check_collect(&c);
		if (cur >= c.n)
			cur = c.n ? c.n - 1 : 0;
		char title[48];
		snprintf(title, sizeof(title), tabs[tab], who(c.duelist));
		begin_frame();
		draw_card_list_screen(title, "L/R: switch view", c.cards, c.n, c.duelist, cur, &top);
		draw_textbox_hint("L/R: view  START: card  B: back");
		end_frame_poll();
		Keys k = keys(ports);
		if (k.up)
			cur = cur > 0 ? cur - 1 : (c.n ? c.n - 1 : 0);
		if (k.down)
			cur = cur + 1 < c.n ? cur + 1 : 0;
		if (k.l || k.left) {
			tab = (tab + 3) % 4;
			cur = top = 0;
		}
		if (k.r || k.right) {
			tab = (tab + 1) % 4;
			cur = top = 0;
		}
		if ((k.a || k.start) && c.n)
			view_card(tcg_card_id(c.duelist, c.cards[cur]), c.duelist, c.cards[cur], ports);
		if (k.b)
			return;
	}
}

/* ---- random bot (frontend policy, same as tcgcli's random duelist) */

static int bot_answer(const TcgRequest *r)
{
	if (r->type == TCG_REQ_MENU && (r->menu == TCG_MENU_TWO_ITEMS || r->menu == TCG_MENU_PEEK_AREA))
		return (int)rnd(2); /* no cancel */
	if (r->type == TCG_REQ_MENU && r->menu == TCG_MENU_PEEK_TARGET) {
		int t = (int)rnd(8);
		if (t < 6 && !(r->arg[0] & (1 << t)))
			t = 7;
		if (t == 6 && !r->arg[1])
			t = 7;
		return t;
	}
	switch (r->type) {
	case TCG_REQ_ACK:
		return 0;
	case TCG_REQ_DUEL_MENU: {
		static const int weights[6] = { 40, 30, 1, 8, 6, 15 }; /* hand attack check power retreat done */
		int t = (int)rnd(100);
		for (int i = 0; i < 6; i++) {
			if (t < weights[i])
				return i;
			t -= weights[i];
		}
		return 5;
	}
	case TCG_REQ_YES_NO:
		return rnd(4) != 0;
	case TCG_REQ_CARD_LIST:
		if (r->arg[0] || !r->num_items || rnd(8) == 0)
			return TCG_CANCEL;
		return (int)rnd(r->num_items);
	case TCG_REQ_CARD_ITEM_MENU:
		return rnd(10) == 0 ? 1 : 0;
	case TCG_REQ_PLAY_AREA:
		if (r->arg[1] || rnd(10) == 0)
			return TCG_CANCEL;
		return r->arg[0] + (int)rnd(r->num_items ? r->num_items : 1);
	case TCG_REQ_CONFIRM:
		return rnd(3) ? 0 : TCG_CANCEL;
	case TCG_REQ_COLOR:
		if (rnd(10) == 0)
			return TCG_CANCEL;
		return r->items && r->num_items ? r->items[rnd(r->num_items)] : (int)rnd(6);
	case TCG_REQ_PRIZE:
		for (int tries = 0; tries < 32; tries++) {
			int i = (int)rnd(6);
			if (r->arg[0] & (1 << i))
				return i;
		}
		return r->cursor;
	default:
		if (!r->num_items || rnd(10) == 0)
			return TCG_CANCEL;
		return (int)rnd(r->num_items);
	}
}

/* ------------------------------------------------------------------ host callbacks */

/* ---- saving (save_store.h on the cartridge EEPROM, see save_store_n64.c) */

static uint8_t g_save_img[TCG_SAVE_SIZE];   /* the engine's latest duel save */
static bool g_save_dirty;                    /* newer than what the store has */
static bool g_save_mode_written;             /* "duelmode" written for this duel */
static uint8_t g_duel_decks[2];              /* deck ids of the duel (for "Continue Duel") */

/* TcgHost.save: called at every duel menu print; only kept in RAM here */
static void on_save(void *ctx, const uint8_t *data, uint32_t size)
{
	(void)ctx;
	if (!g_save_ok || size != TCG_SAVE_SIZE || !memcmp(g_save_img, data, size))
		return;
	memcpy(g_save_img, data, size);
	g_save_dirty = true;
}

/* at turn boundaries: give the latest save to the store, which writes the
 * changed EEPROM blocks in the background (save_store_poll in end_frame) */
static void save_commit(void)
{
	if (!g_save_ok || !g_save_dirty)
		return;
	if (!g_save_mode_written) {
		/* how the sides are controlled, so that "Continue" maps the controllers the same way */
		uint8_t m[4] = { 'M', (uint8_t)g_mode, g_duel_decks[0], g_duel_decks[1] };
		save_store_write("duelmode", m, sizeof(m));
		g_save_mode_written = true;
	}
	save_store_write(SAVE_STORE_DUEL, g_save_img, TCG_SAVE_SIZE);
	g_save_dirty = false;
	debugf("SAVE turn=%d blocks=%d\n", g_turn + 1, save_store_pending());
}

/* the duel is over: delete its save (only the file headers get written) */
static void save_discard(void)
{
	if (!g_save_ok)
		return;
	save_store_remove(SAVE_STORE_DUEL);
	save_store_remove("duelmode");
	g_save_dirty = false;
}

static const char *const kMenuName[] = { "generic", "attacks", "play_area", "two_items", "card_list", "peek_area",
	"peek_target" };
#define NUM_MENU_NAMES ((int)(sizeof(kMenuName) / sizeof(kMenuName[0])))
static const char *const kReqName[] = { "?", "ack", "duel_menu", "card_list", "item_menu", "play_area", "menu",
	"yes_no", "energy_discard", "prize", "color", "number", "confirm" };

static void on_event(void *ctx, const TcgEvent *ev)
{
	(void)ctx;
#ifdef TCGN64_ADVENTURE
	if(ev->type==TCG_EV_MUSIC)adventure_audio_event(0,ev->arg[0]);
	if(ev->type==TCG_EV_SFX)adventure_audio_event(1,ev->arg[0]);
#endif
	switch (ev->type) {
	case TCG_EV_TEXT:
		if(ev->arg[1]==TCG_TEXT_PRACTICE_CHECKLIST) {
			sanitize(ev->str?ev->str:"",g_practice_checklist,sizeof(g_practice_checklist));
			g_practice_checklist_on=g_practice_checklist[0]!=0;
			break;
		}
		g_practice_reading_on=ev->arg[1]==TCG_TEXT_PRACTICE_SPEECH;
		if(g_practice_reading_on)set_practice_reading(ev);
		/* the item labels of the PLAY|CHECK / SELECT|CHECK menus are printed as text: not a message */
		if (ev->text == PlayCheck1Text || ev->text == PlayCheck2Text || ev->text == SelectCheckText)
			break;
		if (ev->str && ev->str[0])
			log_message(ev->str);
		show_frames(1);
		break;
	case TCG_EV_SCREEN:
		g_screen = ev->arg[0];
		g_screen_arg = ev->arg[2];
		if (g_screen == TCG_SCREEN_COIN_TOSS) {
			memset(&g_coin,0,sizeof(g_coin));
			g_coin.total=ev->arg[2];g_coin.owner=ev->arg[1];g_coin.ready=true;
		}
		if (g_screen == TCG_SCREEN_BOX_MESSAGE)
			g_boxmsg = ev->arg[2];
		if (g_screen != TCG_SCREEN_LARGE_CARD)
			g_overlay.on = false;
		break;
	case TCG_EV_CARD:
		g_overlay.on = true;
		g_overlay.deck = ev->arg[0];
		g_overlay.owner = ev->arg[1];
		g_overlay.id = ev->arg[2];
		g_overlay.page = ev->arg[3];
		break;
	case TCG_EV_COIN:
		coin_record(ev);
		debugf("COIN %d/%d %s\n", ev->arg[1], ev->arg[2] ? ev->arg[2] : 1, ev->arg[0] ? "heads" : "tails");
		animate_coin();
		break;
	case TCG_EV_ANIM:
		if(ev->arg[1]&&ev->arg[0]==DUEL_ANIM_COIN_SPIN) {
			g_coin.ready=true;g_coin.owner=ev->arg[2];
		}
		break;
	case TCG_EV_DAMAGE:
		g_dmg.side = ev->arg[0];
		g_dmg.loc = ev->arg[1];
		g_dmg.amount = (unsigned)(ev->arg[2] | ev->arg[3] << 8);
		g_dmg.timer = 40;
		debugf("DAMAGE %s slot %d: %u\n", who(ev->arg[0]), ev->arg[1], g_dmg.amount);
		show_frames(20);
		break;
	case TCG_EV_WAIT:
		show_frames((ev->arg[0] > 60 ? 60 : ev->arg[0]) / 2);
		break;
	case TCG_EV_TURN:
		g_practice_checklist[0]=0;g_practice_checklist_on=false;
		g_practice_reading_on=false;
		g_result=-1;
		g_turn_holder = ev->arg[0];
		g_turn = ev->arg[1];
		g_thinking = 0;
		save_commit();
		debugf("TURN %d %s\n", ev->arg[1] + 1, who(ev->arg[0]));
		show_frames(1);
		break;
	case TCG_EV_DUEL_END:
		g_result=ev->arg[0]==TURN_PLAYER_TIED?2:
		         side_of(ev->arg[0]==TURN_PLAYER_WON?ev->arg[1]:
		                 ev->arg[1]==TCG_PLAYER?TCG_OPPONENT:TCG_PLAYER);
		debugf("DUEL_END finished=%d holder=%s\n", ev->arg[0], who(ev->arg[1]));
		break;
	case TCG_EV_THINKING:
		g_thinking = ev->arg[0];
		show_frames(1);
		break;
	case TCG_EV_SFX:
		if (ev->arg[0] == SFX_DENIED) /* invalid choice: flash the screen border */
			g_denied = 12;
		break;
	default:
		break;
	}
}

static int on_request(void *ctx, const TcgRequest *r)
{
	(void)ctx;
	if(r->type!=TCG_REQ_ACK){g_practice_checklist_on=false;g_practice_reading_on=false;}
#ifdef TCGN64_ADVENTURE
	if(g_automated_ui){
#if ADVENTURE_DIAGNOSTIC == 3
		unsigned bit=0;
		if(r->type==TCG_REQ_DUEL_MENU) {bit=1;g_review_label="duel-action-menu";}
		else if(r->type==TCG_REQ_CARD_LIST&&r->num_items>0) {bit=2;g_review_label="duel-card-list";}
		else if(r->type==TCG_REQ_ACK&&g_overlay.on) {bit=4;g_review_label="duel-card-detail";}
		else if(r->type==TCG_REQ_ACK&&g_screen==TCG_SCREEN_COIN_TOSS) {bit=8;g_review_label="duel-coin";}
		else if(r->type==TCG_REQ_ACK&&g_screen==TCG_SCREEN_BOX_MESSAGE&&g_boxmsg==BOXMSG_PLAYERS_TURN) {bit=16;g_review_label="duel-player-turn";}
		else if(r->type==TCG_REQ_ACK&&g_screen==TCG_SCREEN_BOX_MESSAGE&&g_boxmsg==BOXMSG_OPPONENTS_TURN) {bit=32;g_review_label="duel-opponent-turn";}
		if(bit&&!(g_review_seen&bit)) {
			g_review_seen|=bit;
			if(r->type==TCG_REQ_DUEL_MENU)ui_duel_menu(r,1);
			else if(r->type==TCG_REQ_CARD_LIST)ui_card_list(r,1);
			else ui_ack(r,1);
		}
		g_review_label=NULL;
#endif
		if(r->type==TCG_REQ_ACK||r->type==TCG_REQ_CONFIRM)return 0;
		return bot_answer(r);
	}
#endif
	int side = side_of(r->duelist);
	int ans;
	g_thinking = 0;
	if (r->type == TCG_REQ_ACK) {
		/* text boxes are always acknowledged by a human, so that they can be read */
		int ports = g_mode == MODE_HOTSEAT ? 3 : 1;
		ans = ui_ack(r, ports);
	} else if (g_kind[side] == PK_AI && ((r->type == TCG_REQ_CARD_LIST && r->arg[0]) || r->type == TCG_REQ_CONFIRM)) {
		/* shown to the human although the engine AI "asks": a view-only list (e.g. the
		 * hand checked by the AI's Lass) or the play area an AI's effect changed
		 * (WaitForButtonAorB). The other duelist is always the human on controller 1. */
		debugf("ASK %s(ai) %s n=%d\n", kReqName[r->type], who(r->duelist), r->num_items);
		ans = r->type == TCG_REQ_CONFIRM ? ui_confirm(r, 1) : ui_card_list(r, 1);
	} else if (g_kind[side] != PK_HUMAN) {
		/* the random bot, or an AI duelist reaching a human input routine (should not happen) */
		show_frames(2);
		ans = bot_answer(r);
	} else {
		int ports = ports_for(r->duelist);
		g_asking = r->duelist;
		debugf("ASK %s%s%s %s n=%d cursor=%d\n", r->type < sizeof(kReqName) / sizeof(kReqName[0]) ? kReqName[r->type] : "?",
		       r->type == TCG_REQ_MENU ? "/" : "", r->type == TCG_REQ_MENU && r->menu < NUM_MENU_NAMES ? kMenuName[r->menu] : "",
		       who(r->duelist), r->num_items, r->cursor);
		switch (r->type) {
		case TCG_REQ_DUEL_MENU: ans = ui_duel_menu(r, ports); break;
		case TCG_REQ_CARD_LIST: ans = ui_card_list(r, ports); break;
		case TCG_REQ_CARD_ITEM_MENU: ans = ui_item_menu(r, ports); break;
		case TCG_REQ_PLAY_AREA: ans = ui_play_area(r, ports); break;
		case TCG_REQ_MENU:
			switch (r->menu) {
			case TCG_MENU_ATTACKS: ans = ui_attacks(r, ports); break;
			case TCG_MENU_PLAY_AREA: ans = ui_menu_play_area(r, ports); break;
			case TCG_MENU_TWO_ITEMS: ans = ui_menu_two_items(r, ports); break;
			case TCG_MENU_CARD_LIST: ans = ui_menu_card_list(r, ports); break;
			case TCG_MENU_PEEK_AREA: ans = ui_peek_area(r, ports); break;
			case TCG_MENU_PEEK_TARGET: ans = ui_peek_target(r, ports); break;
			default: ans = ui_menu_generic(r, ports); break;
			}
			break;
		case TCG_REQ_YES_NO: ans = ui_yes_no(r, ports); break;
		case TCG_REQ_ENERGY_DISCARD: ans = ui_energy_discard(r, ports); break;
		case TCG_REQ_PRIZE: ans = ui_prize(r, ports); break;
		case TCG_REQ_COLOR: ans = ui_color(r, ports); break;
		case TCG_REQ_NUMBER: ans = ui_number(r, ports); break;
		case TCG_REQ_CONFIRM: ans = ui_confirm(r, ports); break;
		default: ans = TCG_CANCEL; break;
		}
	}
	g_asking = 0;
	debugf("REQ %s%s%s %s n=%d -> %d\n", r->type < sizeof(kReqName) / sizeof(kReqName[0]) ? kReqName[r->type] : "?",
	       r->type == TCG_REQ_MENU ? "/" : "", r->type == TCG_REQ_MENU && r->menu < NUM_MENU_NAMES ? kMenuName[r->menu] : "",
	       who(r->duelist), r->num_items, ans);
	return ans;
}

#ifdef TCGN64_HOST_TEST
/* host tests only (TCGN64_SELFTEST=1): before the first duel menu of the
 * player, feed the UI the request types that random play rarely reaches */
static int on_request_selftest(void *ctx, const TcgRequest *menu)
{
	static bool done;
	const char *env = getenv("TCGN64_SELFTEST");
	if (!done && env && *env == '1' && menu->type == TCG_REQ_DUEL_MENU && menu->duelist == TCG_PLAYER) {
		done = true;
		const uint8_t *v = tcg_duelvars(TCG_PLAYER);
		TcgRequest r;
		memset(&r, 0, sizeof(r));
		r.duelist = r.shown = TCG_PLAYER;
		r.type = TCG_REQ_YES_NO;
		r.num_items = 2;
		r.cursor = 1;
		r.text = IsThisOKText;
		on_request(ctx, &r);
		r.type = TCG_REQ_MENU; /* TwoItemHorizontalMenu */
		r.menu = TCG_MENU_TWO_ITEMS;
		r.cursor = 0;
		r.text = PleaseSelectTheDeckText;
		on_request(ctx, &r);
		memset(&r, 0, sizeof(r));
		r.duelist = r.shown = TCG_PLAYER;
		static const uint8_t shift_order[6] = { GRASS, FIRE, WATER, LIGHTNING, FIGHTING, PSYCHIC };
		r.type = TCG_REQ_COLOR;
		r.num_items = 6;
		r.items = shift_order;
		r.cursor = GRASS;
		on_request(ctx, &r);
		r.items = NULL;
		r.type = TCG_REQ_NUMBER;
		r.num_items = 10;
		r.cursor = 3;
		on_request(ctx, &r);
		r.type = TCG_REQ_MENU;
		r.menu = TCG_MENU_GENERIC;
		r.num_items = 3;
		r.cursor = 0;
		on_request(ctx, &r);
		r.menu = TCG_MENU_TWO_ITEMS;
		r.num_items = 2;
		on_request(ctx, &r);
		r.menu = TCG_MENU_CARD_LIST;
		r.num_items = v[DUELVARS_NUMBER_OF_CARDS_IN_HAND];
		r.items = &v[DUELVARS_HAND];
		on_request(ctx, &r);
		memset(&r, 0, sizeof(r));
		r.duelist = r.shown = TCG_PLAYER;
		r.type = TCG_REQ_PRIZE;
		r.num_items = 6;
		r.arg[0] = 0x3e; /* prize 0 already taken */
		r.cursor = 1;
		on_request(ctx, &r);
		memset(&r, 0, sizeof(r));
		r.duelist = r.shown = TCG_PLAYER;
		r.type = TCG_REQ_CONFIRM;
		on_request(ctx, &r);
		on_request(ctx, &r);
		/* Peek: whose play area, then which card */
		memset(&r, 0, sizeof(r));
		r.duelist = r.shown = TCG_PLAYER;
		r.type = TCG_REQ_MENU;
		r.menu = TCG_MENU_PEEK_AREA;
		r.num_items = 2;
		on_request(ctx, &r);
		r.menu = TCG_MENU_PEEK_TARGET;
		r.num_items = 8;
		r.arg[0] = 0x3e; /* prize 0 taken */
		r.arg[1] = 1;    /* the other duelist's area: the hand can be chosen */
		on_request(ctx, &r);
		on_request(ctx, &r);
		/* requests on the opponent's side as if it were the engine AI: shown to controller 1 */
		int kind = g_kind[1];
		g_kind[1] = PK_AI;
		memset(&r, 0, sizeof(r));
		r.duelist = r.shown = TCG_OPPONENT;
		r.type = TCG_REQ_CONFIRM;
		on_request(ctx, &r);
		r.type = TCG_REQ_CARD_LIST; /* e.g. the hand checked by the AI's Lass */
		r.shown = TCG_PLAYER;
		r.num_items = v[DUELVARS_NUMBER_OF_CARDS_IN_HAND];
		r.items = &v[DUELVARS_HAND];
		r.text = DuelistHandText;
		r.arg[0] = 1;
		on_request(ctx, &r);
		/* the same view-only list on the random bot's side: the bot leaves it */
		g_kind[1] = PK_BOT;
		on_request(ctx, &r);
		g_kind[1] = kind;
	}
	return on_request(ctx, menu);
}
#define on_request on_request_selftest
#endif

#if defined(TCGN64_HOST_TEST) || ADVENTURE_DIAGNOSTIC == 9 || ADVENTURE_DIAGNOSTIC == 10
/* Verify reflow against every card's actual source text, independent of the
 * renderer: punctuation and conditions must survive, and no line may exceed
 * its measured pixel allocation. Runs in the diagnostic/host builds only. */
static bool verify_rule_wrap(const char *source,int width)
{
	char lines[RULE_LINES][RULE_COLS];
	int count=body_wrap(source,width,lines,RULE_LINES);
	const char *original=source;
	for(int i=0;i<count;i++) {
		if(body_width(lines[i])>width)return false;
		for(const char *text=lines[i];*text;text++) {
			if(*text==' '||*text=='\n')continue;
			while(*original==' '||*original=='\n')original++;
			if(*text!=*original)return false;
			original++;
		}
	}
	while(*original==' '||*original=='\n')original++;
	return !*original;
}

#ifdef TCGN64_HOST_TEST
static uint64_t practice_footer_hash(void)
{
	uint64_t hash=1469598103934665603ULL;
	const uint8_t *bytes=g_fb->buffer;
	for(int y=ui_y(TBOX_Y);y<ui_y(TBOX_Y+TBOX_LINES*TLH+5);y++)
		for(int x=0;x<g_fb->stride;x++)hash=(hash^bytes[y*g_fb->stride+x])*1099511628211ULL;
	return hash;
}

static bool verify_practice_reading(void)
{
	int mode=g_mode,screen=g_screen;g_mode=MODE_PRACTICE;g_screen=TCG_SCREEN_EMPTY;
	char summary[1024];snprintf(summary,sizeof(summary),"Mark's turn 2\n\n%s\n\n%s\n\n%s",
		tcg_text(Turn2Instr1PracticeDuelText),tcg_text(Turn2Instr2PracticeDuelText),tcg_text(Turn2Instr3PracticeDuelText));
	TcgEvent checklist={TCG_EV_TEXT,{0,TCG_TEXT_PRACTICE_CHECKLIST,3,0},PlayersTurnPracticeDuelText,summary};
	int messages=g_log_n;on_event(NULL,&checklist);
	if(g_log_n!=messages||!practice_checklist_visible())return false;
	uint64_t long_footer=0,short_footer=0;int lessons=0;
	for(unsigned id=Turn1DrMason1PracticeDuelText;id<=LetsPlayTheGamePracticeDuelText;id++) {
		char message[512];tcg_format_text((uint16_t)id,message,sizeof(message));
		TcgEvent lesson={TCG_EV_TEXT,{1,TCG_TEXT_PRACTICE_SPEECH,0,0},(uint16_t)id,message};
		g_practice_reading_on=true;set_practice_reading(&lesson);
		if(!practice_reading_visible()||g_practice_reading_count<=0)return false;
		for(int first=0;first<g_practice_reading_count;first+=PRACTICE_PAGE_LINES) {
			begin_frame();draw_practice_reading(first);draw_practice_continue(first);end_frame();
			if(id==Turn1DrMason1PracticeDuelText)long_footer=practice_footer_hash();
			if(id==Turn1DrMason3PracticeDuelText)short_footer=practice_footer_hash();
		}
		lessons++;
	}
	if(!long_footer||long_footer!=short_footer)return false;
	g_mode=mode;g_screen=screen;log_clear();
	debugf("PRACTICE LAYOUT PASS lessons=%d uniform_full_panel=1 hint_only_footer=1 dimensions=%ux%u\n",lessons,g_fb->width,g_fb->height);
	return true;
}
#endif

static bool verify_card_layout(void)
{
	int descriptions=0,max_pages=0,longest_trainer=0,trainer_pages=0,trainer_lines=0,max_card=0;
	for(int id=1;id<=NUM_CARDS;id++) {
		const struct CardData *card=card_data((uint8_t)id);
		int pages=card_rules(card);
		if(pages>max_pages){max_pages=pages;max_card=id;}
		if(!is_pokemon(card)&&!is_energy(card)&&g_rule_count>trainer_lines) {
			trainer_pages=pages;longest_trainer=id;trainer_lines=g_rule_count;
		}
		for(int attack=0;attack<(is_pokemon(card)?2:1);attack++) {
			char source[1400];
			if(is_pokemon(card)) {
				const struct AtkData *atk=attack?&card->Atk2:&card->Atk1;
				if(strlen(tcg_text(rd16(atk->Description)))+
				   strlen(tcg_text(rd16(atk->Description+2)))+2>=sizeof(source))return false;
				attack_description(atk,source,sizeof(source));
			} else {
				if(strlen(tcg_text(rd16(card->NonPokemonDescription)))+1>=sizeof(source))return false;
				description(rd16(card->NonPokemonDescription),source,sizeof(source));
			}
			if(!verify_rule_wrap(source,RIGHT-LEFT-8)||!verify_rule_wrap(source,121))return false;
			if(source[0])descriptions++;
		}
		/* Render all card pages as well: the host graphics mock rejects any
		 * writes beyond the 320x240 surface. */
		for(int page=0;page<pages;page++) {
			begin_frame();draw_card_page_at((uint8_t)id,TCG_PLAYER,-1,NULL,page);end_frame();
		}
	}
	debugf("UI LAYOUT PASS cards=%d descriptions=%d max_pages=%d max_card=%d trainer=%d trainer_pages=%d trainer_lines=%d\n",
	       NUM_CARDS,descriptions,max_pages,max_card,longest_trainer,trainer_pages,trainer_lines);
#ifdef TCGN64_HOST_TEST
	if(!verify_practice_reading())return false;
#endif
	return true;
}
#endif

/* the engine stops on an unrecoverable error: show it and halt */
void tcg_fatal(const char *msg)
{
	char clean[256];
	char lines[12][COLS + 1];
	sanitize(msg, clean, sizeof(clean));
	int n = wrap(clean, COLS, lines, 12);
	debugf("FATAL %s\n", clean);
	for (;;) {
		begin_frame();
		fill(0, 0, SCREEN_W, SCREEN_H, C_FATAL);
		print(LEFT, 24, C_HI, "The duel engine stopped:");
		for (int i = 0; i < n; i++)
			print(LEFT, 44 + i * LH, C_TEXT, lines[i]);
		print(LEFT, 200, C_DIM, "Press RESET to restart.");
		end_frame_poll();
	}
}

/* ------------------------------------------------------------------ title screen */

/* decks a duelist can use: SAMS_NORMAL_DECK_ID .. IMAKUNI_DECK_ID, starters first */
static uint8_t g_decks[64];
static int g_num_decks;

static void deck_name(uint8_t deck_id, char *out, int n)
{
	const uint8_t *p = gDeckLists[deck_id + 2];
	while (*p)
		p += 2;
	sanitize(tcg_text((uint16_t)(p[1] | p[2] << 8)), out, n);
}

static void init_decks(void)
{
	static const uint8_t first[] = { CHARMANDER_AND_FRIENDS_DECK_ID, SQUIRTLE_AND_FRIENDS_DECK_ID,
		BULBASAUR_AND_FRIENDS_DECK_ID };
	g_num_decks = 0;
	for (size_t i = 0; i < sizeof(first); i++)
		g_decks[g_num_decks++] = first[i];
	for (int id = SAMS_NORMAL_DECK_ID; id <= IMAKUNI_DECK_ID && g_num_decks < 64; id++) {
		bool dup = false;
		for (size_t i = 0; i < sizeof(first); i++)
			dup |= first[i] == id;
		if (!dup && id + 2 < NUM_VALID_DECKS)
			g_decks[g_num_decks++] = (uint8_t)id;
	}
}

static void load_buildinfo(void)
{
	FILE *f = fopen("rom:/buildinfo.txt", "r");
	char line[512], all[1024] = "";
	g_buildinfo_lines = 0;
	if (!f) {
		snprintf(g_buildinfo[g_buildinfo_lines++], COLS + 1, "(no build info)");
		return;
	}
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "ai=", 3)) {
			g_have_ai = line[3] == '1';
			continue;
		}
		size_t len = strlen(all);
		snprintf(all + len, sizeof(all) - len, "%s%s", len ? "\n" : "", line);
	}
	fclose(f);
	g_buildinfo_lines = wrap(all, COLS, g_buildinfo, 6);
}

typedef struct Setup { int mode, deck[2], prizes; } Setup;

static const char *const kModeName[NUM_MODES] = { "VS CPU (engine AI)", "VS random bot", "2 players (hot-seat)",
	"Practice with Sam" };

/* "settings" file: the last title screen choices */
static void load_settings(Setup *s)
{
	uint8_t b[16];
	if (!g_save_ok || save_store_read("settings", b, sizeof(b)) < 5 || b[0] != 'S')
		return;
	if (b[1] < NUM_MODES)
		s->mode = b[1];
	if (b[2] < g_num_decks)
		s->deck[0] = b[2];
	if (b[3] < g_num_decks)
		s->deck[1] = b[3];
	if (b[4] >= 1 && b[4] <= 6)
		s->prizes = b[4];
}

static void store_settings(const Setup *s)
{
	uint8_t b[5] = { 'S', (uint8_t)s->mode, (uint8_t)s->deck[0], (uint8_t)s->deck[1], (uint8_t)s->prizes };
	if (g_save_ok)
		save_store_write("settings", b, sizeof(b)); /* only changed blocks get written */
}

enum { ROW_CONTINUE, ROW_MODE, ROW_DECK1, ROW_DECK2, ROW_PRIZES, ROW_START, NUM_ROWS };

/* returns true for "Continue Duel" (saved != NULL), false for a new duel with *s */
static bool title_screen(Setup *s, const TcgSavedDuel *saved, int saved_mode)
{
	int row = saved ? ROW_CONTINUE : ROW_MODE;
	debugf("TITLE continue=%d\n", saved != NULL);
	for (;;) {
		bool practice = s->mode == MODE_PRACTICE;
		char name[2][32];
		deck_name(practice ? PRACTICE_PLAYER_DECK_ID : g_decks[s->deck[0]], name[0], sizeof(name[0]));
		deck_name(practice ? TCG_SAMS_PRACTICE_DECK_ID : g_decks[s->deck[1]], name[1], sizeof(name[1]));
		begin_frame();
		fill(0, 0, SCREEN_W, 46, C_PANEL);
		print(LEFT + 20, 12, C_HI, "POKEMON TRADING CARD GAME");
		print(LEFT + 20, 26, C_TEXT, "duel engine - Nintendo 64 port");
		int y = 52;
		if (saved) {
			char opp[32], b[48];
			/* the opponent's deck: from the save for an AI, else as recorded in "duelmode" */
			uint8_t m[4];
			if (saved->duelist_type[1] & DUELIST_TYPE_AI_OPP)
				deck_name(saved->duelist_type[1] & 0x7f, opp, sizeof(opp));
			else if (save_store_read("duelmode", m, sizeof(m)) == 4 && m[3] <= IMAKUNI_DECK_ID)
				deck_name(m[3], opp, sizeof(opp));
			else
				copy_str(opp, sizeof(opp), "?");
			if (row == ROW_CONTINUE)
				fill(LEFT - 4, y - 3, RIGHT - LEFT + 8, 24, C_SEL);
			print(LEFT + 4, y, row == ROW_CONTINUE ? C_HI : C_TEXT, "CONTINUE DUEL");
			snprintf(b, sizeof(b), "turn %d, prizes %d-%d", saved->turns + 1, saved->prizes_left[0], saved->prizes_left[1]);
			print(LEFT + 16 * CW, y, C_DIM, b);
			snprintf(b, sizeof(b), "%s, %s", saved_mode >= 0 ? kModeName[saved_mode] : "?", opp);
			print_n(LEFT + 4, y + 10, C_DIM, b, COLS - 1);
		}
		y = 82;
		const char *labels[NUM_ROWS] = { "", "Mode", s->mode == MODE_HOTSEAT ? "P1 deck" : "Your deck",
			s->mode == MODE_HOTSEAT ? "P2 deck" : "Opponent", "Prizes", "" };
		for (int i = ROW_MODE; i < NUM_ROWS; i++, y += 15) {
			bool fixed = practice && (i == ROW_DECK1 || i == ROW_DECK2 || i == ROW_PRIZES);
			if (i == row)
				fill(LEFT - 4, y - 3, RIGHT - LEFT + 8, 14, C_SEL);
			if (i < ROW_START) {
				print(LEFT + 4, y, C_DIM, labels[i]);
				char v[48];
				if (i == ROW_MODE)
					copy_str(v, sizeof(v), kModeName[s->mode]);
				else if (i == ROW_DECK1 || i == ROW_DECK2)
					copy_str(v, sizeof(v), name[i - ROW_DECK1]);
				else
					snprintf(v, sizeof(v), "%d", practice ? 2 : s->prizes);
				if (fixed)
					print(LEFT + 13 * CW, y, C_DIM, v);
				else
					printf_at(LEFT + 11 * CW, y, i == row ? C_HI : C_TEXT, "< %s >", v);
			} else {
				print(LEFT + 12 * CW, y, i == row ? C_HI : C_TEXT, "START NEW DUEL");
			}
		}
		y = 170;
		if ((s->mode == MODE_VS_AI || practice) && !g_have_ai) {
			print(LEFT, y, C_RED, "The AI port is not linked yet: an AI");
			print(LEFT, y + 9, C_RED, "duel stops at its first AI decision.");
		} else if (s->mode == MODE_HOTSEAT) {
			print(LEFT, y, C_DIM, "P2 plays with controller 2 (or 1).");
		} else if (s->mode == MODE_VS_BOT) {
			print(LEFT, y, C_DIM, "The bot picks random legal inputs.");
		} else if (practice) {
			print(LEFT, y, C_DIM, "Dr. Mason explains the rules.");
		}
		if (!g_save_ok)
			print(LEFT, y + 18, C_DIM, "No 16K EEPROM: duels are not saved.");
		for (int i = 0; i < g_buildinfo_lines && i < 2; i++)
			print(LEFT, 208 + i * 9, C_DIM, g_buildinfo[i]);
		end_frame_poll();
		Keys k = keys(3);
		int first = saved ? ROW_CONTINUE : ROW_MODE;
		if (k.up)
			row = row > first ? row - 1 : ROW_START;
		if (k.down)
			row = row < ROW_START ? row + 1 : first;
		int d = k.left ? -1 : k.right ? 1 : 0;
		if (k.l)
			d = -8;
		if (k.r)
			d = 8;
		bool fixed = practice && (row == ROW_DECK1 || row == ROW_DECK2 || row == ROW_PRIZES);
		if (d && !fixed) {
			if (row == ROW_MODE && (d == 1 || d == -1))
				s->mode = (s->mode + NUM_MODES + d) % NUM_MODES;
			else if (row == ROW_DECK1 || row == ROW_DECK2)
				s->deck[row - ROW_DECK1] = (s->deck[row - ROW_DECK1] + 8 * g_num_decks + d) % g_num_decks;
			else if (row == ROW_PRIZES && (d == 1 || d == -1))
				s->prizes = s->prizes + d < 1 ? 6 : s->prizes + d > 6 ? 1 : s->prizes + d;
		}
		if (k.a && row == ROW_CONTINUE)
			return true;
		if (k.start || (k.a && row == ROW_START))
			return false;
		if (k.a && row != ROW_CONTINUE && row < ROW_START)
			row++;
	}
}

static void result_screen(int result)
{
	char msg[48];
	uint8_t winner = result == DUEL_WIN ? TCG_PLAYER : TCG_OPPONENT;
	snprintf(msg, sizeof(msg), "%s wins the duel!", who(winner));
	debugf("RESULT %s (turns %d)\n", msg, g_turn + 1);
	for (int f = 0;; f++) {
		begin_frame();
		draw_board(0, -1);
		draw_textbox();
		if(g_result!=2)g_result=side_of(winner);
		draw_match_plaque(true);
		body(106,137,C_TEXT,"A: back to title",130);
		end_frame_poll();
		Keys k=keys(3);
		if (f > 30 && (k.a || k.start))
			return;
	}
}

/* ------------------------------------------------------------------ main */

static void reset_duel_ui(void)
{
	input_discard_pending();
	log_clear();
	memset(&g_overlay, 0, sizeof(g_overlay));
	memset(&g_coin, 0, sizeof(g_coin));
	memset(&g_dmg, 0, sizeof(g_dmg));
	g_turn_holder = 0;
	g_turn = 0;
	g_thinking = 0;
	g_screen = TCG_SCREEN_NONE;
	g_result=-1;
}

/* who answers each side's requests, and the names, in mode m */
static void set_mode(int m)
{
	static const char *const names[NUM_MODES][2] = { { "PLAYER", "CPU" }, { "PLAYER", "BOT" }, { "P1", "P2" },
		{ "PLAYER", "SAM" } };
	g_mode = m;
	g_kind[0] = PK_HUMAN;
	g_kind[1] = m == MODE_VS_AI || m == MODE_PRACTICE ? PK_AI : m == MODE_VS_BOT ? PK_BOT : PK_HUMAN;
	copy_str(g_name[0], sizeof(g_name[0]), names[m][0]);
	copy_str(g_name[1], sizeof(g_name[1]), names[m][1]);
}

/* the mode of a saved duel: the "duelmode" file, checked against the save (an AI
 * side needs an AI mode; bot and hot-seat sides are both DUELIST_TYPE_PLAYER) */
static int saved_duel_mode(const TcgSavedDuel *info)
{
	uint8_t b[8];
	bool ai = (info->duelist_type[1] & DUELIST_TYPE_AI_OPP) != 0;
	int m = -1;
	if (save_store_read("duelmode", b, sizeof(b)) >= 2 && b[0] == 'M' && b[1] < NUM_MODES)
		m = b[1];
	bool ai_mode = m == MODE_VS_AI || m == MODE_PRACTICE;
	if (m < 0 || ai_mode != ai)
		m = !ai ? MODE_HOTSEAT : (info->duelist_type[1] & 0x7f) == TCG_SAMS_PRACTICE_DECK_ID ? MODE_PRACTICE : MODE_VS_AI;
	return m;
}

#ifdef TCGN64_ADVENTURE
#include "adventure/duel_bridge.h"

void n64_ui_draw_text(surface_t *canvas,int x,int y,uint32_t color,const char *text,int font,int width)
{
	if(!canvas||!text||width<=0)return;
	surface_t *previous=g_fb;g_fb=canvas;
	int limit=x+width;if(limit>SCREEN_W)limit=SCREEN_W;
	if(font==N64_UI_FONT_HEADING)banner_run(x,y,color,text,limit);
	else if(font==N64_UI_FONT_BODY)body_run(x,y,color,text,limit);
	else compact_text(x,y,color,text,limit);
	g_fb=previous;
}

void n64_ui_init(void)
{
	g_raster_width=display_get_width();
	g_ui_canvas=surface_alloc(FMT_RGBA16,display_get_width(),display_get_height());
	assertf(g_ui_canvas.buffer,"UI canvas allocation failed");
	for (int i=0;i<NUM_COLORS;i++)
		g_col[i]=graphics_make_color(kRGB[i][0],kRGB[i][1],kRGB[i][2],255);
	load_buildinfo();
	g_save_ok=save_store_open(NULL);
}

void n64_card_name(unsigned id,char *out,int capacity) { card_name(id,out,capacity); }
void n64_ui_sanitize_text(const char *text,char *out,unsigned capacity)
{sanitize(text,out,(int)capacity);}
int n64_ui_wrap_columns(const char *text,unsigned columns,char lines[][37],unsigned capacity)
{
	if(!capacity||!columns)return 0;
	if(!text)text="";
	size_t length=strlen(text)+1;char *clean=malloc(length);
	assertf(clean,"Text wrapping allocation failed");
	sanitize(text,clean,(int)length);
	int count=wrap(clean,(int)columns,lines,(int)capacity);
	free(clean);return count;
}
int n64_ui_wrap_text(const char *text,char lines[][37],unsigned capacity)
{return n64_ui_wrap_columns(text,36,lines,capacity);}
void n64_duel_rng(uint8_t out[3]) { out[0]=wRNG1;out[1]=wRNG2;out[2]=wRNGCounter; }
void n64_duel_automated(bool enabled) {g_automated_ui=enabled;g_automated_events=0;}
surface_t *n64_ui_frame(void) {return g_last_ui_frame;}
void n64_map_label(surface_t *frame,const char *name)
{
	surface_t cached=g_ui_canvas;cached.buffer=CachedAddr(cached.buffer);
	char clean[40];sanitize(name,clean,sizeof(clean));
	int width=(int)strlen(clean)*CW+16;if(width>304)width=304;
	if(width<96)width=96;
	surface_t *previous=g_fb;g_fb=&cached;
	paper_panel(8,6,width,20);print_n(16,12,C_INK,clean,(width-16)/CW);g_fb=previous;
	data_cache_hit_writeback_invalidate(cached.buffer,(unsigned long)cached.stride*cached.height);
	rdpq_attach(frame,NULL);rdpq_mode_push();rdpq_set_mode_copy(false);
	int px=8*cached.width/SCREEN_W,py=6*cached.height/SCREEN_H;
	int pw=(8+width)*cached.width/SCREEN_W-px,ph=26*cached.height/SCREEN_H-py;
	rdpq_tex_blit(&cached,px,py,&(rdpq_blitparms_t){.s0=px,.t0=py,.width=pw,.height=ph});
	rdpq_mode_pop();rdpq_detach_wait();
}
void n64_ui_text_speed(unsigned speed) {g_message_speed=speed<5?speed:2;}

#if ADVENTURE_DIAGNOSTIC >= 2
/* These checks deliberately use the production CPU renderer and display
 * queue. The checksum catches framebuffer corruption; watching the held
 * phases in ares additionally checks the VI output, which a raw framebuffer
 * checksum cannot observe (notably the hidden coverage/AA path). */
void n64_ui_video_probe(unsigned phase,unsigned frames)
{
	static const char *const names[]={"dialogue","duel","card"};
	assertf(phase<3 && frames>=9,"Invalid video test phase");
	g_denied=0;g_thinking=0;g_dmg.timer=0;
	surface_t *buffers[3]={0};unsigned count=0;
	uint32_t expected=0;
	debugf("VIDEO PHASE %s frames=%u\n",names[phase],frames);
	for(unsigned f=0;f<frames;f++) {
		begin_frame();
		if(phase==N64_VIDEO_DIALOGUE) {
			print(LEFT,12,C_HI,"Dr. Mason");
			static const char *const lines[]={
				"Evolve Goldeen into Seaking.",
				"Attach Psychic Energy to Seaking.",
				"Choose Attack, then Waterfall.",
			};
			for(unsigned i=0;i<3;i++)print(LEFT,40+i*20,C_TEXT,lines[i]);
			print(LEFT,218,C_DIM,"A/B: close");
		} else if(phase==N64_VIDEO_DUEL) {
			draw_board(0,-1);draw_textbox_hint("A: choose  START: card");
		} else {
			draw_card_page(SEAKING,TCG_PLAYER,-1,NULL);
			print(LEFT,218,C_DIM,"A/B: close");
		}
		end_frame();
		surface_t *frame=g_last_ui_frame;
		unsigned b=0;while(b<count&&buffers[b]!=frame)b++;
		if(b==count){assertf(count<3,"Video test expected three buffers");buffers[count++]=frame;}
		const uint16_t *pixels=UncachedAddr(frame->buffer);
		uint32_t hash=2166136261u;
		for(unsigned y=0;y<frame->height;y++)for(unsigned x=0;x<frame->width;x++)
			hash=(hash^pixels[y*(frame->stride/2)+x])*16777619u;
		if(!f)expected=hash;
		assertf(hash==expected,"Video %s changed at frame%u: %08lx != %08lx",names[phase],f,(unsigned long)hash,(unsigned long)expected);
	}
	assertf(count==3,"Video phase did not rotate all three buffers");
	debugf("VIDEO PASS %s frames=%u buffers=%u hash=%08lx\n",names[phase],frames,count,(unsigned long)expected);
}
#endif

#if ADVENTURE_DIAGNOSTIC == 9 || ADVENTURE_DIAGNOSTIC == 10
/* Deliberately constructed UI stress states; this ROM never submits them to
 * the duel engine. All card names, pictures and rules are the real database. */
static void polish_fixture(bool energy_stress)
{
 static const uint8_t pokemon[6]={VENUSAUR_LV64,CHANSEY,ALAKAZAM,BLASTOISE,RAICHU_LV40,SCYTHER};
 static const uint8_t hand[14]={GRASS_ENERGY,DOUBLE_COLORLESS_ENERGY,COMPUTER_SEARCH,VENUSAUR_LV64,
  ALAKAZAM,CHANSEY,PLUSPOWER,DEFENDER,POKEMON_BREEDER,DEVOLUTION_SPRAY,ENERGY_REMOVAL,
  SUPER_ENERGY_RETRIEVAL,MYSTERIOUS_FOSSIL,IMPOSTER_PROFESSOR_OAK};
 memset(gWRAM,0,WRAM_SIZE);memset(gHRAM,0,HRAM_SIZE);
 for(unsigned side=0;side<2;side++) {
  uint8_t duelist=side?TCG_OPPONENT:TCG_PLAYER;
  uint8_t *vars=tcg_duelvars(duelist),*deck=side?wOpponentDeck:wPlayerDeck;
  for(unsigned i=0;i<DECK_SIZE;i++){deck[i]=(uint8_t)(GRASS_ENERGY+i%6);vars[DUELVARS_CARD_LOCATIONS+i]=CARD_LOCATION_DECK;}
  vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]=6;
  for(unsigned loc=0;loc<6;loc++) {
   deck[loc]=pokemon[loc];vars[DUELVARS_ARENA_CARD+loc]=(uint8_t)loc;
   vars[DUELVARS_ARENA_CARD_HP+loc]=loc?card_data(deck[loc])->HP:10;
   vars[DUELVARS_CARD_LOCATIONS+loc]=(uint8_t)(CARD_LOCATION_ARENA+loc);
  }
  if(side)deck[0]=SEAKING;
  vars[DUELVARS_ARENA_CARD_STATUS]=side?(PARALYZED|POISONED):(CONFUSED|DOUBLE_POISONED);
  vars[DUELVARS_ARENA_CARD_ATTACHED_PLUSPOWER]=1;vars[DUELVARS_ARENA_CARD_ATTACHED_DEFENDER]=1;
  if(energy_stress) {
   /* Six Pokemon, 53 attached basic Energy, one untaken Prize: all 60
      physical cards accounted for. The overflowing icon row must count. */
   vars[DUELVARS_PRIZES]=1;vars[DUELVARS_PRIZE_CARDS]=59;
   vars[DUELVARS_CARD_LOCATIONS+59]=CARD_LOCATION_PRIZE;
   for(unsigned i=6;i<59;i++)vars[DUELVARS_CARD_LOCATIONS+i]=CARD_LOCATION_ARENA;
   vars[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]=60;
  } else {
   vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND]=14;
   for(unsigned i=0;i<14;i++){deck[6+i]=hand[i];vars[DUELVARS_HAND+i]=(uint8_t)(6+i);vars[DUELVARS_CARD_LOCATIONS+6+i]=CARD_LOCATION_HAND;}
   for(unsigned i=20;i<44;i++)vars[DUELVARS_CARD_LOCATIONS+i]=(uint8_t)(CARD_LOCATION_ARENA+(i<34?0:1+(i-34)/2));
   vars[DUELVARS_PRIZES]=0x2b;
   for(unsigned i=0;i<6;i++){vars[DUELVARS_PRIZE_CARDS+i]=(uint8_t)(44+i);vars[DUELVARS_CARD_LOCATIONS+44+i]=CARD_LOCATION_PRIZE;}
   vars[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK]=50;
  }
 }
}

static void polish_capture(const char *name)
{
 surface_t *frame=g_last_ui_frame;const uint16_t *pixels=UncachedAddr(frame->buffer);
 debugf("FRAMEBUFFER duel-ui-%s %u %u\n",name,frame->width,frame->height);
 for(unsigned y=0;y<frame->height;y++)for(unsigned x=0;x<frame->width;x+=32) {
  char text[129];unsigned count=frame->width-x;if(count>32)count=32;
  for(unsigned i=0;i<count;i++)snprintf(text+i*4,5,"%04x",pixels[y*(frame->stride/2)+x+i]);
  debugf("PIXELS %u %u %s\n",x,y,text);
 }
 debugf("FRAMEBUFFER END\n");
}

void n64_ui_polish_preview(void)
{
 static const char *const stages[]={"board","energy-overflow","actions","hand-pokemon","hand-trainer",
  "chosen-card","pokemon-rules-1","pokemon-rules-2","pokemon-rules-3","trainer-rules",
  "prize","result","start"};
 static uint8_t memory[WRAM_SIZE+HRAM_SIZE];
 memcpy(memory,gWRAM,WRAM_SIZE);memcpy(memory+WRAM_SIZE,gHRAM,HRAM_SIZE);
 assertf(verify_card_layout(),"Card rule reflow failed");
 copy_str(g_name[0],sizeof(g_name[0]),"MARK");copy_str(g_name[1],sizeof(g_name[1]),"SAM");
 g_mode=MODE_VS_AI;g_turn_holder=TCG_PLAYER;g_turn=5;g_asking=0;
 g_denied=0;g_thinking=0;g_dmg.timer=0;g_result=-1;
 log_clear();log_message("Mark's Turn.");
 for(unsigned stage=0;stage<sizeof(stages)/sizeof(stages[0]);stage++) {
  polish_fixture(stage==1);g_result=-1;
  debugf("DUEL UI %s BEGIN\n",stages[stage]);
  int top=0;
  for(unsigned frame=0;frame<24;frame++) {
   begin_frame();
   if(stage==0||stage==1) {draw_board(0,-1);draw_textbox_hint("A: choose  START: card");}
   else if(stage==2)draw_duel_choices(TCG_PLAYER,0);
   else if(stage==3||stage==4) {
    const uint8_t *vars=tcg_duelvars(TCG_PLAYER);
    draw_card_list_screen("Mark's Hand",NULL,vars+DUELVARS_HAND,14,TCG_PLAYER,stage==3?3:6,&top);
    draw_textbox_hint("A: select  B: back  START: info");
   } else if(stage==5) {
    draw_card_page(COMPUTER_SEARCH,TCG_PLAYER,8,NULL);
    chosen_corners(RIGHT-69,BOARD_Y+3,70,54);
    static const char *const labels[]={"Play","Check"};
    draw_choice_popup(RIGHT-100,BOARD_Y+130,NULL,labels,2,0);
    draw_textbox_hint("A: choose  B: back  START: card");
   } else if(stage>=6&&stage<=8) {
    draw_card_page_at(VENUSAUR_LV64,TCG_PLAYER,-1,NULL,(int)stage-6);
    draw_textbox_hint("L/R: page  B: back");
   } else if(stage==9) {
    draw_card_page(PLUSPOWER,TCG_PLAYER,-1,NULL);draw_textbox_hint("B: back");
   } else if(stage==10)draw_prize_choices(0x2b,1);
   else {
    draw_board(0,-1);g_result=0;draw_match_plaque(stage==11);
    draw_textbox_hint("A: continue");
   }
   end_frame();if(frame==4)polish_capture(stages[stage]);
  }
  heap_stats_t stats;sys_get_heap_stats(&stats);
  debugf("DUEL UI HEAP stage=%s total=%d used=%d free=%d\n",stages[stage],stats.total,stats.used,stats.total-stats.used);
 }
 memcpy(gWRAM,memory,WRAM_SIZE);memcpy(gHRAM,memory+WRAM_SIZE,HRAM_SIZE);
 g_result=-1;g_turn_holder=0;g_asking=0;log_clear();
 debugf("DUEL UI PREVIEW PASS\n");
}
#endif

void n64_message(const char *title,const char *text)
{
	static char clean[8192], lines[256][COLS+1];
	sanitize(text,clean,sizeof(clean));
	int n=wrap(clean,COLS,lines,256),page=0,visible=0;
	for (;;) {
		int characters=0;
		for(int i=0;i<17&&page+i<n;i++)characters+=(int)strlen(lines[page+i]);
		static const int characters_per_frame[]={1,2,3,6,COLS*17};
		visible+=characters_per_frame[g_message_speed];
		if(visible>characters)visible=characters;
		begin_frame();
		paper_panel(8,6,304,228);fill(LEFT,27,RIGHT-LEFT,1,C_PAPERSHADE);
		print_n(LEFT,12,C_INK,title?title:"",COLS);
		int remaining=visible;
		for(int i=0;i<17 && page+i<n;i++){int length=(int)strlen(lines[page+i]);print_n(LEFT,32+i*LH,C_INK,lines[page+i],remaining<length?remaining:length);remaining-=length;if(remaining<0)remaining=0;}
		print(LEFT,218,C_INK,page+17<n?"A: next   B: back":"A/B: close");
		end_frame_poll();
		Keys k=keys(1);
		if(k.a || k.b) {
			if(visible<characters){visible=characters;continue;}
			if(page+17>=n) return;
			page+=17;visible=0;
		}
	}
}

int n64_menu(const char *title,const char *const *items,int count,int initial,bool cancel)
{
	if(count<=0) return TCG_CANCEL;
	int cur=initial>=0 && initial<count?initial:0;
	for (;;) {
		begin_frame();
		paper_panel(8,6,304,228);fill(LEFT,27,RIGHT-LEFT,1,C_PAPERSHADE);
		print_n(LEFT,12,C_INK,title?title:"",COLS);
		int top=cur<14?0:cur-13;
		for(int i=top;i<count && i<top+15;i++) {
			char clean[256];sanitize(items[i],clean,sizeof(clean));
			int y=35+(i-top)*11;
			if(i==cur) fill(LEFT-4,y-1,RIGHT-LEFT+8,11,C_STORYSEL);
			print_n(LEFT,y,i==cur?C_TEXT:C_INK,clean,COLS);
		}
		print(LEFT,216,C_INK,cancel?"A: select   B: back":"A: select");
		end_frame_poll();
		Keys k=keys(1);
		if(list_nav(k,count,&cur))adventure_audio_event(1,SFX_CURSOR);
		if(k.a){adventure_audio_event(1,SFX_CONFIRM);return cur;}
		if(cancel && k.b){adventure_audio_event(1,SFX_CANCEL);return TCG_CANCEL;}
	}
}

void n64_card_detail(unsigned id)
{
	bool owned=!g_input_service;if(owned)input_begin();
	view_card((uint8_t)id,TCG_PLAYER,-1,1);
	if(owned)input_end();
}

int n64_duel_play(const TcgDuelSetup *setup,const uint8_t *saved)
{
	bool save_enabled=g_save_ok;
	set_mode(setup->duelist[1].kind==TCG_AI?
		(setup->duelist[1].deck_id==TCG_SAMS_PRACTICE_DECK_ID?MODE_PRACTICE:MODE_VS_AI):MODE_HOTSEAT);
	if(setup->duelist[1].kind!=TCG_AI)g_save_ok=false;
	copy_str(g_name[0],sizeof(g_name[0]),setup->duelist[0].name?setup->duelist[0].name:"MARK");
	copy_str(g_name[1],sizeof(g_name[1]),setup->duelist[1].name?setup->duelist[1].name:"OPPONENT");
	g_rng=(uint32_t)get_ticks()|1;
	reset_duel_ui();
	g_save_dirty=false;
	g_save_mode_written=false;
	g_duel_decks[0]=setup->duelist[0].deck_id;
	g_duel_decks[1]=setup->duelist[1].deck_id;
	TcgHost host={on_event,on_request,on_save,NULL};
	tcg_set_host(&host);
	input_begin();
	int result=saved?tcg_continue_duel(setup,saved,TCG_SAVE_SIZE):tcg_run_duel(setup);
	input_end();
	g_save_ok=save_enabled;
	/* The adventure commits the resumed story before deleting this duel save. */
	return result;
}
#else
int main(void)
{
	debug_init_isviewer();
	debug_init_usblog();
	resolution_t resolution=RESOLUTION_320x240;
#ifdef TCGN64_HOST_TEST
	if(getenv("MOCK_HIGH_RES"))resolution=(resolution_t){480,360,0};
#endif
	display_init(resolution, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
	for (int i = 0; i < NUM_COLORS; i++)
		g_col[i] = graphics_make_color(kRGB[i][0], kRGB[i][1], kRGB[i][2], 255);
	joypad_init();
	dfs_init(DFS_DEFAULT_LOCATION);
#ifdef TCGN64_HOST_TEST
	if(getenv("TCGN64_LAYOUT_TEST"))return verify_card_layout()?0:1;
#endif
	input_begin();
	load_buildinfo();
	init_decks();
	g_save_ok = save_store_open(NULL);
	debugf("EEPROM %s\n", g_save_ok ? "ok" : "missing (no saves)");

	Setup s = { MODE_VS_AI, { 0, 1 }, 6 };
	load_settings(&s);
	TcgHost host = { on_event, on_request, on_save, NULL };
	tcg_set_host(&host);
	static uint8_t saved[TCG_SAVE_SIZE];
	for (;;) {
		TcgSavedDuel info;
		int n = g_save_ok ? save_store_read(SAVE_STORE_DUEL, saved, sizeof(saved)) : -1;
		bool have_save = n == TCG_SAVE_SIZE && tcg_saved_duel_info(saved, (uint32_t)n, &info) == 0 &&
		                 info.duel_type != DUELTYPE_LINK;
		int saved_mode = have_save ? saved_duel_mode(&info) : -1;
		bool cont = title_screen(&s, have_save ? &info : NULL, saved_mode);
		uint32_t seed = (uint32_t)get_ticks();
		g_rng = seed | 1;
		reset_duel_ui();
		TcgDuelSetup setup;
		memset(&setup, 0, sizeof(setup));
		int result;
		if (cont) {
			/* decks, prizes, the duelists' types, the turn and the RNG come from the save */
			set_mode(saved_mode);
			setup.duelist[0].name = g_name[0];
			setup.duelist[1].name = g_name[1];
			memcpy(g_save_img, saved, sizeof(g_save_img));
			g_save_dirty = false;
			g_save_mode_written = true; /* "duelmode" is already there */
			debugf("CONTINUE mode=%d turn=%d prizes=%d/%d\n", saved_mode, info.turns + 1, info.prizes_left[0],
			       info.prizes_left[1]);
			result = tcg_continue_duel(&setup, saved, (uint32_t)n);
		} else {
			store_settings(&s);
			set_mode(s.mode);
			bool practice = s.mode == MODE_PRACTICE;
			setup.prizes = practice ? 2 : (uint8_t)s.prizes;
			setup.music = MUSIC_DUEL_THEME_1;
			setup.rng[0] = (uint8_t)seed;
			setup.rng[1] = (uint8_t)(seed >> 8);
			setup.rng[2] = (uint8_t)(seed >> 16);
			setup.duelist[0].kind = TCG_HUMAN;
			/* cards all zero: the engine loads the deck (the practice duel always uses PRACTICE_PLAYER_DECK) */
			setup.duelist[0].deck_id = practice ? PRACTICE_PLAYER_DECK_ID : g_decks[s.deck[0]];
			setup.duelist[0].name = g_name[0];
			setup.duelist[1].kind = g_kind[1] == PK_AI ? TCG_AI : TCG_HUMAN;
			setup.duelist[1].deck_id = practice ? TCG_SAMS_PRACTICE_DECK_ID : g_decks[s.deck[1]];
			setup.duelist[1].name = g_name[1];
			g_duel_decks[0] = setup.duelist[0].deck_id;
			g_duel_decks[1] = setup.duelist[1].deck_id;
			g_save_dirty = false;
			g_save_mode_written = false;
			debugf("DUEL mode=%d deck1=%d deck2=%d prizes=%d seed=%08lx\n", s.mode, setup.duelist[0].deck_id,
			       setup.duelist[1].deck_id, setup.prizes, (unsigned long)seed);
			result = tcg_run_duel(&setup);
		}
		save_discard(); /* the original clears the saved duel once the duel is over */
		if (result == TCG_CONTINUE_BROKEN) {
			debugf("CONTINUE broken save\n");
			continue;
		}
		result_screen(result);
	}
}
#endif
