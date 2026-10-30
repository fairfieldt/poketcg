/*
 * refrun - run one (or several) duels in the original ROM and print the
 * reference event stream.
 *
 *   refrun ROM SYM --decks <playerDeckId> <oppDeckId> --seed r1 r2 r3
 *          [--trace name ...] [--trace-file file] [--rng] [--screens dir [every]]
 *          [--human [policySeed]] [--acks] [--count N] [--quiet] [--anims 0|1]
 *          [--text-speed n] [--prizes n] [--no-per-side-ai] [--hang frames] [--stack]
 *          [--show label]   (print that WRAM/HRAM byte on every event line; repeatable)
 *
 * Deck ids are *_DECK_ID values (= *_DECK - 2, see src/constants/deck_constants.asm);
 * the 60-card lists are expanded from DeckPointers in the ROM like CopyDeckData.
 * Seeds are wRNG1 wRNG2 wRNGCounter (hex with 0x prefix or decimal).
 *
 * One line per event:
 *   <frame> <KIND> <name> turn=<hWhoseTurn> a=<a> rng=<wRNG1><wRNG2><wRNGCounter> [extra]
 * then a result line:
 *   RESULT finished=<wDuelFinished> result=<wDuelResult> turns=<wDuelTurns> frames=<n> ...
 *
 * --human: the PLAYER_TURN side is DUELIST_TYPE_PLAYER driven by a random but
 *          legal policy (phase 2 input driver); policySeed seeds the policy.
 * --count N: run N duels; duel k uses seed (r1, r2, r3 + k) and, with --human,
 *          policy seed + k. Prints only the result lines unless --verbose.
 */
#define _POSIX_C_SOURCE 199309L
#include "ref.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

static long num(const char *s)
{
	return strtol(s, NULL, 0);
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s ROM SYM --decks P O --seed r1 r2 r3 [--trace name ...] [--trace-file f]\n"
		"       [--rng] [--screens dir [every]] [--human [policySeed]] [--acks] [--count N]\n"
		"       [--quiet] [--verbose] [--anims 0|1] [--text-speed n] [--prizes n]\n"
		"       [--no-per-side-ai] [--hang frames] [--stack] [--show wramLabel ...]\n",
		argv0);
}

/* xorshift policy RNG (harness side, independent of the game's RNG) */
static uint32_t pol_state = 1;
static uint32_t pol_rand(void)
{
	uint32_t x = pol_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return pol_state = x;
}

/* random but legal answer for a human input request. Biased towards
 * playing cards so that card effects get exercised: duel menu Hand 40%,
 * Attack 20%, Check 3%, Pkmn Power 7%, Retreat 10%, Done 20%; item menu
 * Play/Select 80%, Check 10%, cancel 10%; elsewhere cancel 15% when allowed. */
static int policy(const RefInput *in)
{
	int n = in->num_items;
	if (in->type == REF_IN_DUEL_MENU) {
		static const int w[6] = { 40, 20, 3, 7, 10, 20 };
		int x = (int)(pol_rand() % 100), i = 0;
		while (i < 5 && x >= w[i])
			x -= w[i++];
		return i;
	}
	if (in->type == REF_IN_CARD_ITEM_MENU) {
		int x = (int)(pol_rand() % 10);
		return x < 8 ? 0 : x == 8 ? 1 : REF_CANCEL;
	}
	if (in->can_cancel && n > 0 && pol_rand() % 100 < 15)
		return REF_CANCEL;
	if (in->can_cancel && n <= 0)
		return REF_CANCEL;
	if ((in->type == REF_IN_PRIZE || in->type == REF_IN_PEEK) && in->list_len)
		return in->list[pol_rand() % (uint32_t)in->list_len];
	if (n <= 0)
		return in->first;
	return in->first + (int)(pol_rand() % (uint32_t)n);
}

static const char *show_names[32];
static uint16_t show_addr[32];
static int nshow;

