/*
 * difftest: runs duels in the original ROM (inside the emulator, via ref.c)
 * and in the C port in lockstep, and stops at the first divergence.
 *
 * Sync points: every routine of the port that calls TRACE_FN() (the list comes
 * from tools/trace_points.py) is also a breakpoint in the original, and every
 * UpdateRNGSources result is compared. At each sync point the port and the
 * original must have reached the same routine, with identical engine state
 * (the compared RAM ranges are listed in `ranges`). Answers for a human
 * duelist come from a seeded random policy and are fed to both sides.
 *
 *   difftest ROM SYM TRACE_POINTS [--decks P O] [--seeds FIRST COUNT] [--human]
 *            [--strict] [--verbose] [--screens DIR]
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine.h"
#include "../src/gen/cards.h"
#undef GB /* card set constant; clashes with the emulator type */
#include "ref.h"

static Ref *ref;

/* WRAM/HRAM label names for diff reports (from the .sym file) */
static struct { uint16_t addr; char name[64]; } *ram_syms;
static int n_ram_syms;

static void load_ram_syms(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	char line[256];
	ram_syms = calloc(4096, sizeof(*ram_syms));
	while (fgets(line, sizeof(line), f) && n_ram_syms < 4096) {
		unsigned b, a;
		char name[64];
		if (sscanf(line, "%x:%x %63s", &b, &a, name) == 3 && a >= 0xc000 && !strchr(name, '.')) {
			ram_syms[n_ram_syms].addr = (uint16_t)a;
			strcpy(ram_syms[n_ram_syms].name, name);
			n_ram_syms++;
		}
	}
	fclose(f);
}

static void ram_name(uint16_t a, char *buf, size_t n)
{
	int best = -1;
	for (int i = 0; i < n_ram_syms; i++)
		if (ram_syms[i].addr <= a && (best < 0 || ram_syms[i].addr > ram_syms[best].addr))
			best = i;
	if (best < 0)
		snprintf(buf, n, "?");
	else if (ram_syms[best].addr == a)
		snprintf(buf, n, "%s", ram_syms[best].name);
	else
		snprintf(buf, n, "%s+%d", ram_syms[best].name, a - ram_syms[best].addr);
}
static jmp_buf duel_jmp;
static char fail_msg[4096];
static int verbose, strict, human, random_decks, prizes;
static int continue_at = -1;          /* --continue-at: turn whose saved duel is continued */
static u8 saved_duel[DUEL_SAVE_FILE_SIZE]; /* host->save file captured at that turn */
static int have_saved_duel;
static u8 custom_opp_deck[DECK_SIZE]; /* human mode: replaces wOpponentDeck at StartDuel, like the reference */
static int have_custom_opp_deck;
static unsigned long events, rng_events;
static RefEvent lookahead;
static int have_lookahead;
static unsigned long policy_state;

/* ---------------------------------------------------------------- trace names */

static char **names;
static int nnames;

static int name_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static unsigned long *hits; /* per trace point: verified hits (--coverage) */

/* index of fn in names, or -1 */
static int trace_index(const char *fn)
{
	char **p = bsearch(&fn, names, (size_t)nnames, sizeof(*names), name_cmp);
	return p ? (int)(p - names) : -1;
}

static void load_trace_points(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		perror(path);
		exit(1);
	}
	char line[256];
	names = calloc(4096, sizeof(*names));
	while (fgets(line, sizeof(line), f) && nnames < 4096) {
		line[strcspn(line, "\r\n")] = 0;
		if (!line[0])
			continue;
		if (ref_trace_add(ref, line) < 0)
			continue; /* port-only routine (no such ROM symbol) */
		names[nnames] = malloc(strlen(line) + 1);
		strcpy(names[nnames++], line);
	}
	fclose(f);
	qsort(names, (size_t)nnames, sizeof(*names), name_cmp);
	hits = calloc((size_t)nnames, sizeof(*hits));
}

/* --coverage: how often each sync point was reached with identical state */
static void write_coverage(const char *path)
{
	FILE *f = fopen(path, "w");
	if (!f) {
		perror(path);
		return;
	}
	int reached = 0;
	for (int i = 0; i < nnames; i++)
		reached += hits[i] != 0;
	fprintf(f, "# %d/%d trace points reached\n", reached, nnames);
	for (int i = 0; i < nnames; i++)
		fprintf(f, "%lu %s\n", hits[i], names[i]);
	fclose(f);
}

