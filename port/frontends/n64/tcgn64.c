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
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libdragon.h>

#include "tcg.h"
#include "common.h"
#include "gen/decks.h"
#include "gen/text.h"

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

enum {
	C_BG, C_MID, C_PANEL, C_PANEL2, C_BORDER, C_TEXT, C_DIM, C_HI, C_SEL, C_RED, C_GREEN,
	C_BLACK, C_HPBG, C_CARDBACK, C_FATAL,
	C_E0,                /* energy colors: FIRE GRASS LIGHTNING WATER FIGHTING PSYCHIC COLORLESS */
	C_T0 = C_E0 + 7,     /* darker type colors for card backgrounds */
	NUM_COLORS = C_T0 + 7
};

static const uint8_t kRGB[NUM_COLORS][3] = {
	[C_BG] = { 0x10, 0x30, 0x28 },     [C_MID] = { 0x30, 0x60, 0x50 },
	[C_PANEL] = { 0x18, 0x20, 0x48 },  [C_PANEL2] = { 0x28, 0x30, 0x60 },
	[C_BORDER] = { 0xb0, 0xb8, 0xd0 }, [C_TEXT] = { 0xff, 0xff, 0xff },
	[C_DIM] = { 0x98, 0xa0, 0xa8 },    [C_HI] = { 0xff, 0xe0, 0x40 },
	[C_SEL] = { 0x50, 0x68, 0xc0 },    [C_RED] = { 0xf0, 0x40, 0x30 },
	[C_GREEN] = { 0x40, 0xd0, 0x50 },  [C_BLACK] = { 0x00, 0x00, 0x00 },
	[C_HPBG] = { 0x40, 0x10, 0x10 },   [C_CARDBACK] = { 0x30, 0x50, 0xc0 },
	[C_FATAL] = { 0x80, 0x10, 0x10 },
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

enum { MODE_VS_AI, MODE_VS_BOT, MODE_HOTSEAT, NUM_MODES };
enum { PK_HUMAN, PK_AI, PK_BOT };       /* who answers a duelist's requests */

static uint32_t g_col[NUM_COLORS];
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

/* things events asked us to show */
static int g_screen;                    /* last TCG_SCREEN_* */
static uint8_t g_screen_arg;            /* its arg[2] */
static uint8_t g_boxmsg = 0xff;
static struct { bool on; uint8_t id, deck, owner, page; } g_overlay;  /* TCG_EV_CARD */
static struct { uint8_t res[16]; int n, total; } g_coin;              /* TCG_EV_COIN */
static struct { int timer; uint8_t side, loc; unsigned amount; } g_dmg;
static uint8_t g_thinking;
static uint8_t g_asking;                /* duelist answering the current request (0: none) */
static int g_denied;                    /* frames left of the "invalid choice" flash */
static uint8_t g_turn_holder;
static int g_turn;

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
		if (*p == '\n')
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

static void fill(int x, int y, int w, int h, int c)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > SCREEN_W) w = SCREEN_W - x;
	if (y + h > SCREEN_H) h = SCREEN_H - y;
	if (w <= 0 || h <= 0)
		return;
	graphics_draw_box(g_fb, x, y, w, h, g_col[c]);
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
}

/* draw at most `max` characters of one line (clipped to the screen) */
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
	graphics_set_color(g_col[c], 0);
	graphics_draw_text(g_fb, x, y, buf);
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

