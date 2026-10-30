/*
 * tcgcli: a terminal frontend for the duel engine (desktop target), also used
 * for smoke tests with a random "human" policy.
 *
 *   tcgcli [--p1 human|random|ai:<deck_id>] [--p2 ...] [--seed N] [--quiet] [--max-requests N]
 *
 * Human duelists answer on stdin: every request prints its choices and reads a
 * number (or 'b' for cancel / B button).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tcg.h"
#include "common.h"
#include "gen/text.h"

enum { KIND_HUMAN, KIND_RANDOM, KIND_AI };

static int kinds[2];
static int quiet;
static unsigned long rng_state = 1;
static long requests, max_requests = 200000;

static unsigned rnd(unsigned n)
{
	rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
	return n ? (unsigned)((rng_state >> 33) % n) : 0;
}

static const char *card_name(uint8_t duelist, uint8_t deck_index)
{
	static char buf[64];
	const uint8_t *c = tcg_card_data(tcg_card_id(duelist, deck_index));
	const char *s = tcg_text((uint16_t)(c[3] | c[4] << 8));
	snprintf(buf, sizeof(buf), "%s", s);
	return buf;
}

static void print_board(void)
{
	for (int side = 0; side < 2; side++) {
		uint8_t d = side ? TCG_OPPONENT : TCG_PLAYER;
		const uint8_t *v = tcg_duelvars(d);
		printf("  [%s] hand %d, deck %d, prizes %d:", side ? "P2" : "P1",
		       v[DUELVARS_NUMBER_OF_CARDS_IN_HAND], 60 - v[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK],
		       __builtin_popcount(v[DUELVARS_PRIZES]));
		for (int i = 0; i < v[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA] && i < 6; i++) {
			uint8_t card = v[DUELVARS_ARENA_CARD + i];
			printf(" %s%s(%dhp)", i == 0 ? "*" : "", card_name(d, card), v[DUELVARS_ARENA_CARD_HP + i]);
		}
		printf("\n");
	}
}

static void on_event(void *ctx, const TcgEvent *ev)
{
	(void)ctx;
	if (quiet)
		return;
	switch (ev->type) {
	case TCG_EV_TEXT:
		if (ev->str && *ev->str) {
			printf("| ");
			for (const char *s = ev->str; *s; s++) {
				putchar(*s);
				if (*s == '\n')
					printf("| ");
			}
			printf("\n");
		}
		break;
	case TCG_EV_TURN:
		printf("=== turn %d: %s ===\n", ev->arg[1] + 1, ev->arg[0] == TCG_PLAYER ? "P1" : "P2");
		print_board();
		break;
	case TCG_EV_COIN:
		printf("  (coin %d/%d: %s)\n", ev->arg[1], ev->arg[2] ? ev->arg[2] : 1, ev->arg[0] ? "heads" : "tails");
		break;
	case TCG_EV_DUEL_END:
		print_board();
		break;
	}
}

static int random_answer(const TcgRequest *r)
{
	switch (r->type) {
	case TCG_REQ_ACK:
		return 0;
	case TCG_REQ_DUEL_MENU: {
		/* hand, attack, check, power, retreat, done */
		static const int weights[6] = { 40, 30, 1, 8, 6, 15 };
		int t = (int)rnd(100);
		for (int i = 0; i < 6; i++) {
			if (t < weights[i])
				return i;
			t -= weights[i];
		}
		return 5;
	}
	case TCG_REQ_YES_NO:
		return (int)rnd(4) != 0;
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
		return (int)rnd(6);
	default:
		if (!r->num_items || rnd(10) == 0)
			return TCG_CANCEL;
		return (int)rnd(r->num_items);
	}
}