static void print_event(Ref *r, const RefEvent *ev, int show_stack)
{
	printf("%7llu %-5s %-32s turn=%02x a=%02x rng=%02x%02x%02x", (unsigned long long)ev->frame,
		ref_event_kind_name(ev->kind), ev->name, ev->whose_turn,
		ev->kind == REF_EV_RNG ? ev->rng_value : ev->regs.a, ev->rng[0], ev->rng[1], ev->rng[2]);
	if (ev->kind == REF_EV_INPUT) {
		printf(" %s cursor=%d items=%d%s caller=%s", ref_input_type_name(ev->input.type), ev->input.cursor,
			ev->input.num_items, ev->input.can_cancel ? " cancel" : "", ev->input.caller ? ev->input.caller : "?");
		if (ev->input.list_len) {
			printf(" list=");
			for (int i = 0; i < ev->input.list_len; i++)
				printf("%s%02x", i ? "," : "", ev->input.list[i]);
		}
	} else if (ev->kind == REF_EV_RNG && ev->depth >= 2) {
		/* caller of UpdateRNGSources (and its caller when it is Random) */
		const RefFrame *f = &ev->stack[ev->depth - 1];
		printf(" from=%s", f->name);
		if (ev->depth >= 2 && !strcmp(f->name, "Random"))
			printf("<%s", ev->stack[ev->depth - 2].name);
	}
	for (int i = 0; i < nshow; i++) {
		uint16_t a = show_addr[i];
		uint8_t v = a >= 0xff80 ? ev->hram[a - 0xff80] : ev->wram[(a - 0xc000) & 0x1fff];
		printf(" %s=%02x", show_names[i], v);
	}
	if (ev->kind >= REF_EV_END)
		printf(" %s", ev->msg);
	printf("\n");
	if (show_stack) {
		for (int i = ev->depth - 1; i >= 0; i--)
			printf("            %s%s+%u\n", ev->stack[i].irq ? "IRQ " : "", ev->stack[i].name, ev->stack[i].off);
	}
	(void)r;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		usage(argv[0]);
		return 2;
	}
	const char *rom = argv[1], *sym = argv[2];
	RefDuel d;
	ref_duel_defaults(&d);
	int have_decks = 0, have_seed = 0, rng = 0, quiet = 0, verbose = 0, count = 1, show_stack = 0;
	uint32_t policy_seed = 1;
	const char *screens = NULL;
	int screen_every = 60;
	const char *traces[256];
	int ntraces = 0;
	const char *trace_file = NULL;

	for (int i = 3; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--decks") && i + 2 < argc) {
			d.player_deck_id = (uint8_t)num(argv[++i]);
			d.opp_deck_id = (uint8_t)num(argv[++i]);
			have_decks = 1;
		} else if (!strcmp(a, "--seed") && i + 3 < argc) {
			for (int k = 0; k < 3; k++)
				d.rng[k] = (uint8_t)num(argv[++i]);
			have_seed = 1;
		} else if (!strcmp(a, "--trace") && i + 1 < argc) {
			while (i + 1 < argc && strncmp(argv[i + 1], "--", 2) && ntraces < 256)
				traces[ntraces++] = argv[++i];
		} else if (!strcmp(a, "--trace-file") && i + 1 < argc) {
			trace_file = argv[++i];
		} else if (!strcmp(a, "--show") && i + 1 < argc && nshow < 32) {
			show_names[nshow++] = argv[++i];
		} else if (!strcmp(a, "--rng")) {
			rng = 1;
		} else if (!strcmp(a, "--screens") && i + 1 < argc) {
			screens = argv[++i];
			if (i + 1 < argc && argv[i + 1][0] != '-')
				screen_every = (int)num(argv[++i]);
		} else if (!strcmp(a, "--human")) {
			d.player_human = 1;
			if (i + 1 < argc && argv[i + 1][0] != '-')
				policy_seed = (uint32_t)num(argv[++i]);
		} else if (!strcmp(a, "--acks")) {
			d.report_acks = 1;
		} else if (!strcmp(a, "--count") && i + 1 < argc) {
			count = (int)num(argv[++i]);
		} else if (!strcmp(a, "--quiet")) {
			quiet = 1;
		} else if (!strcmp(a, "--verbose")) {
			verbose = 1;
		} else if (!strcmp(a, "--stack")) {
			show_stack = 1;
		} else if (!strcmp(a, "--anims") && i + 1 < argc) {
			d.animations_disabled = num(argv[++i]) ? 0 : 1;
		} else if (!strcmp(a, "--text-speed") && i + 1 < argc) {
			d.text_speed = (uint8_t)num(argv[++i]);
		} else if (!strcmp(a, "--prizes") && i + 1 < argc) {
			d.prizes = (uint8_t)num(argv[++i]);
		} else if (!strcmp(a, "--no-per-side-ai")) {
			d.per_side_ai = 0;
		} else if (!strcmp(a, "--hang") && i + 1 < argc) {
			d.hang_frames = (uint32_t)num(argv[++i]);
		} else {
			usage(argv[0]);
			return 2;
		}
	}
	if (!have_decks || !have_seed) {
		usage(argv[0]);
		return 2;
	}
	if (count > 1 && !verbose)
		quiet = 1;

	double t0 = now_sec();
	Ref *r = ref_new(rom, sym);
	if (!r)
		return 2;
	double tboot = now_sec() - t0;
	for (int i = 0; i < ntraces; i++)
		if (ref_trace_add(r, traces[i]) < 0)
			fprintf(stderr, "warning: unknown trace symbol %s\n", traces[i]);
	if (trace_file) {
		FILE *fp = fopen(trace_file, "r");
		if (!fp) {
			perror(trace_file);
			return 2;
		}
		char line[256];
		while (fgets(line, sizeof line, fp)) {
			char name[200];
			if (line[0] == '#' || sscanf(line, "%199s", name) != 1)
				continue;
			if (ref_trace_add(r, name) < 0)
				fprintf(stderr, "warning: unknown trace symbol %s\n", name);
		}
		fclose(fp);
	}
	for (int i = 0; i < nshow; i++) {
		int b;
		if (ref_sym(r, show_names[i], &b, &show_addr[i]) || show_addr[i] < 0xc000 ||
		    (show_addr[i] >= 0xe000 && show_addr[i] < 0xff80)) {
			fprintf(stderr, "--show: %s is not a WRAM/HRAM label\n", show_names[i]);
			return 2;
		}
	}
	ref_trace_rng(r, rng);
	if (screens)
		ref_set_screens(r, screens, screen_every);
	if (!quiet)
		printf("# boot %.3fs\n", tboot);

	int fails = 0;
	uint64_t total_frames = 0;
	double total_time = 0;
	uint8_t seed2 = d.rng[2];
	for (int k = 0; k < count; k++) {
		d.rng[2] = (uint8_t)(seed2 + k);
		pol_state = policy_seed + (uint32_t)k * 0x9e3779b9u;
		if (!pol_state)
			pol_state = 1;
		double t1 = now_sec();
		if (ref_start_duel(r, &d))
			return 2;
		RefEvent ev;
		long nev = 0, ninput = 0;
		int kind;
		for (;;) {
			kind = ref_next(r, &ev);
			nev++;
			if (!quiet)
				print_event(r, &ev, show_stack);
			if (kind == REF_EV_INPUT) {
				ninput++;
				if (ev.input.type != REF_IN_ACK) {
					int c = policy(&ev.input);
					if (!quiet)
						printf("        answer %d\n", c);
					if (ref_answer(r, c)) {
						printf("ERROR: answer %d rejected\n", c);
						kind = REF_EV_ERROR;
						break;
					}
				}
			}
			if (kind >= REF_EV_END)
				break;
		}
		double dt = now_sec() - t1;
		total_frames += ev.frame;
		total_time += dt;
		if (kind != REF_EV_END) {
			fails++;
			if (quiet)
				print_event(r, &ev, 0);
			ref_print_stack(r, stdout);
		}
		printf("RESULT decks=%d,%d seed=%02x,%02x,%02x%s finished=%u result=%u turns=%u frames=%llu events=%ld inputs=%ld "
		       "time=%.2fs %s\n",
			d.player_deck_id, d.opp_deck_id, d.rng[0], d.rng[1], d.rng[2], d.player_human ? " human" : "",
			ev.duel_finished, ev.duel_result, ev.duel_turns, (unsigned long long)ev.frame, nev, ninput, dt,
			ref_event_kind_name(kind));
		fflush(stdout);
	}
	if (count > 1)
		printf("TOTAL duels=%d failed=%d frames=%llu time=%.2fs (%.0f frames/s)\n", count, fails,
			(unsigned long long)total_frames, total_time, total_time > 0 ? total_frames / total_time : 0.0);
	ref_free(r);
	return fails ? 1 : 0;
}
