/*
 * ref.c - reference side of the differential test harness. See ref.h.
 *
 * Overview
 * --------
 * - Symbols: every "BB:AAAA name" line of the .sym file (global and local
 *   labels, ROM and RAM). Exact-name lookup through a hash table; reverse
 *   lookup (address -> nearest label) through per-bank sorted arrays.
 * - Boot: reset, run with no input until the first entry of HandleTitleScreen
 *   (after Start -> ValidateSRAM (blank SRAM -> RestartSRAM + InitSaveData) ->
 *   GameLoop -> _GameLoop -> Func_c1f8), save the machine state.
 * - Points: one emulator breakpoint per (bank, addr); each carries a set of
 *   roles (trap, AI hook, RNG, joypad, trace, return watch, input routine).
 * - Return watches: "the call that entered routine X at SP s returns": a
 *   breakpoint at the return address that was at [s], accepted only when
 *   SP == s + 2. Used for the AIDoAction hook, UpdateRNGSources results and
 *   the input driver.
 * - Joypad: a point at ReadJoypad; the buttons the original reads are decided
 *   there (input driver, see "input driver" below).
 */
#include "ref.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CYCLES_PER_FRAME 70224u

/* original constants (src/constants) */
#define PLAYER_TURN 0xc2
#define OPPONENT_TURN 0xc3
#define DUELVARS_DUELIST_TYPE 0xf1
#define DUELIST_TYPE_PLAYER 0x00
#define DUELIST_TYPE_AI_OPP 0x80
#define MUSIC_DUEL_THEME_1 0x02
#define TEXT_SPEED_5 0

#define PAD_A GB_BTN_A
#define PAD_B GB_BTN_B
#define PAD_RIGHT GB_BTN_RIGHT
#define PAD_LEFT GB_BTN_LEFT
#define PAD_UP GB_BTN_UP
#define PAD_DOWN GB_BTN_DOWN

/* ------------------------------------------------------------------------ */
/* small utils                                                               */

static char *xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = malloc(n);
	if (p)
		memcpy(p, s, n);
	return p;
}

static uint8_t *load_file(const char *path, size_t *size)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		perror(path);
		return NULL;
	}
	fseek(fp, 0, SEEK_END);
	long n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	uint8_t *buf = malloc(n > 0 ? (size_t)n : 1);
	if (!buf || fread(buf, 1, (size_t)n, fp) != (size_t)n) {
		fprintf(stderr, "%s: read error\n", path);
		fclose(fp);
		free(buf);
		return NULL;
	}
	fclose(fp);
	*size = (size_t)n;
	return buf;
}

static uint32_t hash_str(const char *s)
{
	uint32_t h = 2166136261u;
	while (*s)
		h = (h ^ (uint8_t)*s++) * 16777619u;
	return h;
}

/* ------------------------------------------------------------------------ */
/* data structures                                                           */

typedef struct Sym {
	char *name;
	int bank;
	uint16_t addr;
	uint8_t local;     /* name contains '.' */
	uint32_t order;    /* line order in the .sym file */
} Sym;

enum {
	P_TRAP  = 1 << 0,
	P_RESET = 1 << 1,
	P_AI    = 1 << 2,
	P_RNG   = 1 << 3,
	P_JOY   = 1 << 4,
	P_INPUT = 1 << 5,   /* input routine entry / ready point (Point.input_roles) */
	P_START = 1 << 6,   /* StartDuel (human mode: optional custom opponent deck) */
	P_VFY_DUEL = 1 << 7,   /* HandleDuelMenuInput: verify the duel menu selection */
	P_VFY_CURSOR = 1 << 8, /* YourOrOppPlayAreaScreen_HandleInput: verify prize / peek selection */
	P_VFY_YESNO = 1 << 9,  /* HandleYesOrNoMenu.a_pressed: verify yes / no / two-item selection */
};

typedef struct Point {
	int bank;          /* 0 for ROM0, ROM bank for $4000-$7fff, -1 for RAM */
	uint16_t addr;
	int gbid;          /* emulator breakpoint id, -1 = free slot */
	uint32_t flags;
	int ntrace;        /* trace ids with this point */
	int trace_ids[4];
	int nwatch;
	int ninput;
	short input_roles[6];   /* kInputRoutines index << 2 | 1 entry, 2 ready label, 3 generic HandleMenuInput */
} Point;

typedef struct Trace {
	char *name;
	int bank;
	uint16_t addr;
	int point;
	int active;
} Trace;

enum { W_AI = 1, W_RNG, W_POLL };

typedef struct Watch {
	int point;
	uint16_t sp;       /* SP expected after the return */
	int kind;
	int aux;           /* W_AI: saved wOpponentDeckID; W_POLL: check (see verify_on_poll) */
} Watch;

#define MAX_WATCH 128
#define MAX_PENDING 16
#define MAX_SESS 16

typedef struct Pending {
	int kind;
	const char *name;
	int trace_id;
	uint8_t rng_value;
	int has_input;
	RefInput input;
	char msg[256];
} Pending;

/* input routine sessions (phase 2) */
enum { S_PENDING = 0, S_WAITING, S_ACTIVE, S_FIRED, S_DONE };

typedef struct Session {
	int routine;       /* index into kInputRoutines */
	uint16_t sp;       /* SP at entry (the routine's frame is alive while SP <= sp) */
	uint16_t poll_sp;  /* SP of the polling loop's DoFrame call frame */
	int state;         /* S_* */
	int armed;         /* the poll loop came around since the last decision */
	int answer;        /* chosen answer (REF_CANCEL = B) */
	int steps;         /* cursor presses delivered for the current answer */
	int verify;        /* the poll that processes the final press is being checked */
	const char *caller;
	RefInput in;
} Session;

struct Ref {
	GB *gb;
	/* symbols */
	Sym *syms;
	int nsyms;
	int *hash;         /* open addressing table of sym indexes, -1 = empty */
	uint32_t hash_mask;
	int *bysym[0x200]; /* per ROM bank: sorted sym indexes (ROM labels only) */
	int bycount[0x200];
	/* boot snapshot */
	uint8_t *snap;
	/* key addresses */
	uint16_t a_TryContinueDuel, s_CurrentDuel, s_BackupCurrentDuel;
	int b_BackupCurrentDuel;
	uint16_t a_StartDuel, a_StartDuel_VSAIOpp, a_PowersOf2, a_AIDoAction, a_UpdateRNGSources,
		a_ReadJoypad, a_Start, a_SwitchToBankAtSP;
	int b_StartDuel;
	/* RAM addresses */
	uint16_t w_PlayerDeck, w_OpponentDeck, w_OpponentDeckID, w_NPCDuelDeckID, w_NPCDuelPrizes,
		w_DuelTheme, w_IsPracticeDuel, w_OpponentName, w_OpponentPortrait, w_RNG1, w_AnimationsDisabled,
		w_TextSpeed, w_SkipDelayAllowed, w_DuelFinished, w_DuelResult, w_DuelTurns, w_DuelTempList,
		w_CurMenuItem, w_ListScrollOffset, w_NumListItems, w_NumMenuItems, w_ExcludeArenaPokemon, w_YourOrOppPlayAreaCurPosition, w_NoItemSelectionMenuKeys,
		w_CardListHeaderText, w_CardListInfoBoxText, w_PlayerPrizes, w_TransitionTablePtr,
		w_DuelInitialPrizesUpperBitsSet, w_DeckName;
	int b_TransitionTables;   /* bank of the cursor_transition tables (BANK(_SelectPrizeCards)) */
	uint8_t dv_Prizes;     /* DUELVARS_PRIZES */
	uint16_t a_ShiftListItemToColor;
	int b_ShiftListItemToColor;
	uint16_t h_WhoseTurn, h_BankROM, h_KeysPressed, h_KeysHeld, h_KeysReleased, h_DPadHeld,
		h_DPadRepeat, h_CurMenuItem;
	uint16_t s_TextSpeed, s_AnimationsDisabled, s_SkipDelayAllowed, s_Deck1Cards, s_CurrentlySelectedDeck;
	/* points */
	Point *pts;
	int npts, cappts;
	int pt_trap, pt_reset, pt_ai, pt_rng, pt_joy, pt_start;
	/* traces */
	Trace *traces;
	int ntraces, captraces;
	int rng_trace;
	/* watches */
	Watch watch[MAX_WATCH];
	int nwatch;
	/* pending events */
	Pending pend[MAX_PENDING];
	int npend, pend_head;
	/* duel state */
	RefDuel duel;
	int running;       /* 0 = no duel, 1 = running, 2 = stopped (end/hang/error) */
	Pending last;      /* the final event, re-reported */
	uint64_t start_frames, start_cycles;
	uint64_t last_progress;  /* cycles */
	int start_patched;
	/* joypad */
	uint8_t last_buttons;      /* buttons given to the previous ReadJoypad */
	/* sessions */
	Session sess[MAX_SESS];
	int nsess;
	int waiting;       /* index of the session waiting for ref_answer, -1 if none */
	/* screenshots */
	char *screen_dir;
	int screen_every;
	uint64_t next_screen;
	/* names of routines skipped when looking for the routine waiting on the joypad */
	int nframefns;
	uint16_t framefn[8];
};

/* ------------------------------------------------------------------------ */
/* symbols                                                                   */

static int sym_cmp_ref_syms_bank(const void *a, const void *b, const Sym *syms)
{
	const Sym *x = &syms[*(const int *)a], *y = &syms[*(const int *)b];
	if (x->addr != y->addr)
		return x->addr < y->addr ? -1 : 1;
	if (x->local != y->local)
		return x->local ? 1 : -1;
	return x->order < y->order ? -1 : (x->order > y->order);
}

static const Sym *g_sort_syms;
static int sym_cmp(const void *a, const void *b) { return sym_cmp_ref_syms_bank(a, b, g_sort_syms); }