/* ---------------------------------------------------------------- failure reporting */

static void fail(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(fail_msg, sizeof(fail_msg), fmt, ap);
	va_end(ap);
	longjmp(duel_jmp, 1);
}

void tcg_fatal(const char *msg)
{
	fail("port tcg_fatal: %s", msg);
}

/* ---------------------------------------------------------------- state comparison */

struct Range { u16 start, end; const char *what; int strict_only; };

/* engine state compared at every sync point; addresses inclusive */
static const struct Range ranges[] = {
	{ 0xc200, 0xc2ff, "player duel vars", 0 },
	{ 0xc300, 0xc3ff, "opponent duel vars", 0 },
	{ 0xc400, 0xc4ff, "decks", 0 },
	{ ADDR_wDuelTempList, ADDR_wDuelTempList + 0x7f, "wDuelTempList", 1 },
	{ ADDR_wRNG1, ADDR_wRNGCounter, "RNG", 0 },
	{ ADDR_wDuelStates, ADDR_wDuelStatesEnd - 1, "duel states", 0 },
	{ ADDR_wAttachedEnergies, ADDR_wSkipDelayAllowed, "duel temps", 1 },
	{ ADDR_wAIDuelVars, ADDR_wAIDuelVarsEnd - 1, "AI duel vars", 0 },
	{ 0xff97, 0xffa7, "HRAM temps", 0 },
};

static const u8 *ref_byte_ptr(const RefEvent *ev, u16 addr)
{
	if (addr >= 0xff80)
		return &ev->hram[addr - 0xff80];
	return &ev->wram[addr - 0xc000];
}

static void compare_state(const RefEvent *ev, const char *where)
{
	char buf[2048];
	int n = 0, bad = 0;
	buf[0] = 0;
	for (unsigned r = 0; r < sizeof(ranges) / sizeof(*ranges); r++) {
		if (ranges[r].strict_only && !strict)
			continue;
		for (unsigned a = ranges[r].start; a <= ranges[r].end; a++) {
			u8 p = *gbptr((u16)a), o = *ref_byte_ptr(ev, (u16)a);
			if (p == o)
				continue;
			if (bad++ < 24 && n < (int)sizeof(buf) - 128) {
				char name[96];
				ram_name((uint16_t)a, name, sizeof(name));
				n += snprintf(buf + n, sizeof(buf) - (size_t)n, "    %04x %-40s port %02x original %02x\n",
				              a, name, p, o);
			}
		}
	}
	if (bad)
		fail("state differs at %s (%d bytes):\n%s", where, bad, buf);
}

/* ---------------------------------------------------------------- the original */

static void next_ref_event(RefEvent *ev)
{
	if (have_lookahead) {
		*ev = lookahead;
		have_lookahead = 0;
		return;
	}
	ref_next(ref, ev);
	events++;
	if (ev->kind == REF_EV_HANG || ev->kind == REF_EV_ERROR)
		fail("original: %s: %s", ref_event_kind_name(ev->kind), ev->msg);
}

static void print_ref_stack(void)
{
	ref_print_stack(ref, stderr);
}

void tcg_trace_enter(const char *fn)
{
	int ti = trace_index(fn);
	if (ti < 0)
		return;
	if (have_custom_opp_deck && !strcmp(fn, "StartDuel")) {
		/* human mode: the reference puts the custom opponent deck in place at StartDuel */
		memcpy(wOpponentDeck, custom_opp_deck, DECK_SIZE);
		have_custom_opp_deck = 0;
	}
	RefEvent ev;
	next_ref_event(&ev);
	if (verbose)
		fprintf(stderr, "  [%lu] %s\n", events, fn);
	if (ev.kind != REF_EV_TRACE || strcmp(ev.name, fn)) {
		print_ref_stack();
		fail("divergence: the port entered %s, the original reached %s %s", fn,
		     ref_event_kind_name(ev.kind), ev.name);
	}
	compare_state(&ev, fn);
	hits[ti]++;
}