/* 64x48 card picture (or a placeholder with the type letter) */
static void card_art(int x, int y, uint8_t id)
{
#ifdef TCGN64_CARD_ART
	sprite_t *s = art_get(id);
	if (s) {
		graphics_draw_sprite(g_fb, x, y, s);
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

/* ---- board */

static void draw_side(uint8_t duelist, bool top, int hl_loc)
{
	const uint8_t *v = tcg_duelvars(duelist);
	int side = side_of(duelist);
	char buf[64], name[24];
	/* top half y 8..89 (bar, bench, active), bottom half y 99..189 (active, bench, bar) */
	int bar_y = top ? BOARD_Y : BOARD_Y + 173;
	int bench_y = top ? BOARD_Y + 10 : BOARD_Y + 141;
	int act_y = top ? BOARD_Y + 44 : BOARD_Y + 93;
	int act_art_y = top ? BOARD_Y + 40 : BOARD_Y + 91;

	/* info bar */
	bool turn = g_turn_holder == duelist;
	fill(LEFT - 2, bar_y - 1, RIGHT - LEFT + 4, 9, turn ? C_PANEL2 : C_BG);
	printf_at(LEFT, bar_y, turn ? C_HI : C_TEXT, "%-7.7s", g_name[side]);
	int deck = DECK_SIZE - v[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK];
	if (deck < 0)
		deck = 0;
	printf_at(LEFT + 8 * CW, bar_y, C_TEXT, "Hand %-2d Deck %-2d Disc %-2d",
	          v[DUELVARS_NUMBER_OF_CARDS_IN_HAND], deck, v[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE]);
	int pz = prizes_left(duelist);
	print(LEFT + 31 * CW, bar_y, C_TEXT, "P");
	for (int i = 0; i < 6; i++)
		fill(LEFT + 32 * CW + i * 6, bar_y + 1, 5, 7, i < pz ? C_CARDBACK : C_MID);

	/* bench: 5 mini cards */
	for (int b = 1; b <= MAX_BENCH_POKEMON; b++) {
		int x = LEFT + (b - 1) * 58;
		Slot s;
		if (!get_slot(duelist, b, &s)) {
			rect(x, bench_y, 56, 30, C_MID);
			continue;
		}
		fill(x, bench_y, 56, 30, C_T0 + s.color);
		rect(x, bench_y, 56, 30, hl_loc == b ? C_HI : C_BORDER);
		if (hl_loc == b)
			rect(x + 1, bench_y + 1, 54, 28, C_HI);
		card_name(s.id, name, sizeof(name));
		print_n(x + 2, bench_y + 2, C_TEXT, name, 6);
		printf_at(x + 2, bench_y + 11, s.hp * 4 <= s.maxhp ? C_RED : C_TEXT, "%d/%d", s.hp, s.maxhp);
		uint8_t e[7];
		memcpy(e, s.energy, sizeof(e));
		/* compact energy line: up to 6 small squares */
		int k = 0;
		for (int c = 0; c < 7; c++)
			for (int i = 0; i < e[c] && k < 9; i++, k++)
				fill(x + 2 + k * 6, bench_y + 21, 5, 6, C_E0 + c);
		if (g_dmg.timer && side_of(g_dmg.side) == side && g_dmg.loc == b) {
			fill(x + 20, bench_y + 9, 34, 12, C_RED);
			printf_at(x + 22, bench_y + 11, C_TEXT, "-%u", g_dmg.amount);
		}
	}

	/* active Pokemon */
	Slot s;
	if (!get_slot(duelist, 0, &s)) {
		print(LEFT, act_y + LH, C_DIM, "(no active Pokemon)");
		return;
	}
	const struct CardData *c = card_data(s.id);
	if (hl_loc == 0) {
		fill(LEFT - 4, act_y - 3, 222, 44, C_SEL);
		rect(LEFT - 4, act_y - 3, 222, 44, C_HI);
	}
	card_name(s.id, name, sizeof(name));
	if (is_pokemon(c) && c->Level)
		snprintf(buf, sizeof(buf), "%s Lv%d", name, c->Level);
	else
		copy_str(buf, sizeof(buf), name);
	energy_icon(LEFT, act_y, s.color);
	print_n(LEFT + 12, act_y, C_TEXT, buf, 25);
	hp_bar(LEFT, act_y + LH + 2, 96, s.hp, s.maxhp);
	printf_at(LEFT + 104, act_y + LH, C_TEXT, "HP %d/%d", s.hp, s.maxhp);
	int n = energy_icons(LEFT, act_y + 2 * LH, s.energy, 12);
	if (!n)
		print(LEFT, act_y + 2 * LH, C_DIM, "no energy");
	status_text(duelist, buf, sizeof(buf));
	int sx = LEFT;
	if (buf[0]) {
		fill(sx - 1, act_y + 3 * LH - 1, (int)strlen(buf) * CW + 2, 10, C_RED);
		print(sx, act_y + 3 * LH, C_TEXT, buf);
		sx += ((int)strlen(buf) + 1) * CW;
	}
	if (s.pluspower) {
		print(sx, act_y + 3 * LH, C_HI, "PlusPower");
		sx += 10 * CW;
	}
	if (s.defender)
		print(sx, act_y + 3 * LH, C_HI, "Defender");
	card_art(RIGHT - 64, act_art_y, s.id);
	if (g_dmg.timer && side_of(g_dmg.side) == side && g_dmg.loc == 0) {
		fill(RIGHT - 52, act_art_y + 16, 40, 16, C_RED);
		printf_at(RIGHT - 48, act_art_y + 20, C_TEXT, "-%u", g_dmg.amount);
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

/* the board: opponent (TCG_OPPONENT) on top, player at the bottom */
static void draw_board(uint8_t hl_duelist, int hl_loc)
{
	fill(LEFT - 4, SLOT_INFO_Y + 4, RIGHT - LEFT + 8, 1, C_MID);
	draw_side(TCG_OPPONENT, true, hl_duelist == TCG_OPPONENT ? hl_loc : -1);
	draw_side(TCG_PLAYER, false, hl_duelist == TCG_PLAYER ? hl_loc : -1);
}

/* The display surfaces are uncached memory, where the CPU drawing functions
 * would do one slow uncached store per pixel: draw through the cached alias of
 * the buffer instead and write the data cache back before showing the frame. */
static surface_t *g_disp;
static surface_t g_fb_cached;

static void begin_frame(void)
{
	g_disp = display_get();
	g_fb_cached = *g_disp;
	g_fb_cached.buffer = CachedAddr(g_disp->buffer);
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
	data_cache_hit_writeback_invalidate(g_fb->buffer, (unsigned long)g_fb->stride * g_fb->height);
	display_show(g_disp);
	g_fb = NULL;
	g_disp = NULL;
	g_frame++;
	if (g_dmg.timer)
		g_dmg.timer--;
}

/* ------------------------------------------------------------------ input */

typedef struct Keys { bool a, b, z, start, l, r, up, down, left, right; } Keys;

static Keys g_keys[2];
static uint8_t g_rep[2][4];

static void input_poll(void)
{
	joypad_poll();
	for (int p = 0; p < 2; p++) {
		joypad_port_t port = p ? JOYPAD_PORT_2 : JOYPAD_PORT_1;
		Keys k = { 0 };
		if (!joypad_is_connected(port)) {
			g_keys[p] = k;
			continue;
		}
		joypad_buttons_t pr = joypad_get_buttons_pressed(port);
		joypad_buttons_t hd = joypad_get_buttons_held(port);
		joypad_inputs_t in = joypad_get_inputs(port);
		k.a = pr.a;
		k.b = pr.b;
		k.z = pr.z;
		k.start = pr.start;
		k.l = pr.l || pr.c_left;
		k.r = pr.r || pr.c_right;
		bool held[4] = {
			hd.d_up || hd.c_up || in.stick_y > 40,
			hd.d_down || hd.c_down || in.stick_y < -40,
			hd.d_left || in.stick_x < -40,
			hd.d_right || in.stick_x > 40,
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
		k.up = fire[0];
		k.down = fire[1];
		k.left = fire[2];
		k.right = fire[3];
		g_keys[p] = k;
	}
}

/* keys of the controllers in mask (bit 0 = controller 1, bit 1 = controller 2) */
static Keys keys(int mask)
{
	Keys k = { 0 };
	/* controller 2 unplugged (or never there): controller 1 takes over */
	if (mask == 2 && !joypad_is_connected(JOYPAD_PORT_2))
		mask = 1;
	for (int p = 0; p < 2; p++) {
		if (!(mask & (1 << p)))
			continue;
		const Keys *q = &g_keys[p];
		k.a |= q->a; k.b |= q->b; k.z |= q->z; k.start |= q->start; k.l |= q->l; k.r |= q->r;
		k.up |= q->up; k.down |= q->down; k.left |= q->left; k.right |= q->right;
	}
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

/* render n frames of the board (pacing for events); A/B skips */
static void show_frames(int n)
{
	for (int i = 0; i < n; i++) {
		begin_frame();
		draw_board(0, -1);
		draw_textbox();
		if (g_coin.n && g_screen == TCG_SCREEN_COIN_TOSS) {
			int r = g_coin.res[g_coin.n - 1];
			fill(LEFT + 80, BOARD_Y + 60, 130, 40, C_PANEL2);
			rect(LEFT + 80, BOARD_Y + 60, 130, 40, C_HI);
			printf_at(LEFT + 88, BOARD_Y + 66, C_TEXT, "Coin %d/%d", g_coin.n, g_coin.total ? g_coin.total : 1);
			print(LEFT + 88, BOARD_Y + 80, r ? C_HI : C_DIM, r ? "HEADS" : "TAILS");
		}
		end_frame_poll();
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
}

/* ------------------------------------------------------------------ card page */

static void draw_card_page(uint8_t id, uint8_t owner, int deck, const char *title)
{
	const struct CardData *c = card_data(id);
	char buf[96], name[32];
	char lines[12][COLS + 1];
	panel(LEFT - 6, BOARD_Y, RIGHT - LEFT + 12, BOARD_H);
	int x = LEFT, y = BOARD_Y + 4;
	if (title) {
		print(x, y, C_HI, title);
		y += LH;
	}
	card_name(id, name, sizeof(name));
	card_art(RIGHT - 66, y + 1, id);
	if (is_pokemon(c)) {
		int hp = c->HP;
		/* in play: show the current HP */
		if (deck >= 0 && deck < DECK_SIZE) {
			for (int loc = 0; loc < num_in_play(owner); loc++) {
				Slot s;
				if (get_slot(owner, loc, &s) && s.deck == deck)
					hp = s.hp;
			}
		}
		snprintf(buf, sizeof(buf), "%s Lv%d", name, c->Level);
		print(x, y, C_TEXT, buf);
		y += LH;
		energy_icon(x, y, c->Type & 7);
		printf_at(x + 12, y, C_TEXT, "%s  HP %d/%d", kTypeName[c->Type & 7], hp, c->HP);
		y += LH;
		static const char *const stages[] = { "Basic", "Stage 1", "Stage 2", "Stage 2" };
		if (c->Stage != BASIC) {
			char pre[32];
			text_of(rd16(c->PreEvoName), pre, sizeof(pre));
			snprintf(buf, sizeof(buf), "%s (from %s)", stages[c->Stage & 3], pre);
		} else {
			snprintf(buf, sizeof(buf), "%s Pokemon", stages[0]);
		}
		print_n(x, y, C_DIM, buf, 28);
		y += LH;
		/* weakness / resistance / retreat */
		int wx = x;
		print(wx, y, C_DIM, "Weak");
		wx += 5 * CW;
		for (int k = 0; k < 6; k++)
			if (c->Weakness & (0x80 >> k)) {
				energy_icon(wx, y, k);
				wx += 9;
			}
		wx += 6;
		print(wx, y, C_DIM, "Res");
		wx += 4 * CW;
		for (int k = 0; k < 6; k++)
			if (c->Resistance & (0x80 >> k)) {
				energy_icon(wx, y, k);
				wx += 9;
			}
		wx += 6;
		printf_at(wx, y, C_DIM, "Retreat %d", c->RetreatCost);
		y += LH + 4;
		if (y < BOARD_Y + 58)
			y = BOARD_Y + 58; /* below the picture */
		const struct AtkData *atks[2] = { &c->Atk1, &c->Atk2 };
		for (int a = 0; a < 2; a++) {
			const struct AtkData *atk = atks[a];
			if (!rd16(atk->Name))
				continue;
			text_of(rd16(atk->Name), name, sizeof(name));
			if (atk->Category == POKEMON_POWER) {
				printf_at(x, y, C_HI, "PKMN Power: %s", name);
			} else {
				int w = cost_icons(x, y, atk);
				print(x + w + 4, y, C_HI, name);
				if (atk->Damage) {
					static const char suffix[] = { 0, '+', '-', 'x' };
					char d[8];
					snprintf(d, sizeof(d), "%d%c", atk->Damage, suffix[atk->Category & 3]);
					print(RIGHT - (int)strlen(d) * CW, y, C_TEXT, d);
				}
			}
			y += LH;
			char desc[400];
			attack_description(atk, desc, sizeof(desc));
			int n = wrap(desc, COLS - 1, lines, 12);
			for (int i = 0; i < n && y < BOARD_Y + BOARD_H - 9; i++, y += 9)
				print(x + CW, y, C_TEXT, lines[i]);
			y += 3;
		}
	} else {
		print(x, y, C_TEXT, name);
		y += LH;
		if (is_energy(c)) {
			energy_icon(x, y, card_color(c));
			print(x + 12, y, C_DIM, c->Type == TYPE_ENERGY_DOUBLE_COLORLESS ? "Special Energy" : "Basic Energy");
		} else {
			print(x, y, C_DIM, "Trainer");
		}
		y += 3 * LH;
		char desc[400];
		description(rd16(c->NonPokemonDescription), desc, sizeof(desc));
		char dl[14][COLS + 1];
		int n = wrap(desc, COLS, dl, 14);
		for (int i = 0; i < n; i++, y += 9)
			print(x, y, C_TEXT, dl[i]);
	}
}

/* view a card until A/B/START (frontend only, the engine is not involved) */
static void view_card(uint8_t id, uint8_t owner, int deck, int ports)
{
	for (;;) {
		begin_frame();
		draw_card_page(id, owner, deck, NULL);
		draw_textbox_hint("B: back");
		end_frame_poll();
		Keys k = keys(ports);
		if (k.a || k.b || k.start)
			return;
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

static void draw_list(int x, int y, int w, int rows, const char *title, int n, int cur, int *top,
                      LabelFn label, void *ctx)
{
	panel(x, y, w, rows * LH + LH + 8);
	if (title)
		print_n(x + 6, y + 4, C_HI, title, (w - 12) / CW);
	if (cur < *top)
		*top = cur;
	if (cur >= *top + rows)
		*top = cur - rows + 1;
	if (*top < 0)
		*top = 0;
	int ly = y + 4 + LH;
	if (n == 0)
		print(x + 14, ly, C_DIM, "(none)");
	for (int i = *top; i < n && i < *top + rows; i++, ly += LH) {
		char buf[64];
		int icon = -1;
		int c = label(ctx, i, buf, sizeof(buf), &icon);
		if (i == cur) {
			fill(x + 2, ly - 1, w - 4, LH, C_SEL);
			print(x + 4, ly, C_HI, ">");
		}
		int tx = x + 14;
		if (icon >= 0) {
			energy_icon(tx, ly, icon);
			tx += 11;
		}
		print_n(tx, ly, c, buf, (x + w - 4 - tx) / CW);
	}
	if (*top > 0)
		print(x + w - 12, y + 4 + LH, C_DIM, "^");
	if (*top + rows < n)
		print(x + w - 12, y + 4 + rows * LH, C_DIM, "v");
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
		snprintf(out, (size_t)n, "%-14.14s Lv%d", name, c->Level);
	else if (is_energy(c))
		copy_str(out, (size_t)n, name);
	else {
		*icon = -1;
		snprintf(out, (size_t)n, "T %s", name);
	}
	return C_TEXT;
}

/* short summary of a card in a side panel (list details) */
static void draw_card_summary(int x, int y, int w, uint8_t id)
{
	const struct CardData *c = card_data(id);
	char name[32], buf[64];
	card_art(x + (w - 64) / 2, y + 2, id);
	y += 54;
	card_name(id, name, sizeof(name));
	print_n(x + 2, y, C_TEXT, name, w / CW);
	y += LH;
	if (is_pokemon(c)) {
		snprintf(buf, sizeof(buf), "HP%d %s", c->HP, c->Stage ? (c->Stage == 1 ? "Stage1" : "Stage2") : "Basic");
		print_n(x + 2, y, C_DIM, buf, w / CW);
		y += LH + 2;
		const struct AtkData *atks[2] = { &c->Atk1, &c->Atk2 };
		for (int a = 0; a < 2; a++) {
			if (!rd16(atks[a]->Name))
				continue;
			text_of(rd16(atks[a]->Name), name, sizeof(name));
			if (atks[a]->Category == POKEMON_POWER) {
				print_n(x + 2, y, C_HI, "Power:", 6);
				y += 9;
			} else {
				cost_icons(x + 2, y, atks[a]);
				if (atks[a]->Damage)
					printf_at(x + w - 4 * CW, y, C_TEXT, "%3d", atks[a]->Damage);
				y += 9;
			}
			print_n(x + 2, y, C_HI, name, w / CW);
			y += LH + 1;
		}
	} else {
		print_n(x + 2, y, C_DIM, is_energy(c) ? "Energy" : "Trainer", w / CW);
		y += LH + 2;
		char desc[300];
		char dl[12][COLS + 1];
		description(rd16(c->NonPokemonDescription), desc, sizeof(desc));
		int n = wrap(desc, w / CW - 1, dl, 12);
		for (int i = 0; i < n; i++, y += 9)
			print_n(x + 2, y, C_TEXT, dl[i], w / CW);
	}
}

/* full panel card list with details of the cursor card */
static void draw_card_list_screen(const char *title, const char *info, const uint8_t *items, int n,
                                  uint8_t owner, int cur, int *top)
{
	CardListCtx ctx = { items, owner, 1 };
	fill(LEFT - 6, BOARD_Y, RIGHT - LEFT + 12, BOARD_H, C_BG);
	draw_list(LEFT - 6, BOARD_Y, 196, 13, title, n, cur, top, label_card, &ctx);
	panel(LEFT + 192, BOARD_Y, RIGHT - LEFT - 186, BOARD_H);
	if (n > 0 && cur < n && items[cur] < DECK_SIZE)
		draw_card_summary(LEFT + 194, BOARD_Y + 2, RIGHT - LEFT - 190, tcg_card_id(owner, items[cur]));
	if (info && info[0]) {
		char il[3][COLS + 1];
		int k = wrap(info, 23, il, 2);
		for (int i = 0; i < k; i++)
			print(LEFT, BOARD_Y + 13 * LH + 22 + i * 9, C_DIM, il[i]);
	}
}

/* ------------------------------------------------------------------ requests */

static const char *who(uint8_t duelist) { return g_name[side_of(duelist)]; }

static int ui_ack(const TcgRequest *r, int ports)
{
	char lines[22][COLS + 1];
	int msg_lines = g_log_n - g_log_newest;
	bool long_msg = msg_lines > TBOX_LINES;
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
	int top = 0;
	debugf("ASK ack/%s %s\n", g_overlay.on ? "card" : long_msg ? "long" : list_n ? "list" :
	       g_screen == TCG_SCREEN_COIN_TOSS ? "coin" : g_screen == TCG_SCREEN_BOX_MESSAGE ? "box" : "text", who(r->duelist));
	for (;;) {
		begin_frame();
		if (g_overlay.on) {
			draw_card_page(g_overlay.id, g_overlay.owner, g_overlay.deck, NULL);
		} else if (long_msg) {
			panel(LEFT - 6, BOARD_Y, RIGHT - LEFT + 12, BOARD_H);
			for (int i = 0; i < nl; i++)
				print(LEFT, BOARD_Y + 4 + i * LH, C_TEXT, lines[i]);
		} else if (list_n) {
			int cur = hCurMenuItem < list_n ? hCurMenuItem : 0; /* the card the engine is talking about */
			draw_card_list_screen(NULL, NULL, list, list_n, r->shown, cur, &top);
		} else {
			draw_board(0, -1);
			if (g_screen == TCG_SCREEN_COIN_TOSS) {
				fill(LEFT + 80, BOARD_Y + 60, 150, 40, C_PANEL2);
				rect(LEFT + 80, BOARD_Y + 60, 150, 40, C_HI);
				if (g_coin.n == 0) {
					print(LEFT + 88, BOARD_Y + 66, C_TEXT, "Coin toss");
					print(LEFT + 88, BOARD_Y + 80, C_HI, "A: toss!");
				} else {
					printf_at(LEFT + 88, BOARD_Y + 66, C_TEXT, "Coin %d/%d", g_coin.n, g_coin.total ? g_coin.total : 1);
					for (int i = 0; i < g_coin.n && i < 12; i++) {
						fill(LEFT + 88 + i * 11, BOARD_Y + 80, 9, 9, g_coin.res[i] ? C_HI : C_DIM);
						print(LEFT + 88 + i * 11, BOARD_Y + 81, C_BLACK, g_coin.res[i] ? "H" : "T");
					}
				}
			} else if (g_screen == TCG_SCREEN_BOX_MESSAGE && g_boxmsg <= BOXMSG_COIN_TOSS) {
				static const char *const box[] = { "%s's turn", "%s's turn", "Between turns", "Decision",
					"Bench Pokemon", "Active Pokemon", "Coin toss" };
				char b[40];
				uint8_t d = g_boxmsg == BOXMSG_OPPONENTS_TURN ? TCG_OPPONENT : TCG_PLAYER;
				snprintf(b, sizeof(b), box[g_boxmsg], who(d));
				int w = (int)strlen(b) * CW + 24;
				fill((SCREEN_W - w) / 2, BOARD_Y + 74, w, 28, C_PANEL2);
				rect((SCREEN_W - w) / 2, BOARD_Y + 74, w, 28, C_HI);
				print((SCREEN_W - w) / 2 + 12, BOARD_Y + 84, C_HI, b);
			}
		}
		draw_textbox();
		if ((g_frame / 16) & 1)
			print(RIGHT - CW, TBOX_LINE(TBOX_LINES - 1), C_HI, "v");
		end_frame_poll();
		Keys k = keys(ports);
		if (k.a || k.b)
			break;
		if (k.start && !g_overlay.on && !long_msg)
			view_slot(r->shown, 0, ports);
	}
	g_overlay.on = false;
	return 0;
}

static void check_screen(int ports);

static int ui_duel_menu(const TcgRequest *r, int ports)
{
	static const char *const items[6] = { "Hand", "Attack", "Check", "PKMN Pwr", "Retreat", "Done" };
	int cur = r->cursor < 6 ? r->cursor : 0;
	for (;;) {
		begin_frame();
		draw_board(0, -1);
		panel(LEFT - 6, TBOX_Y, RIGHT - LEFT + 12, TBOX_LINES * TLH + 5);
		printf_at(LEFT, TBOX_LINE(0), C_DIM, "%s: what will you do?", who(r->duelist));
		for (int i = 0; i < 6; i++) {
			int x = LEFT + 8 + (i / 2) * 12 * CW, y = TBOX_LINE(1) + (i & 1) * TLH;
			if (i == cur) {
				fill(x - 10, y - 1, 11 * CW, TLH, C_SEL);
				print(x - 9, y, C_HI, ">");
			}
			print(x, y, i == cur ? C_HI : C_TEXT, items[i]);
		}
		print(RIGHT - 21 * CW, TBOX_LINE(3), C_HI, "START: card  Z: check");
		end_frame_poll();
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

static void draw_choice_popup(int x, int y, const char *title, const char *const *labels, int n, int cur)
{
	int w = 0;
	for (int i = 0; i < n; i++)
		if ((int)strlen(labels[i]) > w)
			w = (int)strlen(labels[i]);
	if (title && (int)strlen(title) > w)
		w = (int)strlen(title);
	if (w > 26)
		w = 26;
	int pw = w * CW + 28, ph = (n + (title ? 1 : 0)) * LH + 8;
	if (x + pw > RIGHT + 6)
		x = RIGHT + 6 - pw;
	if (y + ph > TBOX_Y - 2)
		y = TBOX_Y - 2 - ph;
	panel(x, y, pw, ph);
	int ly = y + 4;
	if (title) {
		print_n(x + 6, ly, C_DIM, title, w + 2);
		ly += LH;
	}
	for (int i = 0; i < n; i++, ly += LH) {
		if (i == cur) {
			fill(x + 2, ly - 1, pw - 4, LH, C_SEL);
			print(x + 6, ly, C_HI, ">");
		}
		print_n(x + 18, ly, i == cur ? C_HI : C_TEXT, labels[i], w);
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
		draw_choice_popup(RIGHT - 100, BOARD_Y + 130, NULL, labels, 2, cur);
		draw_textbox_hint("A: choose  B: back");
		end_frame_poll();
		Keys k = keys(ports);
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
	/* items start at the arena, or at bench 1 for the bench selection menus (cursor y offset 3) */
	int start = tcg_wram()[ADDR_wMenuCursorYOffset - WRAM_START] >= 3 ? 1 : 0;
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
		printf_at(LEFT, py + 4, C_DIM, "%s's attacks", name);
		if (have_slot) /* the attacker's energy */
			energy_icons(RIGHT - 8 * 9, py + 4, slot.energy, 8);
		const struct CardData *c = card_data(id);
		int y = py + 4 + LH;
		for (int i = 0; i < n; i++, y += LH + 2) {
			const struct AtkData *atk = pairs[i * 2 + 1] ? &c->Atk2 : &c->Atk1;
			if (i == cur)
				fill(LEFT - 4, y - 2, RIGHT - LEFT + 8, LH + 1, C_SEL);
			int w = cost_icons(LEFT + 4, y, atk);
			text_of(rd16(atk->Name), name, sizeof(name));
			bool ok = !have_slot || r->shown != g_turn_holder || can_pay(atk, &slot);
			print(LEFT + 8 + (w > 36 ? w : 36), y, i == cur ? C_HI : ok ? C_TEXT : C_DIM, name);
			if (atk->Damage) {
				static const char suffix[] = { 0, '+', '-', 'x' };
				char d[8];
				snprintf(d, sizeof(d), "%d%c", atk->Damage, suffix[atk->Category & 3]);
				print(RIGHT - 4 * CW, y, C_TEXT, d);
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
static int choose(const char *title, const char *const *labels, const int *icons, int n, int cur, bool can_cancel, int ports)
{
	if (cur < 0 || cur >= n)
		cur = 0;
	for (;;) {
		begin_frame();
		draw_board(0, -1);
		int w = 0;
		for (int i = 0; i < n; i++)
			if ((int)strlen(labels[i]) > w)
				w = (int)strlen(labels[i]);
		if (title && (int)strlen(title) > w)
			w = (int)strlen(title);
		if (w > 30)
			w = 30;
		int rows = n > 12 ? 12 : n;
		int top = cur >= rows ? cur - rows + 1 : 0;
		int pw = w * CW + 40, x = (SCREEN_W - pw) / 2, y = BOARD_Y + 20;
		if (icons) {
			panel(x, y, pw, rows * LH + LH + 8);
			print_n(x + 6, y + 4, C_HI, title ? title : "", w + 2);
			for (int i = top; i < top + rows; i++) {
				int ly = y + 4 + LH + (i - top) * LH;
				if (i == cur)
					fill(x + 2, ly - 1, pw - 4, LH, C_SEL);
				if (i == cur)
					print(x + 4, ly, C_HI, ">");
				if (icons[i] >= 0)
					energy_icon(x + 14, ly, icons[i]);
				print_n(x + 26, ly, i == cur ? C_HI : C_TEXT, labels[i], w);
			}
		} else {
			draw_list(x, y, pw, rows, title, n, cur, &top, label_text, (void *)labels);
		}
		draw_textbox_hint(can_cancel ? "A: choose  B: back" : "A: choose");
		end_frame_poll();
		Keys k = keys(ports);
		if (k.up)
			cur = cur > 0 ? cur - 1 : n - 1;
		if (k.down)
			cur = cur < n - 1 ? cur + 1 : 0;
		if (k.a)
			return cur;
		if (k.b && can_cancel)
			return TCG_CANCEL;
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

static int ui_menu_two_items(const TcgRequest *r, int ports)
{
	char a[32], b[32];
	const char *labels[2] = { a, b };
	two_labels(g_log_n ? g_log[g_log_n - 1] : "", a, b, sizeof(a), "First", "Second");
	return choose(NULL, labels, NULL, 2, r->cursor, true, ports);
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
	char a[32] = "Yes", b[32] = "No";
	if (r->arg[0])
		two_labels(r->text ? tcg_text(r->text) : (g_log_n ? g_log[g_log_n - 1] : ""), a, b, sizeof(a), "First", "Second");
	const char *labels[2] = { a, b };
	int cur = r->cursor ? 0 : 1; /* ui item 0 = yes / first item = answer 1 */
	for (;;) {
		begin_frame();
		draw_board(0, -1);
		draw_textbox_hint(r->arg[0] ? "A: choose" : "A: choose  B: no");
		draw_choice_popup(RIGHT - 80, TBOX_Y - 40, NULL, labels, 2, cur);
		end_frame_poll();
		Keys k = keys(ports);
		if (k.up || k.down || k.left || k.right)
			cur ^= 1;
		if (k.a)
			return cur == 0 ? 1 : 0;
		if (k.b && !r->arg[0])
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

static int ui_prize(const TcgRequest *r, int ports)
{
	uint8_t mask = r->arg[0];
	int cur = r->cursor < 6 ? r->cursor : 0;
	for (;;) {
		begin_frame();
		draw_board(0, -1);
		int x0 = LEFT + 70, y0 = BOARD_Y + 36;
		panel(x0 - 10, y0 - 18, 180, 128);
		print(x0, y0 - 12, C_HI, "Choose a Prize card");
		for (int i = 0; i < 6; i++) {
			int x = x0 + (i % 3) * 54, y = y0 + (i / 3) * 52;
			if (mask & (1 << i))
				fill(x, y, 44, 44, C_CARDBACK);
			else
				rect(x, y, 44, 44, C_MID);
			rect(x - 2, y - 2, 48, 48, i == cur ? C_HI : C_PANEL);
			if (i == cur)
				rect(x - 3, y - 3, 50, 50, C_HI);
		}
		draw_textbox_hint("A: take");
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

static int ui_color(const TcgRequest *r, int ports)
{
	static const int icons[6] = { FIRE, GRASS, LIGHTNING, WATER, FIGHTING, PSYCHIC };
	const char *labels[6];
	for (int i = 0; i < 6; i++)
		labels[i] = kTypeName[i];
	return choose("Choose a color", labels, icons, 6, r->cursor, true, ports);
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

static const char *const kMenuName[] = { "generic", "attacks", "play_area", "two_items", "card_list" };
static const char *const kReqName[] = { "?", "ack", "duel_menu", "card_list", "item_menu", "play_area", "menu",
	"yes_no", "energy_discard", "prize", "color", "number" };

static void on_event(void *ctx, const TcgEvent *ev)
{
	(void)ctx;
	switch (ev->type) {
	case TCG_EV_TEXT:
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
			g_coin.n = 0;
			g_coin.total = ev->arg[2];
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
		if (g_coin.n < (int)sizeof(g_coin.res))
			g_coin.res[g_coin.n++] = ev->arg[0];
		g_coin.total = ev->arg[2];
		debugf("COIN %d/%d %s\n", ev->arg[1], ev->arg[2] ? ev->arg[2] : 1, ev->arg[0] ? "heads" : "tails");
		show_frames(20);
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
		g_turn_holder = ev->arg[0];
		g_turn = ev->arg[1];
		g_thinking = 0;
		debugf("TURN %d %s\n", ev->arg[1] + 1, who(ev->arg[0]));
		show_frames(1);
		break;
	case TCG_EV_DUEL_END:
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
	int side = side_of(r->duelist);
	int ans;
	g_thinking = 0;
	if (r->type == TCG_REQ_ACK) {
		/* text boxes are always acknowledged by a human, so that they can be read */
		int ports = g_mode == MODE_HOTSEAT ? 3 : 1;
		ans = ui_ack(r, ports);
	} else if (g_kind[side] != PK_HUMAN) {
		/* the random bot, or an AI duelist reaching a human input routine (should not happen) */
		show_frames(2);
		ans = bot_answer(r);
	} else {
		int ports = ports_for(r->duelist);
		g_asking = r->duelist;
		debugf("ASK %s%s%s %s n=%d cursor=%d\n", r->type < sizeof(kReqName) / sizeof(kReqName[0]) ? kReqName[r->type] : "?",
		       r->type == TCG_REQ_MENU ? "/" : "", r->type == TCG_REQ_MENU && r->menu < 5 ? kMenuName[r->menu] : "",
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
			default: ans = ui_menu_generic(r, ports); break;
			}
			break;
		case TCG_REQ_YES_NO: ans = ui_yes_no(r, ports); break;
		case TCG_REQ_ENERGY_DISCARD: ans = ui_energy_discard(r, ports); break;
		case TCG_REQ_PRIZE: ans = ui_prize(r, ports); break;
		case TCG_REQ_COLOR: ans = ui_color(r, ports); break;
		case TCG_REQ_NUMBER: ans = ui_number(r, ports); break;
		default: ans = TCG_CANCEL; break;
		}
	}
	g_asking = 0;
	debugf("REQ %s%s%s %s n=%d -> %d\n", r->type < sizeof(kReqName) / sizeof(kReqName[0]) ? kReqName[r->type] : "?",
	       r->type == TCG_REQ_MENU ? "/" : "", r->type == TCG_REQ_MENU && r->menu < 5 ? kMenuName[r->menu] : "",
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
		r.arg[0] = 1;
		r.text = PleaseSelectTheDeckText;
		on_request(ctx, &r);
		memset(&r, 0, sizeof(r));
		r.duelist = r.shown = TCG_PLAYER;
		r.type = TCG_REQ_COLOR;
		r.num_items = 6;
		on_request(ctx, &r);
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
	}
	return on_request(ctx, menu);
}
#define on_request on_request_selftest
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

static void title_screen(Setup *s)
{
	static const char *const modes[NUM_MODES] = { "VS CPU (engine AI)", "VS random bot", "2 players (hot-seat)" };
	int row = 0;
	for (;;) {
		char name[2][32];
		deck_name(g_decks[s->deck[0]], name[0], sizeof(name[0]));
		deck_name(g_decks[s->deck[1]], name[1], sizeof(name[1]));
		begin_frame();
		fill(0, 0, SCREEN_W, 52, C_PANEL);
		print(LEFT + 20, 14, C_HI, "POKEMON TRADING CARD GAME");
		print(LEFT + 20, 28, C_TEXT, "duel engine - Nintendo 64 port");
		const char *labels[5] = { "Mode", s->mode == MODE_HOTSEAT ? "P1 deck" : "Your deck",
			s->mode == MODE_HOTSEAT ? "P2 deck" : "Opponent", "Prizes", "" };
		for (int i = 0; i < 5; i++) {
			int y = 66 + i * 16;
			if (i == row)
				fill(LEFT - 4, y - 3, RIGHT - LEFT + 8, 14, C_SEL);
			if (i < 4) {
				print(LEFT + 4, y, C_DIM, labels[i]);
				char v[48];
				if (i == 0)
					copy_str(v, sizeof(v), modes[s->mode]);
				else if (i < 3)
					copy_str(v, sizeof(v), name[i - 1]);
				else
					snprintf(v, sizeof(v), "%d", s->prizes);
				printf_at(LEFT + 11 * CW, y, i == row ? C_HI : C_TEXT, "< %s >", v);
			} else {
				print(LEFT + 12 * CW, y, i == row ? C_HI : C_TEXT, "START DUEL");
			}
		}
		int y = 150;
		if (s->mode == MODE_VS_AI && !g_have_ai) {
			print(LEFT, y, C_RED, "The AI port is not linked yet: an AI");
			print(LEFT, y + 9, C_RED, "duel stops at its first AI decision.");
		} else if (s->mode == MODE_HOTSEAT) {
			print(LEFT, y, C_DIM, "P2 plays with controller 2 (or 1).");
		} else if (s->mode == MODE_VS_BOT) {
			print(LEFT, y, C_DIM, "The bot picks random legal inputs.");
		}
		for (int i = 0; i < g_buildinfo_lines; i++)
			print(LEFT, 176 + i * 9, C_DIM, g_buildinfo[i]);
		end_frame_poll();
		Keys k = keys(3);
		if (k.up)
			row = (row + 4) % 5;
		if (k.down)
			row = (row + 1) % 5;
		int d = k.left ? -1 : k.right ? 1 : 0;
		if (d) {
			if (row == 0)
				s->mode = (s->mode + NUM_MODES + d) % NUM_MODES;
			else if (row == 1 || row == 2)
				s->deck[row - 1] = (s->deck[row - 1] + g_num_decks + d) % g_num_decks;
			else if (row == 3)
				s->prizes = s->prizes + d < 1 ? 6 : s->prizes + d > 6 ? 1 : s->prizes + d;
		}
		if (k.l && (row == 1 || row == 2))
			s->deck[row - 1] = (s->deck[row - 1] + g_num_decks - 8) % g_num_decks;
		if (k.r && (row == 1 || row == 2))
			s->deck[row - 1] = (s->deck[row - 1] + 8) % g_num_decks;
		if (k.start || (k.a && row == 4))
			return;
		if (k.a && row < 4)
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
		fill(SCREEN_W / 2 - 100, BOARD_Y + 70, 200, 40, C_PANEL2);
		rect(SCREEN_W / 2 - 100, BOARD_Y + 70, 200, 40, C_HI);
		print(SCREEN_W / 2 - (int)strlen(msg) * CW / 2, BOARD_Y + 78, C_HI, msg);
		print(SCREEN_W / 2 - 64, BOARD_Y + 94, C_DIM, "A: back to title");
		end_frame_poll();
		if (f > 30 && (keys(3).a || keys(3).start))
			return;
	}
}

/* ------------------------------------------------------------------ main */

int main(void)
{
	debug_init_isviewer();
	debug_init_usblog();
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
	for (int i = 0; i < NUM_COLORS; i++)
		g_col[i] = graphics_make_color(kRGB[i][0], kRGB[i][1], kRGB[i][2], 255);
	joypad_init();
	dfs_init(DFS_DEFAULT_LOCATION);
	load_buildinfo();
	init_decks();

	Setup s = { MODE_VS_AI, { 0, 1 }, 6 };
	TcgHost host = { on_event, on_request, NULL, NULL };
	tcg_set_host(&host);
	for (;;) {
		title_screen(&s);
		uint32_t seed = (uint32_t)get_ticks();
		g_rng = seed | 1;
		g_mode = s.mode;
		g_kind[0] = PK_HUMAN;
		g_kind[1] = s.mode == MODE_VS_AI ? PK_AI : s.mode == MODE_VS_BOT ? PK_BOT : PK_HUMAN;
		copy_str(g_name[0], sizeof(g_name[0]), s.mode == MODE_HOTSEAT ? "P1" : "PLAYER");
		copy_str(g_name[1], sizeof(g_name[1]), s.mode == MODE_HOTSEAT ? "P2" : s.mode == MODE_VS_BOT ? "BOT" : "CPU");

		TcgDuelSetup setup;
		memset(&setup, 0, sizeof(setup));
		setup.prizes = (uint8_t)s.prizes;
		setup.rng[0] = (uint8_t)seed;
		setup.rng[1] = (uint8_t)(seed >> 8);
		setup.rng[2] = (uint8_t)(seed >> 16);
		setup.duelist[0].kind = TCG_HUMAN;
		setup.duelist[0].deck_id = g_decks[s.deck[0]]; /* cards all zero: the engine expands the deck list */
		setup.duelist[0].name = g_name[0];
		setup.duelist[1].kind = s.mode == MODE_VS_AI ? TCG_AI : TCG_HUMAN;
		setup.duelist[1].deck_id = g_decks[s.deck[1]];
		setup.duelist[1].name = g_name[1];

		log_clear();
		memset(&g_overlay, 0, sizeof(g_overlay));
		memset(&g_coin, 0, sizeof(g_coin));
		memset(&g_dmg, 0, sizeof(g_dmg));
		g_turn_holder = 0;
		g_turn = 0;
		g_screen = TCG_SCREEN_NONE;
		debugf("DUEL mode=%d deck1=%d deck2=%d prizes=%d seed=%08lx\n", s.mode, setup.duelist[0].deck_id,
		       setup.duelist[1].deck_id, s.prizes, (unsigned long)seed);
		int result = tcg_run_duel(&setup);
		result_screen(result);
	}
}