static int load_syms(Ref *r, const char *path)
{
	FILE *fp = fopen(path, "r");
	if (!fp) {
		perror(path);
		return -1;
	}
	char line[512];
	int cap = 0;
	while (fgets(line, sizeof line, fp)) {
		unsigned bank, addr;
		char name[256];
		if (line[0] == ';')
			continue;
		/* "BB:AAAA name" (text id constants "NNNN name_" have no colon and are skipped) */
		if (sscanf(line, "%x:%x %255s", &bank, &addr, name) != 3)
			continue;
		if (r->nsyms == cap) {
			cap = cap ? cap * 2 : 8192;
			Sym *n = realloc(r->syms, sizeof *n * (size_t)cap);
			if (!n) {
				fclose(fp);
				return -1;
			}
			r->syms = n;
		}
		Sym *s = &r->syms[r->nsyms];
		s->name = xstrdup(name);
		s->bank = (int)bank;
		s->addr = (uint16_t)addr;
		s->local = strchr(name, '.') != NULL;
		s->order = (uint32_t)r->nsyms;
		r->nsyms++;
	}
	fclose(fp);
	if (!r->nsyms) {
		fprintf(stderr, "%s: no symbols\n", path);
		return -1;
	}
	/* hash */
	uint32_t size = 1;
	while (size < (uint32_t)r->nsyms * 2)
		size <<= 1;
	r->hash = malloc(sizeof(int) * size);
	r->hash_mask = size - 1;
	for (uint32_t i = 0; i < size; i++)
		r->hash[i] = -1;
	for (int i = 0; i < r->nsyms; i++) {
		uint32_t h = hash_str(r->syms[i].name) & r->hash_mask;
		while (r->hash[h] >= 0) {
			if (!strcmp(r->syms[r->hash[h]].name, r->syms[i].name))
				break; /* duplicate name: keep the first */
			h = (h + 1) & r->hash_mask;
		}
		if (r->hash[h] < 0)
			r->hash[h] = i;
	}
	/* per-bank sorted ROM labels */
	for (int i = 0; i < r->nsyms; i++) {
		const Sym *s = &r->syms[i];
		if (s->addr >= 0x8000 || s->bank < 0 || s->bank >= 0x200)
			continue;
		if (s->addr < 0x4000 && s->bank != 0)
			continue;
		r->bycount[s->bank]++;
	}
	for (int b = 0; b < 0x200; b++)
		if (r->bycount[b]) {
			r->bysym[b] = malloc(sizeof(int) * (size_t)r->bycount[b]);
			r->bycount[b] = 0;
		}
	for (int i = 0; i < r->nsyms; i++) {
		const Sym *s = &r->syms[i];
		if (s->addr >= 0x8000 || s->bank < 0 || s->bank >= 0x200)
			continue;
		if (s->addr < 0x4000 && s->bank != 0)
			continue;
		r->bysym[s->bank][r->bycount[s->bank]++] = i;
	}
	g_sort_syms = r->syms;
	for (int b = 0; b < 0x200; b++)
		if (r->bycount[b])
			qsort(r->bysym[b], (size_t)r->bycount[b], sizeof(int), sym_cmp);
	return 0;
}

static const Sym *find_sym(const Ref *r, const char *name)
{
	uint32_t h = hash_str(name) & r->hash_mask;
	while (r->hash[h] >= 0) {
		const Sym *s = &r->syms[r->hash[h]];
		if (!strcmp(s->name, name))
			return s;
		h = (h + 1) & r->hash_mask;
	}
	return NULL;
}

int ref_sym(Ref *r, const char *name, int *bank, uint16_t *addr)
{
	const Sym *s = find_sym(r, name);
	if (!s)
		return -1;
	if (bank)
		*bank = s->bank;
	if (addr)
		*addr = s->addr;
	return 0;
}

/* nearest label at or below (bank, addr); global_only skips local labels */
static const Sym *sym_at(const Ref *r, int bank, uint16_t addr, int global_only)
{
	if (addr >= 0x8000)
		return NULL;
	if (addr < 0x4000)
		bank = 0;
	if (bank < 0 || bank >= 0x200 || !r->bycount[bank])
		return NULL;
	const int *v = r->bysym[bank];
	int lo = 0, hi = r->bycount[bank]; /* first index with addr > target */
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (r->syms[v[mid]].addr <= addr)
			lo = mid + 1;
		else
			hi = mid;
	}
	int i = lo - 1;
	if (global_only)
		while (i >= 0 && r->syms[v[i]].local)
			i--;
	if (i < 0)
		return NULL;
	/* first (preferred) label at that address */
	uint16_t a = r->syms[v[i]].addr;
	while (i > 0 && r->syms[v[i - 1]].addr == a && (!global_only || !r->syms[v[i - 1]].local))
		i--;
	return &r->syms[v[i]];
}

const char *ref_addr_name(Ref *r, int bank, uint16_t addr, char *buf, size_t n)
{
	const Sym *s = sym_at(r, bank, addr, 0);
	int b = addr < 0x4000 ? 0 : bank;
	if (!s)
		snprintf(buf, n, "%02x:%04x", b & 0xff, addr);
	else if (s->addr == addr)
		snprintf(buf, n, "%s", s->name);
	else
		snprintf(buf, n, "%s+%u", s->name, (unsigned)(addr - s->addr));
	return buf;
}

static uint16_t need_sym(Ref *r, const char *name, int *bank, int *ok)
{
	const Sym *s = find_sym(r, name);
	if (!s) {
		fprintf(stderr, "ref: symbol %s not found\n", name);
		*ok = 0;
		return 0;
	}
	if (bank)
		*bank = s->bank;
	return s->addr;
}

/* ------------------------------------------------------------------------ */
/* ROM access                                                                */

static uint8_t rom_byte(Ref *r, int bank, uint16_t addr)
{
	uint32_t off = addr < 0x4000 ? addr : (uint32_t)bank * 0x4000 + (addr & 0x3fff);
	if (off >= r->gb->rom_size)
		return 0xff;
	return r->gb->rom[off];
}

/* Expand deck `deck` like CopyDeckData: every card of the (count, card id)
 * list in order, up to the 0 count, into out[0..0x80) (the size of the
 * wPlayerDeck/wOpponentDeck buffers); *name = the 2 bytes after the 0 count
 * (copied to wDeckName). Returns the number of cards, -1 for a NULL pointer. */
static int rom_deck_raw(Ref *r, int deck, uint8_t out[0x80], uint16_t *name)
{
	const Sym *s = find_sym(r, "DeckPointers");
	if (!s || deck < 0 || deck > 0xff)
		return -1;
	int bank = s->bank;
	/* the table ends with a NULL entry: no deck past it */
	for (int i = 0; i <= deck; i++) {
		uint16_t q = (uint16_t)(s->addr + i * 2);
		if (!(rom_byte(r, bank, q) | rom_byte(r, bank, (uint16_t)(q + 1))))
			return -1;
	}
	uint16_t p = (uint16_t)(s->addr + deck * 2);
	uint16_t ptr = (uint16_t)(rom_byte(r, bank, p) | rom_byte(r, bank, (uint16_t)(p + 1)) << 8);
	memset(out, 0, 0x80);
	int n = 0;
	for (;;) {
		uint8_t cnt = rom_byte(r, bank, ptr++);
		if (!cnt)
			break;
		uint8_t card = rom_byte(r, bank, ptr++);
		while (cnt--) {
			if (n < 0x80)
				out[n] = card;
			n++;
		}
	}
	if (name)
		*name = (uint16_t)(rom_byte(r, bank, ptr) | rom_byte(r, bank, (uint16_t)(ptr + 1)) << 8);
	return n;
}

int ref_rom_deck(Ref *r, int deck, uint8_t out[REF_DECK_SIZE])
{
	uint8_t buf[0x80];
	int n = rom_deck_raw(r, deck, buf, NULL);
	if (n >= 0)
		memcpy(out, buf, REF_DECK_SIZE);
	return n;
}

/* ------------------------------------------------------------------------ */
/* memory helpers                                                            */

static uint8_t *wram(Ref *r, uint16_t a) { return &r->gb->wram[(a - 0xc000) & 0x1fff]; }
static uint8_t *hram(Ref *r, uint16_t a) { return &r->gb->hram[(a - 0xff80) & 0x7f]; }
static uint8_t *sram0(Ref *r, uint16_t a) { return &r->gb->sram[(a - 0xa000) & 0x1fff]; }

/* ------------------------------------------------------------------------ */
/* points                                                                    */

static int norm_bank(int bank, uint16_t addr)
{
	if (addr < 0x4000)
		return 0;
	if (addr >= 0x8000)
		return -1;
	return bank;
}

static int point_get(Ref *r, int bank, uint16_t addr)
{
	bank = norm_bank(bank, addr);
	int free_slot = -1;
	for (int i = 0; i < r->npts; i++) {
		Point *p = &r->pts[i];
		if (p->gbid < 0) {
			if (free_slot < 0)
				free_slot = i;
			continue;
		}
		if (p->addr == addr && p->bank == bank)
			return i;
	}
	int id = gb_add_breakpoint(r->gb, bank, addr);
	if (id < 0)
		return -1;
	if (free_slot < 0) {
		if (r->npts == r->cappts) {
			int nc = r->cappts ? r->cappts * 2 : 64;
			Point *n = realloc(r->pts, sizeof *n * (size_t)nc);
			if (!n)
				return -1;
			r->pts = n;
			r->cappts = nc;
		}
		free_slot = r->npts++;
	}
	Point *p = &r->pts[free_slot];
	memset(p, 0, sizeof *p);
	p->bank = bank;
	p->addr = addr;
	p->gbid = id;
	return free_slot;
}

static void point_maybe_free(Ref *r, int i)
{
	Point *p = &r->pts[i];
	if (p->gbid < 0 || p->flags || p->ntrace || p->nwatch || p->ninput)
		return;
	gb_remove_breakpoint(r->gb, p->gbid);
	p->gbid = -1;
}

static int point_by_gbid(Ref *r, int gbid)
{
	for (int i = 0; i < r->npts; i++)
		if (r->pts[i].gbid == gbid)
			return i;
	return -1;
}

static int point_flag(Ref *r, const char *name, uint32_t flag, uint16_t *out_addr)
{
	const Sym *s = find_sym(r, name);
	if (!s) {
		fprintf(stderr, "ref: symbol %s not found\n", name);
		return -1;
	}
	int i = point_get(r, s->bank, s->addr);
	if (i < 0)
		return -1;
	r->pts[i].flags |= flag;
	if (out_addr)
		*out_addr = s->addr;
	return i;
}

/* ------------------------------------------------------------------------ */
/* return watches                                                            */

/* Watch for the return of the routine just entered (PC at its first
 * instruction, return address at [SP]). */