void tcg_trace_rng(u8 value)
{
	RefEvent ev;
	next_ref_event(&ev);
	rng_events++;
	if (ev.kind != REF_EV_RNG) {
		print_ref_stack();
		fail("divergence: the port called UpdateRNGSources (-> %02x), the original reached %s %s",
		     value, ref_event_kind_name(ev.kind), ev.name);
	}
	if (ev.rng_value != value || ev.rng[0] != wRNG1 || ev.rng[1] != wRNG2 || ev.rng[2] != wRNGCounter) {
		print_ref_stack();
		fail("RNG differs: port %02x [%02x %02x %02x], original %02x [%02x %02x %02x]", value,
		     wRNG1, wRNG2, wRNGCounter, ev.rng_value, ev.rng[0], ev.rng[1], ev.rng[2]);
	}
}

/* ---------------------------------------------------------------- the human duelist */

static unsigned policy(unsigned n)
{
	policy_state = policy_state * 6364136223846793005ULL + 1442695040888963407ULL;
	return n ? (unsigned)((policy_state >> 33) % n) : 0;
}

/* a random valid answer; the original's input description is the authority on what
 * is selectable (first .. first + num_items - 1, list of valid slots, cancel) */
static int choose(const TcgRequest *r, const RefInput *in)
{
	if (in->view_only)
		return TCG_CANCEL;
	if (r->type == TCG_REQ_DUEL_MENU) {
		static const int weights[6] = { 40, 30, 0, 8, 7, 15 };
		int t = (int)policy(100);
		for (int i = 0; i < 6; i++) {
			if (t < weights[i])
				return i;
			t -= weights[i];
		}
		return 5;
	}
	if (r->type == TCG_REQ_YES_NO)
		return policy(4) != 0;
	if (r->type == TCG_REQ_CONFIRM)
		return policy(3) == 0 ? TCG_CANCEL : 0;
	if (r->type == TCG_REQ_CARD_ITEM_MENU)
		return policy(12) == 0 ? 1 : 0;
	if (in->can_cancel && (in->num_items <= 0 || policy(10) == 0))
		return TCG_CANCEL;
	if ((in->type == REF_IN_PRIZE || in->type == REF_IN_PEEK) && in->list_len > 0)
		return in->list[policy((unsigned)in->list_len)];
	if (in->num_items <= 0)
		return TCG_CANCEL;
	return in->first + (int)policy((unsigned)in->num_items);
}

static int on_request(void *ctx, const TcgRequest *r)
{
	(void)ctx;
	if (r->type == TCG_REQ_ACK)
		return 0;
	/* AI-vs-AI duels only reach screens shown to the human: view-only card lists
	 * (the hand shown by Lass) and A/B waits (the play area changed by an effect) */
	if (!human && !(r->type == TCG_REQ_CARD_LIST && r->arg[0]) && r->type != TCG_REQ_CONFIRM)
		fail("AI-vs-AI duel: unexpected request type %d from the port", r->type);
	RefEvent ev;
	next_ref_event(&ev);
	if (ev.kind != REF_EV_INPUT) {
		print_ref_stack();
		fail("divergence: the port asked for input (type %d), the original reached %s %s", r->type,
		     ref_event_kind_name(ev.kind), ev.name);
	}
	bool peek = ev.input.type == REF_IN_PEEK && r->type == TCG_REQ_MENU && r->menu == TCG_MENU_PEEK_TARGET;
	bool confirm = ev.input.type == REF_IN_CONFIRM && r->type == TCG_REQ_CONFIRM;
	if (ev.input.type != r->type && !peek && !confirm)
		fail("input type differs: port %d, original %d (%s)", r->type, ev.input.type, ev.input.routine);
	int c = choose(r, &ev.input);
	if (ref_answer(ref, c) < 0)
		fail("the original rejected answer %d to %s", c, ev.input.routine);
	if (verbose)
		fprintf(stderr, "  input %s -> %d\n", ev.input.routine, c);
	return c;
}

/* host->save: keep the first save of turn continue_at (the duel menu of that turn) */
static void on_save(void *ctx, const uint8_t *data, uint32_t size)
{
	(void)ctx;
	if (continue_at >= 0 && !have_saved_duel && wDuelTurns >= continue_at && size == DUEL_SAVE_FILE_SIZE) {
		memcpy(saved_duel, data, DUEL_SAVE_FILE_SIZE);
		have_saved_duel = 1;
	}
}

static void on_event(void *ctx, const TcgEvent *ev)
{
	(void)ctx;
	(void)ev;
}