static int human_answer(const TcgRequest *r)
{
	if (r->type == TCG_REQ_ACK)
		return 0;
	static const char *names[] = { "", "ack", "duel menu (0 Hand 1 Attack 2 Check 3 Power 4 Retreat 5 Done)",
		"card list", "play/check (0 play 1 check)", "play area slot", "menu", "yes(1)/no(0)",
		"energy to discard", "prize (0-5)", "color", "number" };
	printf("? %s for %s", r->type < sizeof(names) / sizeof(*names) ? names[r->type] : "request",
	       r->duelist == TCG_PLAYER ? "P1" : "P2");
	if (r->text)
		printf(" [%s]", tcg_text(r->text));
	printf("\n");
	if ((r->type == TCG_REQ_CARD_LIST || r->type == TCG_REQ_ENERGY_DISCARD) && r->items)
		for (int i = 0; i < r->num_items; i++)
			printf("   %d: %s\n", i, card_name(r->shown, r->items[i]));
	if (r->type == TCG_REQ_PLAY_AREA) {
		const uint8_t *v = tcg_duelvars(r->shown);
		for (int i = r->arg[0]; i < v[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]; i++)
			printf("   %d: %s (%d HP)\n", i, card_name(r->shown, v[DUELVARS_ARENA_CARD + i]),
			       v[DUELVARS_ARENA_CARD_HP + i]);
	}
	char line[64];
	printf("> ");
	fflush(stdout);
	if (!fgets(line, sizeof(line), stdin))
		exit(0);
	if (line[0] == 'b' || line[0] == 'B')
		return TCG_CANCEL;
	return atoi(line);
}

static int on_request(void *ctx, const TcgRequest *r)
{
	(void)ctx;
	if (++requests > max_requests) {
		fprintf(stderr, "tcgcli: too many requests, giving up\n");
		exit(2);
	}
	int side = r->duelist == TCG_OPPONENT;
	if (kinds[side] == KIND_HUMAN)
		return human_answer(r);
	return random_answer(r);
}

static void parse_kind(const char *s, int side, TcgDuelSetup *setup)
{
	TcgDuelistSetup *d = &setup->duelist[side];
	if (!strcmp(s, "human")) {
		kinds[side] = KIND_HUMAN;
		d->kind = TCG_HUMAN;
	} else if (!strcmp(s, "random")) {
		kinds[side] = KIND_RANDOM;
		d->kind = TCG_HUMAN;
	} else if (!strncmp(s, "ai:", 3)) {
		kinds[side] = KIND_AI;
		d->kind = TCG_AI;
		d->deck_id = (uint8_t)strtol(s + 3, NULL, 0);
	}
}

int main(int argc, char **argv)
{
	TcgDuelSetup setup;
	setvbuf(stdout, NULL, _IOLBF, 0);
	memset(&setup, 0, sizeof(setup));
	setup.prizes = 6;
	setup.duelist[0].deck_id = CHARMANDER_AND_FRIENDS_DECK_ID;
	setup.duelist[1].deck_id = SQUIRTLE_AND_FRIENDS_DECK_ID;
	kinds[0] = kinds[1] = KIND_RANDOM;
	unsigned seed = 1;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--p1") && i + 1 < argc)
			parse_kind(argv[++i], 0, &setup);
		else if (!strcmp(argv[i], "--p2") && i + 1 < argc)
			parse_kind(argv[++i], 1, &setup);
		else if (!strcmp(argv[i], "--deck1") && i + 1 < argc)
			setup.duelist[0].deck_id = (uint8_t)strtol(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--deck2") && i + 1 < argc)
			setup.duelist[1].deck_id = (uint8_t)strtol(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
			seed = (unsigned)strtoul(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--quiet"))
			quiet = 1;
		else if (!strcmp(argv[i], "--max-requests") && i + 1 < argc)
			max_requests = strtol(argv[++i], NULL, 0);
	}
	rng_state = seed * 2654435761u + 1;
	setup.rng[0] = (uint8_t)seed;
	setup.rng[1] = (uint8_t)(seed >> 8);
	setup.rng[2] = (uint8_t)(seed >> 16);
	setup.duelist[0].name = "P1";
	setup.duelist[1].name = "P2";
	TcgHost host = { on_event, on_request, NULL, NULL };
	tcg_set_host(&host);
	int result = tcg_run_duel(&setup);
	const uint8_t *w = tcg_wram();
	printf("result: %s (wDuelFinished=%d, turns=%d, requests=%ld)\n", result == 0 ? "P1 wins" : "P2 wins",
	       w[ADDR_wDuelFinished - 0xc000], w[ADDR_wDuelTurns - 0xc000], requests);
	return 0;
}