static int watch_return(Ref *r, int kind, int aux)
{
	GB *gb = r->gb;
	if (r->nwatch >= MAX_WATCH)
		return -1;
	uint16_t ret = gb_read16(gb, gb->sp);
	int bank = ret < 0x4000 ? 0 : gb_rom_bank(gb);
	int pi = point_get(r, bank, ret);
	if (pi < 0)
		return -1;
	r->pts[pi].nwatch++;
	Watch *w = &r->watch[r->nwatch++];
	w->point = pi;
	w->sp = (uint16_t)(gb->sp + 2);
	w->kind = kind;
	w->aux = aux;
	return 0;
}

static void watch_remove(Ref *r, int i)
{
	int pi = r->watch[i].point;
	r->pts[pi].nwatch--;
	r->watch[i] = r->watch[--r->nwatch];
	point_maybe_free(r, pi);
}

static void watch_clear(Ref *r)
{
	while (r->nwatch)
		watch_remove(r, r->nwatch - 1);
}

/* ------------------------------------------------------------------------ */
/* events                                                                    */

static Pending *pend_push(Ref *r, int kind, const char *name)
{
	if (r->npend >= MAX_PENDING)
		return NULL;
	Pending *p = &r->pend[(r->pend_head + r->npend++) % MAX_PENDING];
	memset(p, 0, sizeof *p);
	p->kind = kind;
	p->name = name ? name : "";
	p->trace_id = -1;
	return p;
}

static void progress(Ref *r) { r->last_progress = r->gb->cycles; }

/* The routine a call stack frame entered. CALL/RST/interrupt frames enter
 * fr->target, except the far call vectors: `rst $28; db bank; dw addr`
 * (farcall) and `rst $18; dw addr` (bank1call) enter addr through FarCall /
 * Bank1Call, which jump there with a `ret` (no frame of its own). */
static void frame_callee(Ref *r, const GBFrame *fr, int *bank, uint16_t *addr)
{
	int rb = fr->ret < 0x4000 ? 0 : fr->bank;
	if (!fr->is_interrupt && fr->target == 0x28) {
		*bank = rom_byte(r, rb, fr->ret);
		*addr = (uint16_t)(rom_byte(r, rb, (uint16_t)(fr->ret + 1)) | rom_byte(r, rb, (uint16_t)(fr->ret + 2)) << 8);
	} else if (!fr->is_interrupt && fr->target == 0x18) {
		*bank = 1;
		*addr = (uint16_t)(rom_byte(r, rb, fr->ret) | rom_byte(r, rb, (uint16_t)(fr->ret + 1)) << 8);
	} else {
		*bank = fr->target < 0x4000 ? 0 : fr->bank;
		*addr = fr->target;
	}
}

static void fill_stack(Ref *r, RefEvent *ev)
{
	GB *gb = r->gb;
	int depth = gb->cs_depth;
	int first = depth > REF_STACK_MAX ? depth - REF_STACK_MAX : 0;
	ev->depth = depth - first;
	for (int i = first; i < depth; i++) {
		const GBFrame *fr = &gb->cs[i];
		RefFrame *o = &ev->stack[i - first];
		int bank;
		uint16_t target;
		frame_callee(r, fr, &bank, &target);
		const Sym *s = sym_at(r, bank, target, 0);
		o->name = s ? s->name : "?";
		o->off = s ? (uint16_t)(target - s->addr) : 0;
		o->target = target;
		o->ret = fr->ret;
		o->sp = fr->sp;
		o->bank = (uint8_t)fr->bank;
		o->irq = fr->is_interrupt;
	}
}

static void fill_event(Ref *r, RefEvent *ev, const Pending *p)
{
	GB *gb = r->gb;
	memset(ev, 0, offsetof(RefEvent, stack));
	ev->kind = p->kind;
	ev->name = p->name;
	ev->trace_id = p->trace_id;
	ev->frame = gb->frames - r->start_frames;
	ev->cycles = gb->cycles - r->start_cycles;
	ev->regs.a = gb->a; ev->regs.f = gb->f; ev->regs.b = gb->b; ev->regs.c = gb->c;
	ev->regs.d = gb->d; ev->regs.e = gb->e; ev->regs.h = gb->h; ev->regs.l = gb->l;
	ev->regs.sp = gb->sp; ev->regs.pc = gb->pc;
	ev->regs.bank = (uint8_t)gb_rom_bank(gb);
	ev->whose_turn = *hram(r, r->h_WhoseTurn);
	ev->rng_value = p->rng_value;
	memcpy(ev->rng, wram(r, r->w_RNG1), 3);
	fill_stack(r, ev);
	ev->wram = gb->wram;
	ev->hram = gb->hram;
	if (p->has_input)
		ev->input = p->input;
	else
		memset(&ev->input, 0, sizeof ev->input);
	ev->duel_finished = *wram(r, r->w_DuelFinished);
	ev->duel_result = *wram(r, r->w_DuelResult);
	ev->duel_turns = *wram(r, r->w_DuelTurns);
	snprintf(ev->msg, sizeof ev->msg, "%s", p->msg);
}

static void stack_string(Ref *r, char *buf, size_t n)
{
	GB *gb = r->gb;
	size_t len = 0;
	char nb[128];
	buf[0] = 0;
	for (int i = gb->cs_depth - 1; i >= 0 && len + 1 < n; i--) {
		const GBFrame *fr = &gb->cs[i];
		int bank;
		uint16_t target;
		frame_callee(r, fr, &bank, &target);
		ref_addr_name(r, bank, target, nb, sizeof nb);
		int k = snprintf(buf + len, n - len, "%s%s%s", len ? " < " : "", fr->is_interrupt ? "IRQ:" : "", nb);
		if (k < 0)
			break;
		len += (size_t)k;
	}
}

/* stop the duel with a final event */
static void stop_duel(Ref *r, int kind, const char *fmt, ...)
{
	Pending *p = pend_push(r, kind, kind == REF_EV_END ? "end" : kind == REF_EV_HANG ? "hang" : "error");
	if (!p) {
		r->npend = 0;
		p = pend_push(r, kind, "error");
	}
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(p->msg, sizeof p->msg, fmt, ap);
	va_end(ap);
	r->last = *p;
	r->running = 2;
	if (r->screen_dir && kind != REF_EV_END) {
		char path[1024];
		snprintf(path, sizeof path, "%s/%s.png", r->screen_dir, kind == REF_EV_HANG ? "hang" : "error");
		gb_write_png(r->gb, path);
	}
}

/* ------------------------------------------------------------------------ */
/* routine identification                                                    */

/* global routine containing addr in bank */
static const Sym *routine_at(Ref *r, int bank, uint16_t addr)
{
	return sym_at(r, bank, addr, 1);
}

/* The routine whose loop is waiting on the joypad: walk the shadow call stack
 * from the innermost frame and return the routine containing the call site of
 * the first frame whose caller is not a frame helper (DoFrame & co). */
static const Sym *joypad_waiter(Ref *r, int *frame_index)
{
	GB *gb = r->gb;
	for (int i = gb->cs_depth - 1; i >= 0; i--) {
		const GBFrame *fr = &gb->cs[i];
		if (fr->is_interrupt)
			return NULL;
		uint16_t site = (uint16_t)(fr->ret - 1);
		int bank = site < 0x4000 ? 0 : fr->bank;
		const Sym *s = routine_at(r, bank, site);
		if (!s)
			return NULL;
		int skip = 0;
		for (int k = 0; k < r->nframefns; k++)
			if (s->addr == r->framefn[k] && s->bank == 0)
				skip = 1;
		if (!skip) {
			if (frame_index)
				*frame_index = i;
			return s;
		}
	}
	return NULL;
}

/* ------------------------------------------------------------------------ */
/* joypad debugging (REF_DEBUG_JOY=1: print each new routine waiting on the joypad) */

static int g_debug_joy = -1;

static void debug_waiter(const Sym *w, int ack)
{
	static const char *seen[256];
	static int nseen;
	if (g_debug_joy < 0)
		g_debug_joy = getenv("REF_DEBUG_JOY") != NULL;
	if (!g_debug_joy)
		return;
	const char *n = w ? w->name : "(none)";
	for (int i = 0; i < nseen; i++)
		if (seen[i] == n)
			return;
	if (nseen < 256)
		seen[nseen++] = n;
	fprintf(stderr, "joypad waiter: %s%s\n", n, ack ? " (ack)" : "");
}

/* ------------------------------------------------------------------------ */
/* input driver                                                              */
/*
 * Every joypad read of the original goes through ReadJoypad (called by
 * DoFrame once per frame). The harness decides the buttons at the entry of
 * ReadJoypad, so the original only ever sees button presses:
 *
 *  1. ACK: if the routine waiting on the joypad (the routine containing the
 *     call site of the innermost DoFrame/DoFrameIfLCDEnabled call) is one of
 *     kAckRoutines, press A (on every other read: a release read in between
 *     makes each press show up in hKeysPressed).
 *  2. Input session (human duelist, phase 2): the innermost active session
 *     whose polling loop is the one reading (its DoFrame frame is at the
 *     expected SP) gets one decision per poll iteration: release if anything
 *     is held, else a D-pad press towards the answer, else A (or B to cancel).
 *     The decision is closed-loop: it is recomputed from the cursor variables
 *     (wCurMenuItem, wListScrollOffset, ...) before every press.
 *  3. Auto-exit screens (kAutoBRoutines: card pages, the Check menu): if one of
 *     them is on the call stack below every live session, press B.
 *  4. Otherwise no button is pressed.
 */

/* routines that only wait for A/B to acknowledge a message/screen */
static const char *const kAckRoutines[] = {
	"WaitForWideTextBoxInput",
	"DrawWideTextBox_WaitForInput",
	"DrawNarrowTextBox_WaitForInput",
	"WaitForButtonAorB",
	NULL,
};

/* look-only screens with no counterpart in the port's request() interface:
 * exited with B as soon as they wait for input */
static const char *const kAutoBRoutines[] = {
	"OpenCardPage",              /* card page (Check in the item menu, START in lists) */
	"OpenAttackPage",            /* START in the attack menu */
	"_OpenDuelCheckMenu",        /* duel menu "Check" */
	"DuelCheckMenu_YourPlayArea",
	"DuelCheckMenu_OppPlayArea",
	"OpenInPlayAreaScreen",
	"OpenGlossaryScreen",
	NULL,
};

