/*
 * unitdiff: function-level differential tests.
 *
 * Calls a routine of the original ROM inside the emulator and its C port on
 * the same memory image, then compares registers/return values and all of
 * WRAM/HRAM. Used for leaf routines (RNG, damage math, card list helpers...)
 * with many random inputs; whole duels are covered by difftest.
 *
 *   unitdiff ROM SYM [iterations]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine.h"
#undef GB /* card set constant; clashes with the emulator type */
#include "gb/gb.h"

static GB *gb;
static struct Sym { char name[96]; int bank; uint16_t addr; } *syms;
static int nsyms;
static uint16_t trap_addr;
static unsigned long rng = 88172645463325252ULL;

static unsigned xr(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return (unsigned)rng;
}

static void load_sym(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		perror(path);
		exit(1);
	}
	char line[256];
	syms = calloc(30000, sizeof(*syms));
	while (fgets(line, sizeof(line), f) && nsyms < 30000) {
		unsigned b, a;
		char name[96];
		if (sscanf(line, "%x:%x %95s", &b, &a, name) == 3) {
			syms[nsyms].bank = (int)b;
			syms[nsyms].addr = (uint16_t)a;
			strcpy(syms[nsyms].name, name);
			nsyms++;
		}
	}
	fclose(f);
}

static const struct Sym *sym(const char *name)
{
	for (int i = 0; i < nsyms; i++)
		if (!strcmp(syms[i].name, name))
			return &syms[i];
	fprintf(stderr, "unknown symbol %s\n", name);
	exit(1);
}

/* call a ROM routine with the current gb registers; returns when it returns */
static void call_rom(const char *name)
{
	const struct Sym *s = sym(name);
	gb->ime = 0;
	gb->ie = 0;
	if (s->bank) {
		gb_write(gb, 0x2000, (uint8_t)s->bank);
		gb->hram[0] = (uint8_t)s->bank; /* hBankROM */
	}
	gb->sp = 0xdff0;
	gb->sp -= 2;
	gb_write(gb, gb->sp, (uint8_t)trap_addr);
	gb_write(gb, (uint16_t)(gb->sp + 1), (uint8_t)(trap_addr >> 8));
	gb->pc = s->addr;
	gb->halted = 0;
	gb->bp_skip_n = 0; /* we moved PC ourselves: forget pending breakpoint resumes */
	for (;;) {
		int r = gb_run(gb, 50000000, 0);
		if (r == GB_STOP_BREAKPOINT && gb->pc == trap_addr)
			return;
		fprintf(stderr, "call_rom(%s): stopped (%d) at %04x\n", name, r, gb->pc);
		exit(1);
	}
}

/* copy the emulator's WRAM/HRAM into the port */
static void sync_to_port(void)
{
	memcpy(gWRAM, gb->wram, WRAM_SIZE);
	memcpy(gHRAM, gb->hram, HRAM_SIZE);
}

static int compare_memory(const char *what)
{
	int bad = 0;
	for (int i = 0; i < WRAM_SIZE; i++) {
		if (i + WRAM_START >= 0xdf00)
			break; /* stack area used by the call */
		if (gWRAM[i] != gb->wram[i]) {
			if (bad++ < 8)
				fprintf(stderr, "%s: WRAM %04x port %02x rom %02x\n", what, i + WRAM_START, gWRAM[i], gb->wram[i]);
		}
	}
	for (int i = 1; i < HRAM_SIZE - 1; i++) {
		if (gHRAM[i] != gb->hram[i]) {
			if (bad++ < 16)
				fprintf(stderr, "%s: HRAM %04x port %02x rom %02x\n", what, i + HRAM_START, gHRAM[i], gb->hram[i]);
		}
	}
	return bad;
}

void tcg_fatal(const char *msg)
{
	fprintf(stderr, "tcg_fatal: %s\n", msg);
	exit(1);
}

static int test_rng(int iters)
{
	int fails = 0;
	for (int it = 0; it < iters; it++) {
		gb->wram[ADDR_wRNG1 - WRAM_START] = (uint8_t)xr();
		gb->wram[ADDR_wRNG2 - WRAM_START] = (uint8_t)xr();
		gb->wram[ADDR_wRNGCounter - WRAM_START] = (uint8_t)xr();
		sync_to_port();
		uint8_t want_a, got_a;
		if (it & 1) {
			uint8_t n = (uint8_t)xr();
			gb->a = n;
			call_rom("Random");
			want_a = gb->a;
			got_a = Random(n);
		} else {
			call_rom("UpdateRNGSources");
			want_a = gb->a;
			got_a = UpdateRNGSources();
		}
		if (want_a != got_a || compare_memory("rng")) {
			fprintf(stderr, "rng mismatch at iteration %d: a port %02x rom %02x\n", it, got_a, want_a);
			if (++fails > 3)
				break;
		}
	}
	return fails;
}

