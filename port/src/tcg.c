/* Frontend entry points (tcg.h). */
#include "engine.h"
#include "tcg.h"
#include "gen/cards.h"
#include "gen/decks.h"

u8 gPlayerDeckCards[DECK_SIZE];
bool gPerSideAI;


/* expand a DeckPointers list into 60 card IDs, exactly like CopyDeckData */
static void expand_deck(u8 deck, u8 *out)
{
	memset(out, 0, DECK_SIZE);
	const u8 *p = gDeckLists[deck];
	u8 n = 0;
	while (*p && n < DECK_SIZE) {
		u8 count = *p++, id = *p++;
		while (count-- && n < DECK_SIZE)
			out[n++] = id;
	}
}

static bool has_cards(const TcgDuelistSetup *d)
{
	for (int i = 0; i < DECK_SIZE; i++)
		if (d->cards[i])
			return true;
	return false;
}

static void fill_deck(const TcgDuelistSetup *d, u8 *out)
{
	if (has_cards(d))
		memcpy(out, d->cards, DECK_SIZE);
	else
		expand_deck((u8)(d->deck_id + 2), out); /* *_DECK = *_DECK_ID + 2 */
}

int tcg_run_duel(const TcgDuelSetup *s)
{
	memset(gWRAM, 0, sizeof(gWRAM));
	memset(gHRAM, 0, sizeof(gHRAM));
	gDuelistNames[0] = s->duelist[0].name ? s->duelist[0].name : "PLAYER";
	gDuelistNames[1] = s->duelist[1].name ? s->duelist[1].name : "OPPONENT";
	wTextSpeed = 0;
	wAnimationsDisabled = s->animations_disabled;
	wNPCDuelPrizes = s->prizes ? s->prizes : PRIZES_6;
	wDuelTheme = s->music ? s->music : MUSIC_DUEL_THEME_1;
	wIsPracticeDuel = 0;
	wRNG1 = s->rng[0];
	wRNG2 = s->rng[1];
	wRNGCounter = s->rng[2];
	wOpponentName[0] = 0;
	wOpponentName[1] = 0;

	const TcgDuelistSetup *p = &s->duelist[0], *o = &s->duelist[1];
	if (p->kind == TCG_HUMAN && o->kind == TCG_AI) {
		/* the original's path: StartDuel_VSAIOpp loads the player's selected deck and the
		 * AI opponent deck (LoadOpponentDeck handles Sam's decks and the duelist type) */
		gPerSideAI = false;
		fill_deck(p, gPlayerDeckCards);
		wNPCDuelDeckID = o->deck_id;
		StartDuel_VSAIOpp();
		return wDuelResult;
	}
	/* other combinations set up both sides directly and enter StartDuel, like the
	 * differential harness does with the original ROM */
	hWhoseTurn = PLAYER_TURN;
	fill_deck(p, wPlayerDeck);
	fill_deck(o, wOpponentDeck);
	wPlayerDuelistType = p->kind == TCG_AI ? (u8)(DUELIST_TYPE_AI_OPP | p->deck_id) : DUELIST_TYPE_PLAYER;
	wOpponentDuelistType = o->kind == TCG_AI ? (u8)(DUELIST_TYPE_AI_OPP | o->deck_id) : DUELIST_TYPE_PLAYER;
	wOpponentDeckID = o->kind == TCG_AI ? o->deck_id : p->deck_id;
	gPerSideAI = p->kind == TCG_AI && o->kind == TCG_AI;
	StartDuel();
	return wDuelResult;
}

uint8_t *tcg_wram(void) { return gWRAM; }
uint8_t *tcg_hram(void) { return gHRAM; }

uint8_t *tcg_duelvars(uint8_t duelist)
{
	return DuelVarsOf(duelist == TCG_OPPONENT ? OPPONENT_TURN : PLAYER_TURN);
}

const uint8_t *tcg_card_data(uint8_t card_id)
{
	if (card_id > NUM_CARDS)
		return gCardData[0];
	return gCardData[card_id];
}

uint8_t tcg_card_id(uint8_t duelist, uint8_t deck_index)
{
	const u8 *deck = duelist == TCG_OPPONENT ? wOpponentDeck : wPlayerDeck;
	return deck_index < DECK_SIZE ? deck[deck_index] : 0;
}

#ifndef TCG_NO_DEFAULT_FATAL
#include <stdio.h>
#include <stdlib.h>
void tcg_fatal(const char *msg)
{
	fprintf(stderr, "tcg: fatal: %s\n", msg);
	abort();
}
#endif