static int name_in(const char *const *list, const char *name)
{
	for (int i = 0; list[i]; i++)
		if (!strcmp(list[i], name))
			return 1;
	return 0;
}

/* steering kinds */
enum {
	K_MENU = 1,   /* HandleMenuInput: wCurMenuItem in [0, wNumMenuItems), UP/DOWN */
	K_LIST,       /* card list: wListScrollOffset + wCurMenuItem in [0, wNumListItems), UP/DOWN */
	K_DUELMENU,   /* 2x3 duel menu grid: UP/DOWN toggle row, RIGHT next column */
	K_YESNO,      /* HandleYesOrNoMenu: wCurMenuItem 0 = YES, 1 = NO; RIGHT toggles; A only */
	K_TWO,        /* TwoItemHorizontalMenu: wCurMenuItem 0 left, 1 right; RIGHT toggles; A only */
	K_PLAYAREA,   /* DisplayPlayAreaScreen: location = wExcludeArenaPokemon + wCurMenuItem */
	K_PRIZE,      /* _SelectPrizeCards: wYourOrOppPlayAreaCurPosition 0-5 over a transition table */
	K_COLOR,      /* HandleColorChangeScreen: item i is color ShiftListItemToColor[i] */
	K_PEEK,       /* _HandlePeekSelection: wYourOrOppPlayAreaCurPosition 0-7 over a transition table */
	K_CONFIRM,    /* WaitForButtonAorB after SetCursorParametersForTextBox_Default: A or B */
};

typedef struct InputRoutine {
	const char *entry;     /* PC that starts a session (routine entry) */
	const char *ready;     /* poll loop label: request reported at its first hit after entry */
	int type;              /* REF_IN_* */
	int kind;              /* K_* */
	int owns_menu;         /* HandleMenuInput calls from this routine (or via HandleCardListInput) belong to it */
} InputRoutine;

#define GENERIC_MENU 0     /* index of the generic HandleMenuInput entry */

static const InputRoutine kInputRoutines[] = {
	{ "HandleMenuInput", "HandleMenuInput", REF_IN_MENU, K_MENU, 0 },
	{ "PrintDuelMenuAndHandleInput", "PrintDuelMenuAndHandleInput.handle_input", REF_IN_DUEL_MENU, K_DUELMENU, 0 },
	{ "DisplayCardList", "DisplayCardList.wait_button", REF_IN_CARD_LIST, K_LIST, 1 },
	{ "CardListItemSelectionMenu", "CardListItemSelectionMenu.wait_a_or_b", REF_IN_CARD_ITEM_MENU, K_MENU, 1 },
	{ "DisplayPlayAreaScreen", "DisplayPlayAreaScreen.asm_604c", REF_IN_PLAY_AREA, K_PLAYAREA, 1 },
	{ "HandleYesOrNoMenu", "HandleYesOrNoMenu.wait_button_loop", REF_IN_YES_NO, K_YESNO, 0 },
	{ "TwoItemHorizontalMenu", "HandleYesOrNoMenu.wait_button_loop", REF_IN_MENU, K_TWO, 0 },
	{ "HandleEnergyDiscardMenuInput", "HandleEnergyDiscardMenuInput.wait_input", REF_IN_ENERGY_DISCARD, K_LIST, 1 },
	{ "_SelectPrizeCards", "_SelectPrizeCards.loop_handle_input", REF_IN_PRIZE, K_PRIZE, 0 },
	{ "HandleColorChangeScreen", "HandleColorChangeScreen.loop_input", REF_IN_COLOR, K_COLOR, 1 },
	{ "_HandlePeekSelection", "_HandlePeekSelection.loop_input_2", REF_IN_PEEK, K_PEEK, 0 },
	{ "SetCursorParametersForTextBox_Default", "WaitForButtonAorB", REF_IN_CONFIRM, K_CONFIRM, 0 },
};
#define NUM_INPUT_ROUTINES ((int)(sizeof kInputRoutines / sizeof kInputRoutines[0]))

#define MAX_STEPS 120      /* presses per answer before giving up */

/* YourOrOppPlayAreaScreen_HandleInput cursor (prize / peek screens): the
 * cursor moves along the 7-byte cursor_transition table at wTransitionTablePtr
 * (x, y, attr, up, down, right, left). Breadth-first search from cur; moving
 * onto a prize slot that does not exist in this duel
 * (wDuelInitialPrizesUpperBitsSet) moves again in the same direction, like the
 * original. Fills first_dir[n] (index into kTransDirs, -1 = cur itself, -2 =
 * unreachable) for positions 0-7. */
static const uint8_t kTransDirs[4] = { PAD_UP, PAD_DOWN, PAD_RIGHT, PAD_LEFT };

static void transition_bfs(Ref *r, int cur, int first_dir[8])
{
	uint16_t tbl = (uint16_t)(wram(r, r->w_TransitionTablePtr)[0] | wram(r, r->w_TransitionTablePtr)[1] << 8);
	uint8_t exist = *wram(r, r->w_DuelInitialPrizesUpperBitsSet);
	int queue[8], qh = 0, qt = 0;
	for (int i = 0; i < 8; i++)
		first_dir[i] = -2;
	if (cur < 0 || cur >= 8)
		return;
	first_dir[cur] = -1;
	queue[qt++] = cur;
	while (qh < qt) {
		int p = queue[qh++];
		for (int d = 0; d < 4; d++) {
			int n = p, hops = 0;
			do {
				n = rom_byte(r, r->b_TransitionTables, (uint16_t)(tbl + n * 7 + 3 + d));
			} while (n < 8 && !(exist & (1 << n)) && p != 6 && ++hops < 8);
			if (n >= 8 || first_dir[n] != -2)
				continue;
			first_dir[n] = first_dir[p] < 0 ? d : first_dir[p];
			queue[qt++] = n;
		}
	}
}

static uint8_t transition_steer(Ref *r, int cur, int target)
{
	int fd[8];
	transition_bfs(r, cur, fd);
	if (target < 0 || target >= 8 || fd[target] < 0)
		return PAD_DOWN;   /* unreachable: keep moving, MAX_STEPS will catch it */
	return kTransDirs[fd[target]];
}

/* --- verification: after the final A press, check that the original selected
 * the answer (the poll routine that processes the press returns carry with
 * the expected item). Cancels (B) are not checked: some callers test B
 * before polling. */

enum { V_CURMENU = 1, V_REG_A = 2 };

/* the item value the poll routine reports for the session's answer */
static int sess_expected_item(Ref *r, const Session *s)
{
	const InputRoutine *ir = &kInputRoutines[s->routine];
	switch (ir->kind) {
	case K_PLAYAREA:
		return s->answer - s->in.first;
	case K_YESNO:
		return s->answer ? 0 : 1;
	case K_COLOR:
		for (int i = 0; i < 6; i++)
			if (rom_byte(r, r->b_ShiftListItemToColor, (uint16_t)(r->a_ShiftListItemToColor + i)) == s->answer)
				return i;
		return -1;
	default:
		return s->answer;
	}
}

/* at the entry of the poll routine that will process the final press */
static void verify_on_poll(Ref *r, Session *s, int how)
{
	if (s->verify || s->answer == REF_CANCEL)
		return;
	s->verify = 1;
	int si = (int)(s - r->sess);
	if (watch_return(r, W_POLL, how | (sess_expected_item(r, s) & 0xff) << 4 | si << 12) < 0)
		stop_duel(r, REF_EV_ERROR, "too many return watches (verify)");
}

static void verify_result(Ref *r, int aux)
{
	GB *gb = r->gb;
	int how = aux & 15, expected = (aux >> 4) & 0xff, si = aux >> 12;
	const char *name = si < r->nsess ? kInputRoutines[r->sess[si].routine].entry : "?";
	int answer = si < r->nsess ? r->sess[si].answer : -1;
	if (!(gb->f & 0x10)) {
		stop_duel(r, REF_EV_ERROR, "%s: the press for answer %d was not accepted", name, answer);
		return;
	}
	int got = how == V_CURMENU ? *hram(r, r->h_CurMenuItem) : gb->a;
	if (got != expected)
		stop_duel(r, REF_EV_ERROR, "%s: answer %d selected item %d instead of %d", name, answer, got, expected);
}

static int is_frame_helper(Ref *r, const Sym *s)
{
	for (int k = 0; k < r->nframefns; k++)
		if (s->addr == r->framefn[k] && s->bank == 0)
			return 1;
	return 0;
}

/* SP of the frame of the call into DoFrame/DoFrameIfLCDEnabled made by the
 * polling loop (the outermost frame-helper frame above ReadJoypad), -1 if the
 * read does not come from a frame helper. */
static int poll_frame_sp(Ref *r)
{
	GB *gb = r->gb;
	int sp = -1;
	for (int i = gb->cs_depth - 1; i >= 0; i--) {
		const GBFrame *fr = &gb->cs[i];
		if (fr->is_interrupt)
			break;
		int bank;
		uint16_t target;
		frame_callee(r, fr, &bank, &target);
		const Sym *s = routine_at(r, bank, target);
		if (!s || !is_frame_helper(r, s))
			break;
		sp = fr->sp;
	}
	return sp;
}

/* innermost frame (index) whose callee or call-site routine is an auto-exit screen, -1 if none */
static int autob_frame(Ref *r)
{
	GB *gb = r->gb;
	for (int i = gb->cs_depth - 1; i >= 0; i--) {
		const GBFrame *fr = &gb->cs[i];
		int bank;
		uint16_t target;
		frame_callee(r, fr, &bank, &target);
		const Sym *s = routine_at(r, bank, target);
		if (s && name_in(kAutoBRoutines, s->name))
			return i;
		uint16_t site = (uint16_t)(fr->ret - 1);
		s = routine_at(r, site < 0x4000 ? 0 : fr->bank, site);
		if (s && !fr->is_interrupt && name_in(kAutoBRoutines, s->name))
			return i;
	}
	return -1;
}

static uint8_t wr8(Ref *r, uint16_t a) { return *wram(r, a); }

/* --- sessions --- */

/* drop sessions whose routine has returned (SP above their entry SP) */
static void sess_gc(Ref *r)
{
	uint16_t sp = r->gb->sp;
	while (r->nsess && r->sess[r->nsess - 1].sp < sp)
		r->nsess--;
}

static Session *sess_top(Ref *r) { return r->nsess ? &r->sess[r->nsess - 1] : NULL; }