/* ------------------------------------------------------------------ random duel states */

static const uint8_t card_pool_extra[] = { PLUSPOWER, DEFENDER, CLEFAIRY_DOLL, MYSTERIOUS_FOSSIL,
	MUK, MACHAMP, MEW_LV8, MR_MIME, KABUTO, DODRIO, AERODACTYL, CHARIZARD, BLASTOISE, OMANYTE, VENOMOTH };

static uint8_t random_card(void)
{
	if (xr() % 4 == 0)
		return card_pool_extra[xr() % sizeof(card_pool_extra)];
	/* not RECYCLE (NUM_CARDS): its record is at the end of the bank and the original's
	 * 65-byte copy reads VRAM past $7fff (see gen_data.py) */
	/* not GAMBLER/RECYCLE (the last two): their records are at the end of the bank and the
	 * original's 65-byte copy reads VRAM past $7fff (see gen_data.py) */
	return (uint8_t)(1 + xr() % (NUM_CARDS - 2));
}

static void random_duelvars(uint8_t *v, uint8_t *deck)
{
	memset(v, 0, 0x100);
	for (int i = 0; i < 60; i++)
		deck[i] = random_card();
	/* deck order: a permutation */
	for (int i = 0; i < 60; i++)
		v[DUELVARS_DECK_CARDS + i] = (uint8_t)i;
	for (int i = 59; i > 0; i--) {
		int j = (int)(xr() % (unsigned)(i + 1));
		uint8_t t = v[DUELVARS_DECK_CARDS + i];
		v[DUELVARS_DECK_CARDS + i] = v[DUELVARS_DECK_CARDS + j];
		v[DUELVARS_DECK_CARDS + j] = t;
	}
	v[DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK] = (uint8_t)(xr() % 61);
	int npkmn = 1 + (int)(xr() % 6);
	v[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA] = (uint8_t)npkmn;
	for (int i = 0; i < 7; i++)
		v[DUELVARS_ARENA_CARD + i] = 0xff;
	int hand = 0, discard = 0;
	for (int i = 0; i < 60; i++) {
		uint8_t loc;
		switch (xr() % 7) {
		case 0: loc = CARD_LOCATION_HAND; v[DUELVARS_HAND + hand++] = (uint8_t)i; break;
		case 1: loc = CARD_LOCATION_DISCARD_PILE; discard++; break;
		case 2: loc = CARD_LOCATION_PRIZE; break;
		case 3: case 4: loc = (uint8_t)(CARD_LOCATION_ARENA + xr() % (unsigned)npkmn); break;
		default: loc = CARD_LOCATION_DECK;
		}
		v[i] = loc;
	}
	for (int p = 0; p < npkmn; p++) {
		uint8_t card = (uint8_t)(xr() % 60);
		v[DUELVARS_ARENA_CARD + p] = card;
		v[card] = (uint8_t)(CARD_LOCATION_ARENA + p);
		v[DUELVARS_ARENA_CARD_HP + p] = (uint8_t)(10 * (xr() % 13));
		v[DUELVARS_ARENA_CARD_STAGE + p] = (uint8_t)(xr() % 3);
		v[DUELVARS_ARENA_CARD_FLAGS + p] = (uint8_t)(xr() & 0xe0);
		if (xr() % 6 == 0)
			v[DUELVARS_ARENA_CARD_CHANGED_TYPE + p] = (uint8_t)(0x80 | xr() % 7);
		v[DUELVARS_ARENA_CARD_ATTACHED_PLUSPOWER + p] = (uint8_t)(xr() % 3);
		v[DUELVARS_ARENA_CARD_ATTACHED_DEFENDER + p] = (uint8_t)(xr() % 3);
	}
	v[DUELVARS_NUMBER_OF_CARDS_IN_HAND] = (uint8_t)hand;
	v[DUELVARS_NUMBER_OF_CARDS_IN_DISCARD_PILE] = (uint8_t)discard;
	static const uint8_t statuses[] = { 0, 0, CONFUSED, ASLEEP, PARALYZED, POISONED, DOUBLE_POISONED, POISONED | ASLEEP };
	v[DUELVARS_ARENA_CARD_STATUS] = statuses[xr() % sizeof(statuses)];
	static const uint8_t sub1[] = { 0, 0, 0, SUBSTATUS1_AGILITY, SUBSTATUS1_FLY, SUBSTATUS1_PREVENT_LESS_THAN_40,
		SUBSTATUS1_NO_DAMAGE_STIFFEN, SUBSTATUS1_REDUCE_BY_20, SUBSTATUS1_BARRIER, SUBSTATUS1_HALVE_DAMAGE,
		SUBSTATUS1_DESTINY_BOND, SUBSTATUS1_NEXT_TURN_DOUBLE_DAMAGE, SUBSTATUS1_REDUCE_BY_10 };
	static const uint8_t sub2[] = { 0, 0, 0, SUBSTATUS2_SMOKESCREEN, SUBSTATUS2_SAND_ATTACK, SUBSTATUS2_REDUCE_BY_20,
		SUBSTATUS2_AMNESIA, SUBSTATUS2_TAIL_WAG, SUBSTATUS2_LEER, SUBSTATUS2_POUNCE, SUBSTATUS2_ACID,
		SUBSTATUS2_BONE_ATTACK, SUBSTATUS2_GROWL };
	v[DUELVARS_ARENA_CARD_SUBSTATUS1] = sub1[xr() % sizeof(sub1)];
	v[DUELVARS_ARENA_CARD_SUBSTATUS2] = sub2[xr() % sizeof(sub2)];
	v[DUELVARS_ARENA_CARD_SUBSTATUS3] = (uint8_t)(xr() & 3);
	if (xr() % 5 == 0)
		v[DUELVARS_ARENA_CARD_CHANGED_WEAKNESS] = (uint8_t)(1 << (xr() % 8));
	if (xr() % 5 == 0)
		v[DUELVARS_ARENA_CARD_CHANGED_RESISTANCE] = (uint8_t)(1 << (xr() % 8));
	v[DUELVARS_PRIZES] = (uint8_t)(xr() & 0x3f);
	v[DUELVARS_DUELIST_TYPE] = xr() & 1 ? DUELIST_TYPE_PLAYER : 0x80 | (uint8_t)(xr() % 20);
	for (int i = 0; i < 6; i++)
		v[DUELVARS_PRIZE_CARDS + i] = (uint8_t)(xr() % 60);
}

