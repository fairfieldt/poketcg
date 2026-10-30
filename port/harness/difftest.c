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
static int verbose, strict, human;
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

static int is_traced(const char *fn)
{
	return bsearch(&fn, names, (size_t)nnames, sizeof(*names), name_cmp) != NULL;
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
	if (!is_traced(fn))
		return;
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

static int choose(const TcgRequest *r, const RefInput *in)
{
	int n = in->num_items >= 0 ? in->num_items : r->num_items;
	switch (r->type) {
	case TCG_REQ_DUEL_MENU: {
		static const int weights[6] = { 40, 30, 0, 8, 7, 15 };
		int t = (int)policy(100);
		for (int i = 0; i < 6; i++) {
			if (t < weights[i])
				return i;
			t -= weights[i];
		}
		return 5;
	}
	case TCG_REQ_YES_NO:
		return policy(4) != 0;
	case TCG_REQ_CARD_ITEM_MENU:
		return 0;
	case TCG_REQ_PLAY_AREA:
		if (in->can_cancel && policy(10) == 0)
			return TCG_CANCEL;
		return r->arg[0] + (int)policy((unsigned)(n > 0 ? n : 1));
	default:
		if (in->can_cancel && (n <= 0 || policy(8) == 0))
			return TCG_CANCEL;
		return (int)policy((unsigned)(n > 0 ? n : 1));
	}
}

static int on_request(void *ctx, const TcgRequest *r)
{
	(void)ctx;
	if (r->type == TCG_REQ_ACK)
		return 0;
	if (!human)
		fail("AI-vs-AI duel: unexpected request type %d from the port", r->type);
	RefEvent ev;
	next_ref_event(&ev);
	if (ev.kind != REF_EV_INPUT) {
		print_ref_stack();
		fail("divergence: the port asked for input (type %d), the original reached %s %s", r->type,
		     ref_event_kind_name(ev.kind), ev.name);
	}
	if (ev.input.type != r->type)
		fail("input type differs: port %d, original %d (%s)", r->type, ev.input.type, ev.input.routine);
	int c = choose(r, &ev.input);
	if (ref_answer(ref, c) < 0)
		fail("the original rejected answer %d to %s", c, ev.input.routine);
	if (verbose)
		fprintf(stderr, "  input %s -> %d\n", ev.input.routine, c);
	return c;
}

static void on_event(void *ctx, const TcgEvent *ev)
{
	(void)ctx;
	(void)ev;
}

/* ---------------------------------------------------------------- driver */

static int run_duel(int pdeck, int odeck, const u8 seed[3])
{
	RefDuel d;
	ref_duel_defaults(&d);
	d.player_human = (uint8_t)human;
	d.player_deck_id = (uint8_t)pdeck;
	d.opp_deck_id = (uint8_t)odeck;
	memcpy(d.rng, seed, 3);
	events = rng_events = 0;
	have_lookahead = 0;
	policy_state = (unsigned long)(seed[0] | seed[1] << 8 | seed[2] << 16) * 2654435761u + 7;
	if (ref_start_duel(ref, &d) < 0) {
		fprintf(stderr, "ref_start_duel failed\n");
		return -1;
	}
	if (setjmp(duel_jmp)) {
		printf("FAIL decks %d vs %d seed %02x%02x%02x after %lu events (%lu rng)\n%s\n",
		       pdeck, odeck, seed[0], seed[1], seed[2], events, rng_events, fail_msg);
		return 1;
	}
	/* the first event is the duel entry point: take the original's memory image */
	const char *entry = human ? "StartDuel_VSAIOpp" : "StartDuel";
	next_ref_event(&lookahead);
	if (lookahead.kind != REF_EV_TRACE || strcmp(lookahead.name, entry))
		fail("expected the duel to start at %s, got %s %s", entry, ref_event_kind_name(lookahead.kind), lookahead.name);
	have_lookahead = 1;
	memcpy(gWRAM, lookahead.wram, WRAM_SIZE);
	memcpy(gHRAM, lookahead.hram, HRAM_SIZE);
	gPerSideAI = !human;
	if (human) {
		memcpy(gPlayerDeckCards, d.player_deck, DECK_SIZE);
		StartDuel_VSAIOpp();
	} else {
		StartDuel();
	}
	RefEvent ev;
	next_ref_event(&ev);
	if (ev.kind != REF_EV_END)
		fail("the port's duel ended, the original reached %s %s", ref_event_kind_name(ev.kind), ev.name);
	compare_state(&ev, "end of duel");
	printf("ok   decks %d vs %d seed %02x%02x%02x: %lu events, %lu rng, %d turns, result %d\n", pdeck, odeck,
	       seed[0], seed[1], seed[2], events, rng_events, ev.duel_turns, ev.duel_result);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s ROM SYM TRACE_POINTS [--decks P O] [--seeds FIRST COUNT] [--human] [--strict] [--verbose]\n",
		        argv[0]);
		return 1;
	}
	int pdeck = CHARMANDER_AND_FRIENDS_DECK_ID, odeck = SQUIRTLE_AND_FRIENDS_DECK_ID;
	unsigned first = 1, count = 1;
	const char *screens = NULL;
	int all_pairs = 0;
	for (int i = 4; i < argc; i++) {
		if (!strcmp(argv[i], "--decks") && i + 2 < argc) {
			pdeck = atoi(argv[++i]);
			odeck = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--seeds") && i + 2 < argc) {
			first = (unsigned)strtoul(argv[++i], NULL, 0);
			count = (unsigned)strtoul(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "--all-pairs")) {
			all_pairs = 1;
		} else if (!strcmp(argv[i], "--human")) {
			human = 1;
		} else if (!strcmp(argv[i], "--strict")) {
			strict = 1;
		} else if (!strcmp(argv[i], "--verbose")) {
			verbose = 1;
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
	TcgHost host = { on_event, on_request, NULL, NULL };
	tcg_set_host(&host);
	int fails = 0, runs = 0;
	for (unsigned s = first; s < first + count; s++) {
		u8 seed[3] = { (u8)(s * 37), (u8)(s * 101 + 13), (u8)(s >> 8) };
		if (all_pairs) {
			/* every AI deck against a rotating opponent (deck ids 1..NUM_DECK_IDS-1) */
			int p = 1 + (int)(s % (NUM_DECK_IDS - 1));
			int o = 1 + (int)((s * 7 + 3) % (NUM_DECK_IDS - 1));
			fails += run_duel(p, o, seed) != 0;
		} else {
			fails += run_duel(pdeck, odeck, seed) != 0;
		}
		runs++;
	}
	printf("%d/%d duels identical\n", runs - fails, runs);
	return fails != 0;
}