/* current cursor position, in answer units */
static int sess_cursor(Ref *r, const Session *s)
{
	int item = wr8(r, r->w_CurMenuItem);
	switch (kInputRoutines[s->routine].kind) {
	case K_CONFIRM:
		return 0;
	case K_LIST:
		return wr8(r, r->w_ListScrollOffset) + item;
	case K_YESNO:
		return item == 0 ? 1 : 0;
	case K_PLAYAREA:
		return wr8(r, r->w_ExcludeArenaPokemon) + item;
	case K_PRIZE:
	case K_PEEK:
		return wr8(r, r->w_YourOrOppPlayAreaCurPosition);
	case K_COLOR:
		return item < 6 ? rom_byte(r, r->b_ShiftListItemToColor, (uint16_t)(r->a_ShiftListItemToColor + item)) : -1;
	default:
		return item;
	}
}

/* fill the request description from the original's variables at the ready point */
static void sess_describe(Ref *r, Session *s, RefInput *in)
{
	const InputRoutine *ir = &kInputRoutines[s->routine];
	GB *gb = r->gb;
	memset(in, 0, sizeof *in);
	in->type = ir->type;
	in->routine = ir->entry;
	in->caller = s->caller;
	in->whose_turn = *hram(r, r->h_WhoseTurn);
	in->cursor = sess_cursor(r, s);
	in->can_cancel = 1;
	switch (ir->kind) {
	case K_MENU:
		in->num_items = wr8(r, r->w_NumMenuItems);
		break;
	case K_LIST:
		in->num_items = wr8(r, r->w_NumListItems);
		break;
	case K_DUELMENU:
		in->num_items = 6;
		in->can_cancel = 0;
		break;
	case K_YESNO:
	case K_TWO:
		in->num_items = 2;
		in->can_cancel = 0;
		break;
	case K_CONFIRM:
		in->num_items = 1;
		break;
	case K_PLAYAREA:
		in->first = wr8(r, r->w_ExcludeArenaPokemon);
		in->num_items = wr8(r, r->w_NumMenuItems);
		in->view_only = (wr8(r, r->w_NoItemSelectionMenuKeys) & PAD_A) != 0;
		break;
	case K_PRIZE: {
		in->num_items = 6;
		in->can_cancel = 0;
		uint8_t prizes = gb->wram[((in->whose_turn << 8) | r->dv_Prizes) - 0xc000];
		for (int i = 0; i < 6; i++)
			if (prizes & (1 << i))
				in->list[in->list_len++] = (uint8_t)i;
		break;
	}
	case K_COLOR:
		in->num_items = 6;
		break;
	case K_PEEK: {
		/* positions reachable in the current transition table */
		int fd[8];
		transition_bfs(r, in->cursor, fd);
		in->num_items = 8;
		for (int i = 0; i < 8; i++)
			if (fd[i] != -2)
				in->list[in->list_len++] = (uint8_t)i;
		break;
	}
	}
	if (ir->type == REF_IN_CARD_LIST || ir->type == REF_IN_ENERGY_DISCARD) {
		const uint8_t *l = wram(r, r->w_DuelTempList);
		while (in->list_len < REF_LIST_MAX && l[in->list_len] != 0xff)
			in->list_len++;
		memcpy(in->list, l, (size_t)in->list_len);
	}
	if (ir->type == REF_IN_CARD_LIST) {
		in->view_only = (wr8(r, r->w_NoItemSelectionMenuKeys) & PAD_A) != 0;
		in->text = (uint16_t)(wram(r, r->w_CardListHeaderText)[0] | wram(r, r->w_CardListHeaderText)[1] << 8);
		in->text2 = (uint16_t)(wram(r, r->w_CardListInfoBoxText)[0] | wram(r, r->w_CardListInfoBoxText)[1] << 8);
	}
}

/* report the session's request: queue an INPUT event and wait for ref_answer */
static void sess_report(Ref *r, int si)
{
	Session *s = &r->sess[si];
	RefInput in;
	sess_describe(r, s, &in);
	/* the original also shows some screens to the human when the AI acts (the hand
	 * shown by Lass, the play area changed by an AI's effect): those view-only lists
	 * and A/B waits are input in AI-vs-AI duels too */
	if (!r->duel.player_human && !in.view_only && in.type != REF_IN_CONFIRM) {
		char st[400];
		stack_string(r, st, sizeof st);
		stop_duel(r, REF_EV_ERROR, "human input routine %s (from %s) reached in an AI-vs-AI duel: %s",
			kInputRoutines[s->routine].entry, s->caller, st);
		return;
	}
	Pending *p = pend_push(r, REF_EV_INPUT, kInputRoutines[s->routine].entry);
	if (!p) {
		stop_duel(r, REF_EV_ERROR, "event queue full");
		return;
	}
	p->has_input = 1;
	p->input = in;
	s->in = p->input;
	s->state = S_WAITING;
	s->armed = 1;
	s->steps = 0;
	r->waiting = si;
	progress(r);
}

/* routine that called the routine at PC (return address at [SP]). Routines
 * entered through farcall / bank1call return into SwitchToBankAtSP, with the
 * caller's bank at [SP+2] and its return address at [SP+4]. */
static const char *caller_of_entry(Ref *r)
{
	GB *gb = r->gb;
	uint16_t ret = gb_read16(gb, gb->sp);
	int bank = gb_rom_bank(gb);
	if (ret == r->a_SwitchToBankAtSP) {
		bank = gb_read(gb, (uint16_t)(gb->sp + 2));
		ret = gb_read16(gb, (uint16_t)(gb->sp + 4));
	}
	uint16_t site = (uint16_t)(ret - 1);
	const Sym *s = routine_at(r, site < 0x4000 ? 0 : bank, site);
	return s ? s->name : "?";
}

static Session *sess_push(Ref *r, int routine, uint16_t sp, const char *caller)
{
	/* a new routine entered at SP sp replaces every session at the same or a
	 * deeper level (they have returned or tail-jumped here). A generic
	 * HandleMenuInput session (sp = its caller's level) can be polled by a known
	 * routine at that same level (e.g. _HandlePeekSelection's first menu): it
	 * only replaces deeper sessions and a same-level generic one. */
	while (r->nsess) {
		const Session *t = &r->sess[r->nsess - 1];
		if (t->sp < sp || (t->sp == sp && (routine != GENERIC_MENU || t->routine == GENERIC_MENU)))
			r->nsess--;
		else
			break;
	}
	if (r->nsess == MAX_SESS) {
		stop_duel(r, REF_EV_ERROR, "too many nested input sessions");
		return NULL;
	}
	Session *s = &r->sess[r->nsess++];
	memset(s, 0, sizeof *s);
	s->routine = routine;
	s->sp = sp;
	s->state = S_PENDING;
	s->caller = caller;
	return s;
}

/* entry of a known input routine */
static void on_input_entry(Ref *r, int routine)
{
	sess_push(r, routine, r->gb->sp, caller_of_entry(r));
}

/* ready (poll loop) label of a known input routine */
static void on_input_ready(Ref *r, int routine)
{
	GB *gb = r->gb;
	/* the innermost live session of this routine (or of one sharing the label) */
	const char *ready = kInputRoutines[routine].ready;
	int si = -1;
	for (int i = r->nsess - 1; i >= 0; i--)
		if (!strcmp(kInputRoutines[r->sess[i].routine].ready, ready)) {
			si = i;
			break;
		}
	if (si < 0 && kInputRoutines[routine].kind == K_CONFIRM)
		return;   /* WaitForButtonAorB of a text page (WaitForPlayerToAdvanceText): an ACK */
	if (si < 0) {
		/* label reached without passing the entry: start a session here */
		if (!sess_push(r, routine, gb->sp, "?"))
			return;
		si = r->nsess - 1;
	} else if (si != r->nsess - 1) {
		/* deeper sessions are over */
		r->nsess = si + 1;
	}
	Session *s = &r->sess[si];
	s->poll_sp = (uint16_t)(gb->sp - 2);
	if (s->state == S_PENDING || s->state == S_FIRED)
		sess_report(r, si);   /* first poll, or the original came back for another answer */
	else
		s->armed = 1;
}

/* HandleMenuInput: generic menu, unless called by the routine of the
 * innermost session that owns it */
static void on_menu_input(Ref *r)
{
	GB *gb = r->gb;
	const char *caller = caller_of_entry(r);
	Session *top = sess_top(r);
	if (top && kInputRoutines[top->routine].owns_menu &&
	    (!strcmp(caller, kInputRoutines[top->routine].entry) || !strcmp(caller, "HandleCardListInput"))) {
		if (top->state == S_FIRED)
			verify_on_poll(r, top, V_CURMENU);
		return;
	}
	int ab = autob_frame(r);
	if (ab >= 0 && (!top || top->sp > gb->cs[ab].sp))
		return;   /* inside a look-only screen that is being exited with B */
	uint16_t level = (uint16_t)(gb->sp + 2);   /* SP of the polling loop */
	Session *s = NULL;
	int si = -1;
	if (top && top->routine == GENERIC_MENU && top->sp == level) {
		s = top;
		si = r->nsess - 1;
	} else {
		s = sess_push(r, GENERIC_MENU, level, caller);
		if (!s)
			return;
		si = r->nsess - 1;
	}
	s->poll_sp = gb->sp;
	/* this call runs after the DoFrame that delivered the final press: it is
	 * the one that processes it. Only a later call is a new request. */
	if (s->state == S_FIRED) {
		s->state = S_DONE;
		verify_on_poll(r, s, V_CURMENU);
	} else if (s->state == S_PENDING || s->state == S_DONE)
		sess_report(r, si);
	else
		s->armed = 1;
}