static void random_state(void)
{
	uint8_t *w = gb->wram;
	random_duelvars(&w[ADDR_wPlayerDuelVariables - WRAM_START], &w[ADDR_wPlayerDeck - WRAM_START]);
	random_duelvars(&w[ADDR_wOpponentDuelVariables - WRAM_START], &w[ADDR_wOpponentDeck - WRAM_START]);
	gb->hram[ADDR_hWhoseTurn - HRAM_START] = xr() & 1 ? PLAYER_TURN : OPPONENT_TURN;
	gb->hram[ADDR_hTempPlayAreaLocation_ff9d - HRAM_START] = (uint8_t)(xr() % 6);
	gb->hram[ADDR_hTempCardIndex_ff98 - HRAM_START] = (uint8_t)(xr() % 60);
	w[ADDR_wDamage - WRAM_START] = (uint8_t)(10 * (xr() % 15));
	w[ADDR_wDamage + 1 - WRAM_START] = xr() % 8 == 0 ? 0x80 : 0;
	w[ADDR_wNoDamageOrEffect - WRAM_START] = xr() % 6 == 0 ? (uint8_t)(1 + xr() % 5) : 0;
	w[ADDR_wLoadedAttackCategory - WRAM_START] = (uint8_t)(xr() % 5) | (xr() % 4 == 0 ? RESIDUAL : 0);
	w[ADDR_wTempNonTurnDuelistCardID - WRAM_START] = random_card();
	w[ADDR_wTempTurnDuelistCardID - WRAM_START] = random_card();
	w[ADDR_wTempPlayAreaLocation_cceb - WRAM_START] = (uint8_t)(xr() % 6);
	w[ADDR_wIsDamageToSelf - WRAM_START] = xr() % 4 == 0;
	w[ADDR_wWhoseTurn - WRAM_START] = gb->hram[ADDR_hWhoseTurn - HRAM_START];
	/* a random card list in wDuelTempList */
	int n = (int)(xr() % 12);
	for (int i = 0; i < n; i++)
		w[ADDR_wDuelTempList - WRAM_START + i] = (uint8_t)(xr() % 60);
	w[ADDR_wDuelTempList - WRAM_START + n] = 0xff;
	sync_to_port();
}

