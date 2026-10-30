/* Frontend entry points (tcg.h). */
#include "engine.h"
#include "tcg.h"
#include "duel_ui.h"
#include "gen/cards.h"
#include "gen/decks.h"

u8 gPlayerDeckCards[DECK_SIZE];
bool gPerSideAI;
static u8 gReceivedLegendaryCards;
u8 gSkipDelayAllowed;

/* sReceivedLegendaryCards: whether the player has received the legendary cards
 * (the end of the game). Read by the AI (CheckIfNotABossDeckID). */
u8 GetSRAMReceivedLegendaryCards(void)
{
	return gReceivedLegendaryCards;
}


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

/* clear engine memory and apply the settings both entry points take from setup */
static void init_engine(const TcgDuelSetup *s)
{
	static const TcgDuelSetup defaults;
	if (!s)
		s = &defaults;
	memset(gWRAM, 0, sizeof(gWRAM));
	memset(gHRAM, 0, sizeof(gHRAM));
	gUIDecider = 0; /* (a duel left by a longjmp from tcg_fatal or the host can leave it set) */
	gTextID = 0;
	gDuelistNames[0] = s->duelist[0].name ? s->duelist[0].name : "PLAYER";
	gDuelistNames[1] = s->duelist[1].name ? s->duelist[1].name : "OPPONENT";
	wTextSpeed = 0;
	wAnimationsDisabled = s->animations_disabled;
	gReceivedLegendaryCards = s->received_legendary_cards;
	gSkipDelayAllowed = s->skip_delay_allowed ? 1 : 0;
	wSkipDelayAllowed = gSkipDelayAllowed; /* (game_loop.asm copies sSkipDelayAllowed at boot) */
}

int tcg_run_duel(const TcgDuelSetup *s)
{
	init_engine(s);
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
	/* decks given as card lists are copied; ROM decks are loaded with LoadDeck like the
	 * original (CopyDeckData; GRASS_AND_PSYCHIC_DECK even has 61 cards, the extra one
	 * landing in the unused part of the deck buffer) */
	hWhoseTurn = PLAYER_TURN;
	if (has_cards(p))
		memcpy(wPlayerDeck, p->cards, DECK_SIZE);
	else
		LoadDeck((u8)(p->deck_id + 2));
	hWhoseTurn = OPPONENT_TURN;
	if (has_cards(o))
		memcpy(wOpponentDeck, o->cards, DECK_SIZE);
	else
		LoadDeck((u8)(o->deck_id + 2));
	hWhoseTurn = PLAYER_TURN;
	wPlayerDuelistType = p->kind == TCG_AI ? (u8)(DUELIST_TYPE_AI_OPP | p->deck_id) : DUELIST_TYPE_PLAYER;
	wOpponentDuelistType = o->kind == TCG_AI ? (u8)(DUELIST_TYPE_AI_OPP | o->deck_id) : DUELIST_TYPE_PLAYER;
	wOpponentDeckID = o->kind == TCG_AI ? o->deck_id : p->deck_id;
	gPerSideAI = p->kind == TCG_AI && o->kind == TCG_AI;
	StartDuel();
	return wDuelResult;
}

uint32_t tcg_state_hash(void) { return HashDuelData(); }

int tcg_saved_duel_info(const uint8_t *data, uint32_t size, TcgSavedDuel *out)
{
	if (size != DUEL_SAVE_FILE_SIZE || ValidateSavedDuelDataFromHL(data))
		return -1;
	memset(out, 0, sizeof(*out));
	out->duel_type = data[SAVE_DUEL_HEADER_DUEL_TYPE];
	for (int i = 0; i < 2; i++) {
		u16 vars = i ? ADDR_wOpponentDuelVariables : ADDR_wPlayerDuelVariables;
		out->duelist_type[i] = *SavedDuelDataByte(data, (u16)(vars + DUELVARS_DUELIST_TYPE));
		u8 prizes = *SavedDuelDataByte(data, (u16)(vars + DUELVARS_PRIZES));
		for (; prizes; prizes &= (u8)(prizes - 1))
			out->prizes_left[i]++;
	}
	out->opponent_deck_id = *SavedDuelDataByte(data, ADDR_wOpponentDeckID);
	out->turns = *SavedDuelDataByte(data, ADDR_wDuelTurns);
	out->whose_turn = *SavedDuelDataByte(data, ADDR_hWhoseTurn) == OPPONENT_TURN ? TCG_OPPONENT : TCG_PLAYER;
	return 0;
}

int tcg_continue_duel(const TcgDuelSetup *setup, const uint8_t *data, uint32_t size)
{
	init_engine(setup);
	/* back in SRAM (data of another size is a broken save) */
	memset(gDuelSave, 0, sizeof(gDuelSave));
	if (size == DUEL_SAVE_FILE_SIZE)
		memcpy(gDuelSave, data, DUEL_SAVE_FILE_SIZE);
	TcgSavedDuel info;
	gPerSideAI = tcg_saved_duel_info(gDuelSave, DUEL_SAVE_FILE_SIZE, &info) == 0 &&
		(info.duelist_type[0] & DUELIST_TYPE_AI_OPP) && (info.duelist_type[1] & DUELIST_TYPE_AI_OPP);
	if (TryContinueDuel())
		return TCG_CONTINUE_BROKEN;
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