/* one steering decision for an active session */
static uint8_t sess_steer(Ref *r, Session *s)
{
	if (r->last_buttons)
		return 0;   /* release first: every press must be a new press */
	if (s->answer == REF_CANCEL) {
		s->state = S_FIRED;
		return PAD_B;
	}
	const InputRoutine *ir = &kInputRoutines[s->routine];
	int cur = sess_cursor(r, s), target = s->answer;
	if (cur == target) {
		s->state = S_FIRED;
		return PAD_A;
	}
	if (++s->steps > MAX_STEPS) {
		stop_duel(r, REF_EV_ERROR, "%s: cursor did not reach answer %d (at %d) after %d presses", ir->entry,
			target, cur, MAX_STEPS);
		return 0;
	}
	switch (ir->kind) {
	case K_DUELMENU:
		return (cur & 1) != (target & 1) ? PAD_DOWN : PAD_RIGHT;
	case K_YESNO:
	case K_TWO:
		return PAD_RIGHT;
	case K_PRIZE:
	case K_PEEK:
		return transition_steer(r, cur, target);
	case K_COLOR: {
		/* move by menu item: find the item showing the target color */
		int item = wr8(r, r->w_CurMenuItem), titem = item;
		for (int i = 0; i < 6; i++)
			if (rom_byte(r, r->b_ShiftListItemToColor, (uint16_t)(r->a_ShiftListItemToColor + i)) == target)
				titem = i;
		return titem > item ? PAD_DOWN : PAD_UP;
	}
	default:
		return target > cur ? PAD_DOWN : PAD_UP;
	}
}

static void on_joypad(Ref *r)
{
	uint8_t buttons = 0;
	sess_gc(r);
	int wi = -1;
	const Sym *w = joypad_waiter(r, &wi);
	debug_waiter(w, w && name_in(kAckRoutines, w->name));
	Session *s = sess_top(r);
	/* WaitForButtonAorB is an ACK routine, except while a confirm session polls it */
	int confirm = s && kInputRoutines[s->routine].kind == K_CONFIRM &&
		(s->state == S_WAITING || s->state == S_ACTIVE) &&
		poll_frame_sp(r) == s->poll_sp;
	if (w && name_in(kAckRoutines, w->name) && !confirm) {
		if (!(r->last_buttons & PAD_A)) {
			buttons = PAD_A;
			if (r->duel.report_acks) {
				Pending *p = pend_push(r, REF_EV_INPUT, w->name);
				if (p) {
					p->has_input = 1;
					p->input.type = REF_IN_ACK;
					p->input.routine = w->name;
					p->input.whose_turn = *hram(r, r->h_WhoseTurn);
					p->input.num_items = 1;
					p->input.caller = "?";
					/* the frame that entered the ACK routine: its call site is the caller */
					if (wi > 0) {
						const GBFrame *fr = &r->gb->cs[wi - 1];
						uint16_t site = (uint16_t)(fr->ret - 1);
						const Sym *cs = routine_at(r, site < 0x4000 ? 0 : fr->bank, site);
						if (cs)
							p->input.caller = cs->name;
					}
				}
			}
		}
	} else if (s && s->state == S_ACTIVE && s->armed && poll_frame_sp(r) == s->poll_sp) {
		s->armed = 0;
		buttons = sess_steer(r, s);
	} else {
		int ab = autob_frame(r);
		if (ab >= 0 && (!s || s->sp > r->gb->cs[ab].sp) && !(r->last_buttons & PAD_B))
			buttons = PAD_B;
	}
	r->last_buttons = buttons;
	gb_set_buttons(r->gb, buttons);
}

/* ------------------------------------------------------------------------ */
/* stops                                                                     */

static void on_ai_entry(Ref *r)
{
	if (!r->duel.per_side_ai)
		return;
	uint8_t turn = *hram(r, r->h_WhoseTurn);
	uint8_t type = r->gb->wram[((turn << 8) | DUELVARS_DUELIST_TYPE) - 0xc000];
	uint8_t *id = wram(r, r->w_OpponentDeckID);
	if (watch_return(r, W_AI, *id) < 0) {
		stop_duel(r, REF_EV_ERROR, "too many return watches (AIDoAction)");
		return;
	}
	if (type & DUELIST_TYPE_AI_OPP)
		*id = type & 0x7f;
}

static void handle_watch(Ref *r, int wi)
{
	Watch w = r->watch[wi];
	watch_remove(r, wi);
	switch (w.kind) {
	case W_AI:
		*wram(r, r->w_OpponentDeckID) = (uint8_t)w.aux;
		break;
	case W_POLL:
		verify_result(r, w.aux);
		break;
	case W_RNG:
		progress(r);
		if (r->rng_trace) {
			Pending *p = pend_push(r, REF_EV_RNG, "UpdateRNGSources");
			if (p)
				p->rng_value = r->gb->a;
		}
		break;
	default:
		break;
	}
}

/* StartDuel (human mode reaches it from StartDuel_VSAIOpp after the decks are
 * loaded): optional custom opponent deck */
static void on_start_duel(Ref *r)
{
	if (!r->duel.player_human || r->start_patched || !r->duel.opp_deck[0])
		return;
	memcpy(wram(r, r->w_OpponentDeck), r->duel.opp_deck, REF_DECK_SIZE);
	r->start_patched = 1;
}

static void handle_stop(Ref *r)
{
	GB *gb = r->gb;
	int pi = point_by_gbid(r, gb->hit_breakpoint);
	if (pi < 0)
		return;
	/* copy: handlers may add points (realloc) */
	Point p = r->pts[pi];

	if (p.nwatch) {
		for (int i = r->nwatch - 1; i >= 0; i--)
			if (r->watch[i].point == pi && r->watch[i].sp == gb->sp) {
				handle_watch(r, i);
				break;
			}
	}
	if (p.flags & P_RESET) {
		char st[400];
		stack_string(r, st, sizeof st);
		stop_duel(r, REF_EV_ERROR, "game reset (PC=Start): %s", st);
		return;
	}
	if (p.flags & P_TRAP) {
		if (gb->sp != 0xe000) {
			char st[400];
			stack_string(r, st, sizeof st);
			stop_duel(r, REF_EV_ERROR, "trap reached with SP=%04x: %s", gb->sp, st);
		} else {
			stop_duel(r, REF_EV_END, "wDuelFinished=%u wDuelResult=%u wDuelTurns=%u",
				*wram(r, r->w_DuelFinished), *wram(r, r->w_DuelResult), *wram(r, r->w_DuelTurns));
		}
		return;
	}
	if (p.flags & P_AI) {
		progress(r);
		on_ai_entry(r);
	}
	if (p.flags & P_RNG) {
		if (watch_return(r, W_RNG, 0) < 0)
			stop_duel(r, REF_EV_ERROR, "too many return watches (UpdateRNGSources)");
	}
	if (p.flags & P_START)
		on_start_duel(r);
	if (p.flags & P_INPUT) {
		sess_gc(r);
		for (int k = 0; k < p.ninput && r->running == 1; k++) {
			int code = p.input_roles[k], idx = code >> 2;
			switch (code & 3) {
			case 1: on_input_entry(r, idx); break;
			case 2: on_input_ready(r, idx); break;
			case 3: on_menu_input(r); break;
			}
		}
	}
	if (p.flags & (P_VFY_DUEL | P_VFY_CURSOR | P_VFY_YESNO)) {
		sess_gc(r);
		Session *s = sess_top(r);
		int kind = s ? kInputRoutines[s->routine].kind : 0;
		if (s && s->state == S_FIRED) {
			if ((p.flags & P_VFY_DUEL) && kind == K_DUELMENU)
				verify_on_poll(r, s, V_CURMENU);
			if ((p.flags & P_VFY_CURSOR) && (kind == K_PRIZE || kind == K_PEEK))
				verify_on_poll(r, s, V_REG_A);
			if ((p.flags & P_VFY_YESNO) && (kind == K_YESNO || kind == K_TWO) &&
			    *wram(r, r->w_CurMenuItem) != sess_expected_item(r, s))
				stop_duel(r, REF_EV_ERROR, "%s: answer %d selected item %d", kInputRoutines[s->routine].entry,
					s->answer, *wram(r, r->w_CurMenuItem));
		}
	}
	if (p.flags & P_JOY)
		on_joypad(r);
	for (int k = 0; k < p.ntrace; k++) {
		const Trace *t = &r->traces[p.trace_ids[k]];
		Pending *e = pend_push(r, REF_EV_TRACE, t->name);
		if (e)
			e->trace_id = p.trace_ids[k];
		progress(r);
	}
}

/* ------------------------------------------------------------------------ */
/* public API                                                                */

void ref_duel_defaults(RefDuel *d)
{
	memset(d, 0, sizeof *d);
	d->player_deck_id = 3;   /* CHARMANDER_AND_FRIENDS_DECK_ID */
	d->opp_deck_id = 5;      /* SQUIRTLE_AND_FRIENDS_DECK_ID */
	d->prizes = 6;
	d->duel_theme = MUSIC_DUEL_THEME_1;
	d->rng[0] = 0x12; d->rng[1] = 0x34; d->rng[2] = 0x56;
	d->text_speed = TEXT_SPEED_5;
	d->per_side_ai = 1;
	d->hang_frames = 3600;
}

GB *ref_gb(Ref *r) { return r->gb; }

uint64_t ref_frame(Ref *r) { return r->gb->frames - r->start_frames; }

static int boot(Ref *r)
{
	GB *gb = r->gb;
	int ok = 1;
	int bank;
	uint16_t a = need_sym(r, "HandleTitleScreen", &bank, &ok);
	if (!ok)
		return -1;
	int id = gb_add_breakpoint(gb, bank, a);
	gb->render = 0;
	memset(gb->sram, 0, sizeof gb->sram);   /* blank (never saved) SRAM */
	gb_reset(gb);
	int r_ = gb_run(gb, (uint64_t)CYCLES_PER_FRAME * 3000, 0);
	gb_remove_breakpoint(gb, id);
	if (r_ != GB_STOP_BREAKPOINT || gb->pc != a) {
		fprintf(stderr, "ref: boot did not reach HandleTitleScreen (%d, %s)\n", r_, gb_error_string(gb));
		return -1;
	}
	gb->bp_skip_n = 0;
	gb->hit_breakpoint = -1;
	r->snap = malloc(gb_state_size());
	if (!r->snap)
		return -1;
	gb_save_state(gb, r->snap);
	return 0;
}