#define F_A 1   /* compare register a */
#define F_C 2   /* compare carry */
#define F_DE 4  /* compare de */
#define F_Z 8   /* compare zero flag */

struct Out { uint8_t a; bool c; uint16_t de; bool z; };

typedef void (*PortFn)(struct Out *o, uint8_t a, uint8_t b, uint8_t c, uint16_t de);

struct UnitTest {
	const char *rom;
	int flags;
	PortFn port;
	uint8_t a_mod, b_mod, c_mod; /* random register ranges (0 = any) */
	uint16_t de_mod;
};

#define T(name) static void t_##name(struct Out *o, uint8_t a, uint8_t b, uint8_t c, uint16_t de)
T(CountPrizes) { o->a = CountPrizes(); }
T(ShuffleDeck) { ShuffleDeck(); }
T(DrawCardFromDeck) { u8 x = 0; o->c = DrawCardFromDeck(&x); o->a = x; }
T(ReturnCardToDeck) { o->a = ReturnCardToDeck(a); }
T(SearchCardInDeckAndAddToHand) { SearchCardInDeckAndAddToHand(a); }
T(AddCardToHand) { AddCardToHand(a); }
T(RemoveCardFromHand) { RemoveCardFromHand(a); }
T(MoveHandCardToDiscardPile) { MoveHandCardToDiscardPile(a); }
T(PutCardInDiscardPile) { PutCardInDiscardPile(a); }
T(MoveDiscardPileCardToHand) { o->a = MoveDiscardPileCardToHand(a); }
T(CreateDiscardPileCardList) { o->c = CreateDiscardPileCardList(); }
T(CreateDeckCardList) { u8 n = 0; o->c = CreateDeckCardList(&n); o->a = n; }
T(CreateArenaOrBenchEnergyCardList) { o->c = CreateArenaOrBenchEnergyCardList(a); }
T(CreateHandCardList) { u8 n = 0; o->c = CreateHandCardList(&n); o->a = n; }
T(SortCardsInDuelTempListByID) { SortCardsInDuelTempListByID(); }
T(GetCardIDFromDeckIndex) { o->de = GetCardIDFromDeckIndex(a); }
T(RemoveCardFromDuelTempList) { o->c = RemoveCardFromDuelTempList(a); }
T(CountCardsInDuelTempList) { o->a = CountCardsInDuelTempList(); }
T(LoadCardDataToBuffer1_FromDeckIndex) { o->a = LoadCardDataToBuffer1_FromDeckIndex(a); }
T(LoadCardDataToBuffer2_FromDeckIndex) { o->a = LoadCardDataToBuffer2_FromDeckIndex(a); }
T(EvolvePokemonCard) { EvolvePokemonCard(); }
T(CheckIfCanEvolveInto) { u8 x = 0; o->c = CheckIfCanEvolveInto((u8)(de >> 8), (u8)de, &x); o->z = x == 0; }
T(CheckIfCanEvolveInto_BasicToStage2) { o->c = CheckIfCanEvolveInto_BasicToStage2((u8)(de >> 8), (u8)de); }
T(ClearAllStatusConditions) { ClearAllStatusConditions(); }
T(PutHandPokemonCardInPlayArea) { u8 x = 0; o->c = PutHandPokemonCardInPlayArea(a, &x); o->a = x; }
T(MovePlayAreaCardToDiscardPile) { MovePlayAreaCardToDiscardPile((u8)de); }
T(EmptyPlayAreaSlot) { EmptyPlayAreaSlot((u8)de); }
T(ShiftAllPokemonToFirstPlayAreaSlots) { ShiftAllPokemonToFirstPlayAreaSlots(); }
T(SwapArenaWithBenchPokemon) { SwapArenaWithBenchPokemon((u8)de); }
T(SwapPlayAreaPokemon) { SwapPlayAreaPokemon((u8)(de >> 8), (u8)de); }
T(GetPlayAreaCardAttachedEnergies) { GetPlayAreaCardAttachedEnergies((u8)de); }
T(CopyAttackDataAndDamage_FromDeckIndex) { CopyAttackDataAndDamage_FromDeckIndex((u8)(de >> 8), (u8)de); }
T(UpdateArenaCardIDsAndClearTwoTurnDuelVars) { UpdateArenaCardIDsAndClearTwoTurnDuelVars(); }
T(ApplyDamageModifiers_DamageToSelf) { o->de = ApplyDamageModifiers_DamageToSelf(); }
T(GetCardDamageAndMaxHP) { u8 m = 0; o->a = GetCardDamageAndMaxHP((u8)de, &m); }
T(CheckLoadedAttackFlag) { o->c = CheckLoadedAttackFlag(a); }
T(SwapTurn) { SwapTurn(); }
T(HandleDoubleDamageSubstatus) { o->de = HandleDoubleDamageSubstatus(de); }
T(HandleDamageReductionExceptSubstatus2) { o->de = HandleDamageReductionExceptSubstatus2(de); }
T(HandleCantAttackSubstatus) { o->c = HandleCantAttackSubstatus(); }
T(HandleAmnesiaSubstatus) { o->c = HandleAmnesiaSubstatus(); }
T(CheckSandAttackOrSmokescreenSubstatus) { o->c = CheckSandAttackOrSmokescreenSubstatus(); }
T(CheckNoDamageOrEffect) { o->c = CheckNoDamageOrEffect(); }
T(IsClairvoyanceActive) { o->c = IsClairvoyanceActive(); }
T(CheckIsIncapableOfUsingPkmnPower) { o->c = CheckIsIncapableOfUsingPkmnPower(a); }
T(CountPokemonWithActivePkmnPowerInBothPlayAreas) { o->a = CountPokemonWithActivePkmnPowerInBothPlayAreas(a); o->c = o->a != 0; }
T(GetLoadedCard1RetreatCost) { o->a = GetLoadedCard1RetreatCost(); }
T(CheckUnableToRetreatDueToEffect) { o->c = CheckUnableToRetreatDueToEffect(); }
T(CheckCantUseTrainerDueToEffect) { o->c = CheckCantUseTrainerDueToEffect(); }
T(IsPrehistoricPowerActive) { o->c = IsPrehistoricPowerActive(); }
T(ClearDamageReductionSubstatus2) { ClearDamageReductionSubstatus2(); }
T(UpdateSubstatusConditions_StartOfTurn) { UpdateSubstatusConditions_StartOfTurn(); }
T(UpdateSubstatusConditions_EndOfTurn) { UpdateSubstatusConditions_EndOfTurn(); }
T(IsRainDanceActive) { o->c = IsRainDanceActive(); }
T(CheckRainDanceScenario) { o->c = CheckRainDanceScenario(); }
T(ClearChangedTypesIfMuk) { ClearChangedTypesIfMuk(a); }
T(GetPlayAreaCardColor) { o->a = GetPlayAreaCardColor(a); }
T(GetPlayAreaCardWeakness) { o->a = GetPlayAreaCardWeakness(a); }
T(GetPlayAreaCardResistance) { o->a = GetPlayAreaCardResistance(a); }
T(HandleEnergyBurn) { HandleEnergyBurn(); }
T(CheckIfEnoughEnergiesForGivenAttack) { o->c = CheckIfEnoughEnergiesForGivenAttack((u8)(de >> 8), (u8)de); }
T(CheckIfActiveCardParalyzedOrAsleep) { o->c = CheckIfActiveCardParalyzedOrAsleep(); }
T(CheckIfEnoughEnergiesToRetreat) { o->c = CheckIfEnoughEnergiesToRetreat(); }
T(GetCardOneStageBelow) { u8 d = 0, e = 0; o->c = GetCardOneStageBelow(&d, &e); o->de = (u16)(d << 8 | e); }
T(CountKnockedOutPokemon) { o->a = CountKnockedOutPokemon(); o->c = o->a != 0; }
T(CheckIfTurnDuelistPlayAreaPokemonAreAllKnockedOut) { o->c = CheckIfTurnDuelistPlayAreaPokemonAreAllKnockedOut(); }
T(ClearNonTurnTemporaryDuelvars_CopyStatus) { ClearNonTurnTemporaryDuelvars_CopyStatus(); }
T(UpdateArenaCardLastTurnDamage) { UpdateArenaCardLastTurnDamage(); }
T(SetAllPlayAreaPokemonCanEvolve) { SetAllPlayAreaPokemonCanEvolve(); }
T(HasAlivePokemonInPlayArea) { u8 n = 0; o->c = HasAlivePokemonInPlayArea(&n); }
T(HasAlivePokemonInBench) { u8 n = 0; o->c = HasAlivePokemonInBench(&n); }
T(TakeAPrizes) { TakeAPrizes(a); }
T(AddToDamage) { AddToDamage(a); }
T(SubtractFromDamage) { SubtractFromDamage(a); }
T(CheckIfEnoughEnergiesToAttack) { o->c = CheckIfEnoughEnergiesToAttack(); }
T(TranslateColorToWR) { o->a = TranslateColorToWR(a); }