/* ---------------------------------------------------------------- random decks */

/* --random-decks: 10 random Basic Pokemon, 30 other random Pokemon/Trainer cards and
 * 20 random energy cards, to reach cards that no AI deck has. GAMBLER and RECYCLE are
 * left out: their card records end past their ROM bank, and the bytes the original
 * reads there come from VRAM (screen dependent; the port reads 0). */
static void random_deck(u8 *out)
{
	int n = 0;
	while (n < DECK_SIZE) {
		u8 id = (u8)(1 + policy(NUM_CARDS));
		const struct CardData *c = (const struct CardData *)gCardData[id];
		if (id == GAMBLER || id == RECYCLE)
			continue;
		bool energy = c->Type >= TYPE_ENERGY && c->Type < TYPE_TRAINER;
		bool basic = c->Type < TYPE_ENERGY && c->Stage == BASIC;
		if (n < 10 ? basic : n < 40 ? !energy : energy)
			out[n++] = id;
	}
}

/* ---------------------------------------------------------------- driver */

/* resume: continue the duel saved in saved_duel instead of starting one */
static int run_duel(int pdeck, int odeck, const u8 seed[3], int resume)
{
	RefDuel d;
	ref_duel_defaults(&d);
	if (resume) {
		d.continue_duel = 1;
		memcpy(d.save, saved_duel, DUEL_SAVE_FILE_SIZE);
	} else {
		have_saved_duel = 0;
	}
	d.player_human = (uint8_t)human;
	d.player_deck_id = (uint8_t)pdeck;
	d.opp_deck_id = (uint8_t)odeck;
	if (prizes)
		d.prizes = (uint8_t)prizes;
	memcpy(d.rng, seed, 3);
	events = rng_events = 0;
	have_lookahead = 0;
	policy_state = (unsigned long)(seed[0] | seed[1] << 8 | seed[2] << 16) * 2654435761u + 7;
	have_custom_opp_deck = 0;
	if (random_decks) {
		random_deck(d.player_deck);
		random_deck(d.opp_deck);
	}
	if (ref_start_duel(ref, &d) < 0) {
		fprintf(stderr, "ref_start_duel failed\n");
		return -1;
	}
	const char *what = resume ? " (continued)" : "";
	if (setjmp(duel_jmp)) {
		printf("FAIL decks %d vs %d seed %02x%02x%02x%s after %lu events (%lu rng)\n%s\n",
		       pdeck, odeck, seed[0], seed[1], seed[2], what, events, rng_events, fail_msg);
		return 1;
	}
	/* the first event is the duel entry point: take the original's memory image */
	const char *entry = resume ? "TryContinueDuel" : human ? "StartDuel_VSAIOpp" : "StartDuel";
	next_ref_event(&lookahead);
	if (lookahead.kind != REF_EV_TRACE || strcmp(lookahead.name, entry))
		fail("expected the duel to start at %s, got %s %s", entry, ref_event_kind_name(lookahead.kind), lookahead.name);
	have_lookahead = 1;
	memcpy(gWRAM, lookahead.wram, WRAM_SIZE);
	memcpy(gHRAM, lookahead.hram, HRAM_SIZE);
	gPerSideAI = !human;
	if (resume) {
		memcpy(gDuelSave, saved_duel, DUEL_SAVE_FILE_SIZE);
		if (TryContinueDuel())
			fail("the port could not continue the saved duel");
	} else if (human) {
		/* the original's LoadPlayerDeck copies the deck the reference put in SRAM
		 * (ROM deck pdeck): expand the same deck with the port's LoadDeck */
		hWhoseTurn = PLAYER_TURN;
		LoadDeck((u8)(pdeck + 2));
		memcpy(gPlayerDeckCards, d.player_deck[0] ? d.player_deck : wPlayerDeck, DECK_SIZE);
		memcpy(gWRAM, lookahead.wram, WRAM_SIZE);
		memcpy(gHRAM, lookahead.hram, HRAM_SIZE);
		if (d.opp_deck[0]) {
			memcpy(custom_opp_deck, d.opp_deck, DECK_SIZE);
			have_custom_opp_deck = 1;
		}
		StartDuel_VSAIOpp();
	} else {
		StartDuel();
	}
	RefEvent ev;
	next_ref_event(&ev);
	if (ev.kind != REF_EV_END)
		fail("the port's duel ended, the original reached %s %s", ref_event_kind_name(ev.kind), ev.name);
	compare_state(&ev, "end of duel");
	printf("ok   decks %d vs %d seed %02x%02x%02x%s: %lu events, %lu rng, %d turns, result %d\n", pdeck, odeck,
	       seed[0], seed[1], seed[2], what, events, rng_events, ev.duel_turns, ev.duel_result);
	if (!resume && continue_at >= 0) {
		if (!have_saved_duel) {
			printf("     (no save at turn %d to continue)\n", continue_at);
			return 0;
		}
		return run_duel(pdeck, odeck, seed, 1);
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s ROM SYM TRACE_POINTS [--decks P O] [--seeds FIRST COUNT | --rng XXYYZZ] [--human] [--random-decks] [--prizes N] [--continue-at TURN] [--strict] [--verbose] [--coverage FILE]\n",
		        argv[0]);
		return 1;
	}
	int pdeck = CHARMANDER_AND_FRIENDS_DECK_ID, odeck = SQUIRTLE_AND_FRIENDS_DECK_ID;
	unsigned first = 1, count = 1;
	const char *screens = NULL, *coverage = NULL;
	int all_pairs = 0, have_rng = 0;
	u8 rng[3] = { 0 };
	for (int i = 4; i < argc; i++) {
		if (!strcmp(argv[i], "--decks") && i + 2 < argc) {
			pdeck = atoi(argv[++i]);
			odeck = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--seeds") && i + 2 < argc) {
			first = (unsigned)strtoul(argv[++i], NULL, 0);
			count = (unsigned)strtoul(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "--rng") && i + 1 < argc) {
			/* the seed as printed by a FAIL line: wRNG1, wRNG2, wRNGCounter */
			unsigned long v = strtoul(argv[++i], NULL, 16);
			rng[0] = (u8)(v >> 16);
			rng[1] = (u8)(v >> 8);
			rng[2] = (u8)v;
			have_rng = 1;
		} else if (!strcmp(argv[i], "--all-pairs")) {
			all_pairs = 1;
		} else if (!strcmp(argv[i], "--continue-at") && i + 1 < argc) {
			continue_at = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--prizes") && i + 1 < argc) {
			prizes = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--random-decks")) {
			random_decks = 1;
		} else if (!strcmp(argv[i], "--human")) {
			human = 1;
		} else if (!strcmp(argv[i], "--strict")) {
			strict = 1;
		} else if (!strcmp(argv[i], "--verbose")) {
			verbose = 1;
		} else if (!strcmp(argv[i], "--coverage") && i + 1 < argc) {
			coverage = argv[++i];
		} else if (!strcmp(argv[i], "--screens") && i + 1 < argc) {
			screens = argv[++i];
		}
	}
	ref = ref_new(argv[1], argv[2]);
	if (!ref)
		return 1;
	load_trace_points(argv[3]);
	load_ram_syms(argv[2]);
	ref_trace_rng(ref, 1);
	if (screens)
		ref_set_screens(ref, screens, 600);
	TcgHost host = { on_event, on_request, on_save, NULL };
	tcg_set_host(&host);
	int fails = 0, runs = 0;
	if (have_rng) {
		fails = run_duel(pdeck, odeck, rng, 0) != 0;
		printf("%d/1 duels identical\n", 1 - fails);
		if (coverage)
			write_coverage(coverage);
		return fails;
	}
	for (unsigned s = first; s < first + count; s++) {
		u8 seed[3] = { (u8)(s * 37), (u8)(s * 101 + 13), (u8)(s >> 8) };
		if (all_pairs) {
			/* every AI deck against a rotating opponent (deck ids 1..NUM_DECK_IDS-1) */
			int p = 1 + (int)(s % (NUM_DECK_IDS - 1));
			int o = 1 + (int)((s * 7 + 3) % (NUM_DECK_IDS - 1));
			fails += run_duel(p, o, seed, 0) != 0;
		} else {
			fails += run_duel(pdeck, odeck, seed, 0) != 0;
		}
		runs++;
	}
	printf("%d/%d duels identical\n", runs - fails, runs);
	if (coverage)
		write_coverage(coverage);
	return fails != 0;
}