Ref *ref_new(const char *rom_path, const char *sym_path)
{
	Ref *r = calloc(1, sizeof *r);
	if (!r)
		return NULL;
	r->waiting = -1;
	size_t size;
	uint8_t *rom = load_file(rom_path, &size);
	if (!rom)
		goto fail;
	r->gb = gb_new(rom, size);
	free(rom);
	if (!r->gb) {
		fprintf(stderr, "%s: gb_new failed\n", rom_path);
		goto fail;
	}
	if (load_syms(r, sym_path))
		goto fail;

	int ok = 1;
	r->a_StartDuel = need_sym(r, "StartDuel", &r->b_StartDuel, &ok);
	r->a_StartDuel_VSAIOpp = need_sym(r, "StartDuel_VSAIOpp", NULL, &ok);
	r->a_TryContinueDuel = need_sym(r, "TryContinueDuel", NULL, &ok);
	r->s_CurrentDuel = need_sym(r, "sCurrentDuel", NULL, &ok);
	r->s_BackupCurrentDuel = need_sym(r, "sBackupCurrentDuel", &r->b_BackupCurrentDuel, &ok);
	r->a_PowersOf2 = need_sym(r, "PowersOf2", NULL, &ok);
	r->a_AIDoAction = need_sym(r, "AIDoAction", NULL, &ok);
	r->a_UpdateRNGSources = need_sym(r, "UpdateRNGSources", NULL, &ok);
	r->a_ReadJoypad = need_sym(r, "ReadJoypad", NULL, &ok);
	r->a_Start = need_sym(r, "Start", NULL, &ok);
	r->a_SwitchToBankAtSP = need_sym(r, "SwitchToBankAtSP", NULL, &ok);
#define W(f, n) r->f = need_sym(r, n, NULL, &ok)
	W(w_PlayerDeck, "wPlayerDeck");
	W(w_OpponentDeck, "wOpponentDeck");
	W(w_OpponentDeckID, "wOpponentDeckID");
	W(w_NPCDuelDeckID, "wNPCDuelDeckID");
	W(w_NPCDuelPrizes, "wNPCDuelPrizes");
	W(w_DuelTheme, "wDuelTheme");
	W(w_IsPracticeDuel, "wIsPracticeDuel");
	W(w_OpponentName, "wOpponentName");
	W(w_OpponentPortrait, "wOpponentPortrait");
	W(w_RNG1, "wRNG1");
	W(w_AnimationsDisabled, "wAnimationsDisabled");
	W(w_TextSpeed, "wTextSpeed");
	W(w_SkipDelayAllowed, "wSkipDelayAllowed");
	W(w_DuelFinished, "wDuelFinished");
	W(w_DuelResult, "wDuelResult");
	W(w_DuelTurns, "wDuelTurns");
	W(w_DuelTempList, "wDuelTempList");
	W(w_CurMenuItem, "wCurMenuItem");
	W(w_ListScrollOffset, "wListScrollOffset");
	W(w_NumListItems, "wNumListItems");
	W(w_NumMenuItems, "wNumMenuItems");
	W(w_ExcludeArenaPokemon, "wExcludeArenaPokemon");
	W(w_YourOrOppPlayAreaCurPosition, "wYourOrOppPlayAreaCurPosition");
	W(w_NoItemSelectionMenuKeys, "wNoItemSelectionMenuKeys");
	W(w_CardListHeaderText, "wCardListHeaderText");
	W(w_CardListInfoBoxText, "wCardListInfoBoxText");
	W(w_PlayerPrizes, "wPlayerPrizes");
	W(w_TransitionTablePtr, "wTransitionTablePtr");
	W(w_DuelInitialPrizesUpperBitsSet, "wDuelInitialPrizesUpperBitsSet");
	W(w_DeckName, "wDeckName");
	W(h_WhoseTurn, "hWhoseTurn");
	W(h_BankROM, "hBankROM");
	W(h_KeysPressed, "hKeysPressed");
	W(h_KeysHeld, "hKeysHeld");
	W(h_KeysReleased, "hKeysReleased");
	W(h_DPadHeld, "hDPadHeld");
	W(h_DPadRepeat, "hDPadRepeat");
	W(h_CurMenuItem, "hCurMenuItem");
	W(s_TextSpeed, "sTextSpeed");
	W(s_AnimationsDisabled, "sAnimationsDisabled");
	W(s_SkipDelayAllowed, "sSkipDelayAllowed");
	W(s_Deck1Cards, "sDeck1Cards");
	W(s_CurrentlySelectedDeck, "sCurrentlySelectedDeck");
#undef W
	r->dv_Prizes = (uint8_t)r->w_PlayerPrizes;
	need_sym(r, "_SelectPrizeCards", &r->b_TransitionTables, &ok);
	r->a_ShiftListItemToColor = need_sym(r, "ShiftListItemToColor", &r->b_ShiftListItemToColor, &ok);
	{
		static const char *const fns[] = { "DoFrame", "DoFrameIfLCDEnabled", "ReadJoypad", NULL };
		for (int i = 0; fns[i]; i++) {
			uint16_t a = need_sym(r, fns[i], NULL, &ok);
			if (r->nframefns < 8)
				r->framefn[r->nframefns++] = a;
		}
	}
	if (!ok)
		goto fail;
	if (boot(r))
		goto fail;

	/* permanent points */
	r->pt_trap = point_flag(r, "PowersOf2", P_TRAP, NULL);
	r->pt_reset = point_flag(r, "Start", P_RESET, NULL);
	r->pt_ai = point_flag(r, "AIDoAction", P_AI, NULL);
	r->pt_rng = point_flag(r, "UpdateRNGSources", P_RNG, NULL);
	r->pt_joy = point_flag(r, "ReadJoypad", P_JOY, NULL);
	r->pt_start = point_flag(r, "StartDuel", P_START, NULL);
	if (point_flag(r, "HandleDuelMenuInput", P_VFY_DUEL, NULL) < 0 ||
	    point_flag(r, "YourOrOppPlayAreaScreen_HandleInput", P_VFY_CURSOR, NULL) < 0 ||
	    point_flag(r, "HandleYesOrNoMenu.a_pressed", P_VFY_YESNO, NULL) < 0)
		goto fail;
	if (r->pt_trap < 0 || r->pt_reset < 0 || r->pt_ai < 0 || r->pt_rng < 0 || r->pt_joy < 0 || r->pt_start < 0)
		goto fail;
	/* input routines: entry and ready (poll loop) points */
	for (int i = 0; i < NUM_INPUT_ROUTINES; i++) {
		const InputRoutine *ir = &kInputRoutines[i];
		for (int role = 1; role <= 2; role++) {
			if (i == GENERIC_MENU && role == 2)
				continue;
			const Sym *sy = find_sym(r, role == 1 ? ir->entry : ir->ready);
			if (!sy) {
				fprintf(stderr, "ref: symbol %s not found\n", role == 1 ? ir->entry : ir->ready);
				goto fail;
			}
			int pi = point_get(r, sy->bank, sy->addr);
			if (pi < 0)
				goto fail;
			Point *p = &r->pts[pi];
			int code = i << 2 | (i == GENERIC_MENU ? 3 : role);
			int dup = 0;
			for (int k = 0; k < p->ninput; k++)
				if ((p->input_roles[k] & 3) == 2 && role == 2)
					dup = 1;   /* one ready role per label is enough */
			if (!dup && p->ninput < (int)(sizeof p->input_roles / sizeof p->input_roles[0]))
				p->input_roles[p->ninput++] = (short)code;
			p->flags |= P_INPUT;
		}
	}
	return r;
fail:
	ref_free(r);
	return NULL;
}

void ref_free(Ref *r)
{
	if (!r)
		return;
	if (r->gb)
		gb_free(r->gb);
	for (int i = 0; i < r->nsyms; i++)
		free(r->syms[i].name);
	free(r->syms);
	free(r->hash);
	for (int b = 0; b < 0x200; b++)
		free(r->bysym[b]);
	free(r->snap);
	free(r->pts);
	for (int i = 0; i < r->ntraces; i++)
		free(r->traces[i].name);
	free(r->traces);
	free(r->screen_dir);
	free(r);
}

int ref_trace_add_addr(Ref *r, int bank, uint16_t addr, const char *label)
{
	if (r->ntraces == r->captraces) {
		int nc = r->captraces ? r->captraces * 2 : 32;
		Trace *n = realloc(r->traces, sizeof *n * (size_t)nc);
		if (!n)
			return -1;
		r->traces = n;
		r->captraces = nc;
	}
	int pi = point_get(r, bank, addr);
	if (pi < 0)
		return -1;
	Point *p = &r->pts[pi];
	if (p->ntrace >= (int)(sizeof p->trace_ids / sizeof p->trace_ids[0]))
		return -1;
	int id = r->ntraces++;
	Trace *t = &r->traces[id];
	char buf[128];
	t->name = xstrdup(label ? label : ref_addr_name(r, bank, addr, buf, sizeof buf));
	t->bank = norm_bank(bank, addr);
	t->addr = addr;
	t->point = pi;
	t->active = 1;
	p->trace_ids[p->ntrace++] = id;
	return id;
}

int ref_trace_add(Ref *r, const char *name)
{
	const Sym *s = find_sym(r, name);
	if (!s || s->addr >= 0x8000)
		return -1;
	return ref_trace_add_addr(r, s->bank, s->addr, s->name);
}

void ref_trace_clear(Ref *r)
{
	for (int i = 0; i < r->ntraces; i++) {
		Trace *t = &r->traces[i];
		if (!t->active)
			continue;
		Point *p = &r->pts[t->point];
		p->ntrace = 0;
		point_maybe_free(r, t->point);
		t->active = 0;
	}
}

void ref_trace_rng(Ref *r, int on) { r->rng_trace = on; }

void ref_set_screens(Ref *r, const char *dir, int every)
{
	free(r->screen_dir);
	r->screen_dir = dir ? xstrdup(dir) : NULL;
	r->screen_every = dir ? every : 0;
}