static const struct UnitTest tests[] = {
	{ "CountPrizes", F_A, t_CountPrizes },
	{ "ShuffleDeck", 0, t_ShuffleDeck },
	{ "DrawCardFromDeck", F_C, t_DrawCardFromDeck },
	{ "ReturnCardToDeck", F_A, t_ReturnCardToDeck, 60 },
	{ "SearchCardInDeckAndAddToHand", 0, t_SearchCardInDeckAndAddToHand, 60 },
	{ "AddCardToHand", 0, t_AddCardToHand, 60 },
	{ "RemoveCardFromHand", 0, t_RemoveCardFromHand, 60 },
	{ "MoveHandCardToDiscardPile", 0, t_MoveHandCardToDiscardPile, 60 },
	{ "PutCardInDiscardPile", 0, t_PutCardInDiscardPile, 60 },
	{ "MoveDiscardPileCardToHand", F_A, t_MoveDiscardPileCardToHand, 60 },
	{ "CreateDiscardPileCardList", F_C, t_CreateDiscardPileCardList },
	{ "CreateDeckCardList", F_C, t_CreateDeckCardList },
	{ "CreateArenaOrBenchEnergyCardList", F_C, t_CreateArenaOrBenchEnergyCardList, 6 },
	{ "CreateHandCardList", F_C | F_A, t_CreateHandCardList },
	{ "SortCardsInDuelTempListByID", 0, t_SortCardsInDuelTempListByID },
	{ "GetCardIDFromDeckIndex", F_DE, t_GetCardIDFromDeckIndex, 60 },
	{ "RemoveCardFromDuelTempList", F_C, t_RemoveCardFromDuelTempList, 60 },
	{ "CountCardsInDuelTempList", F_A, t_CountCardsInDuelTempList },
	{ "LoadCardDataToBuffer1_FromDeckIndex", F_A, t_LoadCardDataToBuffer1_FromDeckIndex, 60 },
	{ "LoadCardDataToBuffer2_FromDeckIndex", F_A, t_LoadCardDataToBuffer2_FromDeckIndex, 60 },
	{ "EvolvePokemonCard", 0, t_EvolvePokemonCard },
	{ "CheckIfCanEvolveInto", F_C | F_Z, t_CheckIfCanEvolveInto, 0, 0, 0, 0 },
	{ "CheckIfCanEvolveInto_BasicToStage2", F_C, t_CheckIfCanEvolveInto_BasicToStage2 },
	{ "ClearAllStatusConditions", 0, t_ClearAllStatusConditions },
	{ "PutHandPokemonCardInPlayArea", F_C, t_PutHandPokemonCardInPlayArea, 60 },
	{ "MovePlayAreaCardToDiscardPile", 0, t_MovePlayAreaCardToDiscardPile },
	{ "EmptyPlayAreaSlot", 0, t_EmptyPlayAreaSlot },
	{ "ShiftAllPokemonToFirstPlayAreaSlots", 0, t_ShiftAllPokemonToFirstPlayAreaSlots },
	{ "SwapArenaWithBenchPokemon", 0, t_SwapArenaWithBenchPokemon },
	{ "SwapPlayAreaPokemon", 0, t_SwapPlayAreaPokemon },
	{ "GetPlayAreaCardAttachedEnergies", 0, t_GetPlayAreaCardAttachedEnergies },
	{ "CopyAttackDataAndDamage_FromDeckIndex", 0, t_CopyAttackDataAndDamage_FromDeckIndex },
	{ "UpdateArenaCardIDsAndClearTwoTurnDuelVars", 0, t_UpdateArenaCardIDsAndClearTwoTurnDuelVars },
	{ "ApplyDamageModifiers_DamageToSelf", F_DE, t_ApplyDamageModifiers_DamageToSelf },
	{ "GetCardDamageAndMaxHP", F_A, t_GetCardDamageAndMaxHP },
	{ "CheckLoadedAttackFlag", F_C, t_CheckLoadedAttackFlag, 24 },
	{ "SwapTurn", 0, t_SwapTurn },
	{ "HandleDoubleDamageSubstatus", F_DE, t_HandleDoubleDamageSubstatus },
	{ "HandleDamageReductionExceptSubstatus2", F_DE, t_HandleDamageReductionExceptSubstatus2 },
	{ "HandleCantAttackSubstatus", F_C, t_HandleCantAttackSubstatus },
	{ "HandleAmnesiaSubstatus", F_C, t_HandleAmnesiaSubstatus },
	{ "CheckSandAttackOrSmokescreenSubstatus", F_C, t_CheckSandAttackOrSmokescreenSubstatus },
	{ "CheckNoDamageOrEffect", F_C, t_CheckNoDamageOrEffect },
	{ "IsClairvoyanceActive", F_C, t_IsClairvoyanceActive },
	{ "CheckIsIncapableOfUsingPkmnPower", F_C, t_CheckIsIncapableOfUsingPkmnPower, 6 },
	{ "CountPokemonWithActivePkmnPowerInBothPlayAreas", F_A | F_C, t_CountPokemonWithActivePkmnPowerInBothPlayAreas },
	{ "GetLoadedCard1RetreatCost", F_A, t_GetLoadedCard1RetreatCost },
	{ "CheckUnableToRetreatDueToEffect", F_C, t_CheckUnableToRetreatDueToEffect },
	{ "CheckCantUseTrainerDueToEffect", F_C, t_CheckCantUseTrainerDueToEffect },
	{ "IsPrehistoricPowerActive", F_C, t_IsPrehistoricPowerActive },
	{ "ClearDamageReductionSubstatus2", 0, t_ClearDamageReductionSubstatus2 },
	{ "UpdateSubstatusConditions_StartOfTurn", 0, t_UpdateSubstatusConditions_StartOfTurn },
	{ "UpdateSubstatusConditions_EndOfTurn", 0, t_UpdateSubstatusConditions_EndOfTurn },
	{ "IsRainDanceActive", F_C, t_IsRainDanceActive },
	{ "CheckRainDanceScenario", F_C, t_CheckRainDanceScenario },
	{ "ClearChangedTypesIfMuk", 0, t_ClearChangedTypesIfMuk, 60 },
	{ "GetPlayAreaCardColor", F_A, t_GetPlayAreaCardColor, 6 },
	{ "GetPlayAreaCardWeakness", F_A, t_GetPlayAreaCardWeakness, 6 },
	{ "GetPlayAreaCardResistance", F_A, t_GetPlayAreaCardResistance, 6 },
	{ "HandleEnergyBurn", 0, t_HandleEnergyBurn },
	{ "CheckIfEnoughEnergiesForGivenAttack", F_C, t_CheckIfEnoughEnergiesForGivenAttack },
	{ "CheckIfActiveCardParalyzedOrAsleep", F_C, t_CheckIfActiveCardParalyzedOrAsleep },
	{ "CheckIfEnoughEnergiesToRetreat", F_C, t_CheckIfEnoughEnergiesToRetreat },
	{ "GetCardOneStageBelow", F_C, t_GetCardOneStageBelow },
	{ "CountKnockedOutPokemon", F_C, t_CountKnockedOutPokemon },
	{ "CheckIfTurnDuelistPlayAreaPokemonAreAllKnockedOut", F_C, t_CheckIfTurnDuelistPlayAreaPokemonAreAllKnockedOut },
	{ "ClearNonTurnTemporaryDuelvars_CopyStatus", 0, t_ClearNonTurnTemporaryDuelvars_CopyStatus },
	{ "UpdateArenaCardLastTurnDamage", 0, t_UpdateArenaCardLastTurnDamage },
	{ "SetAllPlayAreaPokemonCanEvolve", 0, t_SetAllPlayAreaPokemonCanEvolve },
	{ "HasAlivePokemonInPlayArea", F_C, t_HasAlivePokemonInPlayArea },
	{ "HasAlivePokemonInBench", F_C, t_HasAlivePokemonInBench },
	{ "TakeAPrizes", 0, t_TakeAPrizes, 7 },
	{ "AddToDamage", 0, t_AddToDamage },
	{ "SubtractFromDamage", 0, t_SubtractFromDamage },
	{ "CheckIfEnoughEnergiesToAttack", F_C, t_CheckIfEnoughEnergiesToAttack },
	{ "TranslateColorToWR", F_A, t_TranslateColorToWR },
};

static int run_unit_test(const struct UnitTest *t, int iters)
{
	int fails = 0;
	for (int it = 0; it < iters && fails < 3; it++) {
		random_state();
		uint8_t a = (uint8_t)(t->a_mod ? xr() % t->a_mod : xr());
		uint8_t b = (uint8_t)xr(), c = (uint8_t)xr();
		uint16_t de;
		if (!strcmp(t->rom, "CheckIfCanEvolveInto") || !strcmp(t->rom, "CheckIfCanEvolveInto_BasicToStage2") ||
		    !strcmp(t->rom, "CopyAttackDataAndDamage_FromDeckIndex") || !strcmp(t->rom, "CheckIfEnoughEnergiesForGivenAttack"))
			de = (uint16_t)((xr() % 60) << 8 | (xr() % (strcmp(t->rom, "CheckIfCanEvolveInto") && strcmp(t->rom, "CheckIfCanEvolveInto_BasicToStage2") ? 2 : 6)));
		else if (!strcmp(t->rom, "SwapPlayAreaPokemon"))
			de = (uint16_t)((xr() % 6) << 8 | (xr() % 6));
		else if (!strcmp(t->rom, "HandleDoubleDamageSubstatus") || !strcmp(t->rom, "HandleDamageReductionExceptSubstatus2"))
			de = (uint16_t)(xr() % 4 == 0 ? xr() : 10 * (xr() % 16));
		else
			de = (uint16_t)(xr() % 6);
		if (!strcmp(t->rom, "CheckIfEnoughEnergiesToAttack")) {
			/* attack list pairs in wDuelTempList, attack chosen in hCurMenuItem */
			gb->wram[ADDR_wDuelTempList - WRAM_START + 0] = gb->wram[ADDR_wDuelTempList - WRAM_START + 2] = (uint8_t)(xr() % 60);
			gb->wram[ADDR_wDuelTempList - WRAM_START + 1] = 0;
			gb->wram[ADDR_wDuelTempList - WRAM_START + 3] = 1;
			gb->hram[ADDR_hCurMenuItem - HRAM_START] = (uint8_t)(xr() % 2);
			sync_to_port();
		}
		gb->a = a;
		gb->b = b;
		gb->c = c;
		gb->d = (uint8_t)(de >> 8);
		gb->e = (uint8_t)de;
		gb->f = 0;
		call_rom(t->rom);
		struct Out want = { gb->a, (gb->f & 0x10) != 0, (uint16_t)(gb->d << 8 | gb->e), (gb->f & 0x80) != 0 };
		struct Out got = { 0 };
		t->port(&got, a, b, c, de);
		char what[128];
		snprintf(what, sizeof(what), "%s#%d", t->rom, it);
		int bad = compare_memory(what);
		if ((t->flags & F_A) && want.a != got.a) {
			fprintf(stderr, "%s: a port %02x rom %02x\n", what, got.a, want.a);
			bad++;
		}
		if ((t->flags & F_C) && want.c != got.c) {
			fprintf(stderr, "%s: carry port %d rom %d\n", what, got.c, want.c);
			bad++;
		}
		if ((t->flags & F_DE) && want.de != got.de) {
			fprintf(stderr, "%s: de port %04x rom %04x\n", what, got.de, want.de);
			bad++;
		}
		if ((t->flags & F_Z) && want.c && want.z != got.z) {
			fprintf(stderr, "%s: z port %d rom %d\n", what, got.z, want.z);
			bad++;
		}
		if (bad) {
			fprintf(stderr, "  inputs: a=%02x b=%02x c=%02x de=%04x\n", a, b, c, de);
			fails++;
		}
	}
	return fails;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s ROM SYM [iterations]\n", argv[0]);
		return 1;
	}
	int iters = argc > 3 ? atoi(argv[3]) : 2000;
	FILE *f = fopen(argv[1], "rb");
	if (!f) {
		perror(argv[1]);
		return 1;
	}
	static uint8_t rom[0x200000];
	size_t n = fread(rom, 1, sizeof(rom), f);
	fclose(f);
	load_sym(argv[2]);
	gb = gb_new(rom, n);
	gb->render = 0;
	trap_addr = sym("PowersOf2")->addr;
	gb_add_breakpoint(gb, -1, trap_addr);
	/* let the game initialise the hardware and RAM */
	for (int i = 0; i < 120; i++)
		gb_run(gb, 1 << 30, 1);
	int fails = 0;
	fails += test_rng(iters);
	const char *only = argc > 4 ? argv[4] : NULL;
	for (unsigned i = 0; i < sizeof(tests) / sizeof(*tests); i++) {
		if (only && strcmp(only, tests[i].rom))
			continue;
		int f = run_unit_test(&tests[i], iters);
		printf("%-50s %s\n", tests[i].rom, f ? "FAIL" : "ok");
		fails += f;
	}
	printf("unitdiff: %s\n", fails ? "FAILED" : "all tests passed");
	return fails != 0;
}