int ref_start_duel(Ref *r, const RefDuel *d)
{
	GB *gb = r->gb;
	r->duel = *d;
	/* decks as CopyDeckData leaves them: all cards of the ROM list (61 for
	 * GRASS_AND_PSYCHIC_DECK: the 61st lands in the unused rest of the $80-byte
	 * buffer), or the 60 given card ids */
	uint8_t pdeck[0x80], odeck[0x80];
	uint16_t pname = 0, oname = 0;
	int pn = REF_DECK_SIZE, on = REF_DECK_SIZE;
	memset(pdeck, 0, sizeof pdeck);
	memset(odeck, 0, sizeof odeck);
	if (d->player_deck[0])
		memcpy(pdeck, d->player_deck, REF_DECK_SIZE);
	else if ((pn = rom_deck_raw(r, d->player_deck_id + 2, pdeck, &pname)) < REF_DECK_SIZE) {
		fprintf(stderr, "ref: invalid player deck id %d\n", d->player_deck_id);
		return -1;
	}
	if (d->opp_deck[0])
		memcpy(odeck, d->opp_deck, REF_DECK_SIZE);
	else if ((on = rom_deck_raw(r, d->opp_deck_id + 2, odeck, &oname)) < REF_DECK_SIZE) {
		fprintf(stderr, "ref: invalid opponent deck id %d\n", d->opp_deck_id);
		return -1;
	}
	if (pn > 0x80)
		pn = 0x80;
	if (on > 0x80)
		on = 0x80;

	/* machine back to the title screen snapshot */
	gb_load_state(gb, r->snap);
	gb->bp_skip_n = 0;
	gb->hit_breakpoint = -1;
	gb->render = r->screen_dir ? 1 : 0;
	watch_clear(r);
	r->npend = r->pend_head = 0;
	r->nsess = 0;
	r->waiting = -1;
	r->last_buttons = 0;
	r->start_patched = 0;

	/* SRAM settings (bank 0) */
	if (d->player_human) {
		/* the player's deck is the selected saved deck (LoadPlayerDeck) */
		*sram0(r, r->s_CurrentlySelectedDeck) = 0;
		memcpy(sram0(r, r->s_Deck1Cards), pdeck, REF_DECK_SIZE);
	}
	*sram0(r, r->s_TextSpeed) = d->text_speed;
	*sram0(r, r->s_AnimationsDisabled) = d->animations_disabled;
	*sram0(r, r->s_SkipDelayAllowed) = 0;

	/* WRAM. AI vs AI: what StartDuel_VSAIOpp's LoadPlayerDeck/LoadOpponentDeck
	 * would leave (decks; wDeckName = name of the deck loaded last, the
	 * opponent's). Human mode: StartDuel_VSAIOpp loads them itself. */
	if (!d->player_human) {
		memcpy(wram(r, r->w_PlayerDeck), pdeck, (size_t)pn);
		memcpy(wram(r, r->w_OpponentDeck), odeck, (size_t)on);
		if (!d->opp_deck[0]) {
			wram(r, r->w_DeckName)[0] = (uint8_t)oname;
			wram(r, r->w_DeckName)[1] = (uint8_t)(oname >> 8);
		}
	}
	gb->wram[((PLAYER_TURN << 8) | DUELVARS_DUELIST_TYPE) - 0xc000] =
		d->player_human ? DUELIST_TYPE_PLAYER : (uint8_t)(DUELIST_TYPE_AI_OPP | d->player_deck_id);
	gb->wram[((OPPONENT_TURN << 8) | DUELVARS_DUELIST_TYPE) - 0xc000] =
		(uint8_t)(DUELIST_TYPE_AI_OPP | d->opp_deck_id);
	*wram(r, r->w_OpponentDeckID) = d->opp_deck_id;
	*wram(r, r->w_NPCDuelDeckID) = d->opp_deck_id;
	*wram(r, r->w_NPCDuelPrizes) = d->prizes;
	*wram(r, r->w_DuelTheme) = d->duel_theme;
	*wram(r, r->w_IsPracticeDuel) = 0;
	wram(r, r->w_OpponentName)[0] = (uint8_t)d->opp_name;
	wram(r, r->w_OpponentName)[1] = (uint8_t)(d->opp_name >> 8);
	*wram(r, r->w_OpponentPortrait) = d->opp_portrait;
	memcpy(wram(r, r->w_RNG1), d->rng, 3);
	*wram(r, r->w_AnimationsDisabled) = d->animations_disabled;
	*wram(r, r->w_TextSpeed) = d->text_speed;
	*wram(r, r->w_SkipDelayAllowed) = 0;

	/* HRAM */
	*hram(r, r->h_WhoseTurn) = PLAYER_TURN;
	*hram(r, r->h_DPadRepeat) = 0;
	*hram(r, r->h_KeysReleased) = 0;
	*hram(r, r->h_DPadHeld) = 0;
	*hram(r, r->h_KeysHeld) = 0;
	*hram(r, r->h_KeysPressed) = 0;
	gb_set_buttons(gb, 0);

	/* ROM bank 1 (BANK(StartDuel)) */
	*hram(r, r->h_BankROM) = (uint8_t)r->b_StartDuel;
	gb_write(gb, 0x2000, (uint8_t)r->b_StartDuel);
	gb_write(gb, 0x3000, 0);

	/* stack: trap return address at $dffe, SP = $dffe */
	gb->sp = 0xe000 - 2;
	gb_write(gb, 0xdffe, (uint8_t)r->a_PowersOf2);
	gb_write(gb, 0xdfff, (uint8_t)(r->a_PowersOf2 >> 8));
	gb->pc = d->player_human ? r->a_StartDuel_VSAIOpp : r->a_StartDuel;
	if (d->continue_duel) {
		memcpy(sram0(r, r->s_CurrentDuel), d->save, REF_SAVE_SIZE / 2);
		memcpy(&gb->sram[r->b_BackupCurrentDuel * 0x2000 + (r->s_BackupCurrentDuel - 0xa000)],
		       d->save + REF_SAVE_SIZE / 2, REF_SAVE_SIZE / 2);
		gb->pc = r->a_TryContinueDuel;
	}
	gb->halted = 0;
	gb->halt_bug = 0;
	gb->ei_pending = 0;
	gb->ime = 1;
	gb->cs_depth = 0;
	gb->cs_dropped = 0;

	r->start_frames = gb->frames;
	r->start_cycles = gb->cycles;
	r->last_progress = gb->cycles;
	r->next_screen = gb->frames;
	r->running = 1;
	if (!r->duel.hang_frames)
		r->duel.hang_frames = 3600;
	return 0;
}

int ref_next(Ref *r, RefEvent *ev)
{
	GB *gb = r->gb;
	for (;;) {
		if (r->npend) {
			Pending *p = &r->pend[r->pend_head];
			r->pend_head = (r->pend_head + 1) % MAX_PENDING;
			r->npend--;
			fill_event(r, ev, p);
			return ev->kind;
		}
		if (r->running != 1) {
			if (r->running == 0) {
				Pending p = { 0 };
				p.kind = REF_EV_ERROR;
				p.name = "error";
				snprintf(p.msg, sizeof p.msg, "no duel started");
				fill_event(r, ev, &p);
				return ev->kind;
			}
			fill_event(r, ev, &r->last);
			return ev->kind;
		}
		if (r->waiting >= 0) {
			Pending p = { 0 };
			p.kind = REF_EV_ERROR;
			p.name = "error";
			snprintf(p.msg, sizeof p.msg, "ref_next called with an unanswered input");
			fill_event(r, ev, &p);
			return ev->kind;
		}
		if (gb->cycles - r->last_progress > (uint64_t)r->duel.hang_frames * CYCLES_PER_FRAME) {
			char st[400];
			stack_string(r, st, sizeof st);
			char pc[128];
			ref_addr_name(r, gb_rom_bank(gb), gb->pc, pc, sizeof pc);
			stop_duel(r, REF_EV_HANG, "no progress for %u frames; pc=%s stack: %s", r->duel.hang_frames, pc, st);
			continue;
		}
		if (r->screen_every > 0 && gb->frames >= r->next_screen) {
			char path[1024];
			snprintf(path, sizeof path, "%s/%07llu.png", r->screen_dir,
				(unsigned long long)(gb->frames - r->start_frames));
			gb_write_png(gb, path);
			r->next_screen = gb->frames + (uint64_t)r->screen_every;
		}
		int res = gb_run(gb, CYCLES_PER_FRAME, 1);
		if (res == GB_STOP_ERROR) {
			char st[400];
			stack_string(r, st, sizeof st);
			stop_duel(r, REF_EV_ERROR, "emulator error: %s; stack: %s", gb_error_string(gb), st);
			continue;
		}
		if (res == GB_STOP_BREAKPOINT)
			handle_stop(r);
	}
}

int ref_answer(Ref *r, int choice)
{
	if (r->running != 1 || r->waiting < 0 || r->waiting >= r->nsess)
		return -1;
	Session *s = &r->sess[r->waiting];
	const RefInput *in = &s->in;
	if (choice == REF_CANCEL) {
		if (!in->can_cancel)
			return -1;
	} else {
		if (choice < in->first || choice >= in->first + in->num_items)
			return -1;
		if (in->type == REF_IN_PRIZE || in->type == REF_IN_PEEK) {
			int ok = 0;
			for (int i = 0; i < in->list_len; i++)
				if (in->list[i] == choice)
					ok = 1;
			if (!ok)
				return -1;
		}
	}
	s->answer = choice;
	s->state = S_ACTIVE;
	s->steps = 0;
	s->verify = 0;
	r->waiting = -1;
	return 0;
}

void ref_print_stack(Ref *r, FILE *fp)
{
	GB *gb = r->gb;
	char nb[128];
	fprintf(fp, "  pc=%s (%02x:%04x) sp=%04x depth=%d\n",
		ref_addr_name(r, gb_rom_bank(gb), gb->pc, nb, sizeof nb), gb_bank_of(gb, gb->pc) & 0xff, gb->pc,
		gb->sp, gb->cs_depth);
	for (int i = gb->cs_depth - 1; i >= 0; i--) {
		const GBFrame *fr = &gb->cs[i];
		int bank;
		uint16_t target;
		frame_callee(r, fr, &bank, &target);
		fprintf(fp, "  [%2d] %s%-40s ret=%04x sp=%04x bank=%02x\n", i, fr->is_interrupt ? "IRQ " : "",
			ref_addr_name(r, bank, target, nb, sizeof nb), fr->ret, fr->sp, fr->bank);
	}
}

const char *ref_input_type_name(int t)
{
	static const char *const n[] = { "?", "ACK", "DUEL_MENU", "CARD_LIST", "CARD_ITEM_MENU", "PLAY_AREA",
		"MENU", "YES_NO", "ENERGY_DISCARD", "PRIZE", "COLOR", "NUMBER", "PEEK" };
	return t >= 0 && t < (int)(sizeof n / sizeof n[0]) ? n[t] : "?";
}

const char *ref_event_kind_name(int k)
{
	static const char *const n[] = { "?", "TRACE", "RNG", "INPUT", "END", "HANG", "ERROR" };
	return k >= 0 && k < (int)(sizeof n / sizeof n[0]) ? n[k] : "?";
}
