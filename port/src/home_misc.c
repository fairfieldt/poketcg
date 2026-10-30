/* Port of the small src/home/ helpers used by the duel engine:
 * card_data.asm, card_color.asm, damage.asm, random.asm, math.asm,
 * effect_commands.asm, load_deck.asm and the memory model helpers. */
#include "engine.h"
#include "gen/cards.h"
#include "gen/decks.h"
#include "gen/effect_commands.h"
#include "gen/rom_blobs.h"

u8 gWRAM[WRAM_SIZE];
u8 gHRAM[HRAM_SIZE];
u16 gTextID;

u8 *gbptr(u16 addr)
{
	if (addr >= WRAM_START && addr < WRAM_START + WRAM_SIZE)
		return &gWRAM[addr - WRAM_START];
	if (addr >= 0xe000 && addr < 0xfe00) /* echo RAM */
		return &gWRAM[addr - 0xe000];
	if (addr >= HRAM_START)
		return &gHRAM[addr - HRAM_START];
	tcg_fatal("gbptr: address outside of WRAM/HRAM");
	return gWRAM;
}

u16 gbaddr(const u8 *p)
{
	if (p >= gWRAM && p < gWRAM + WRAM_SIZE)
		return (u16)(WRAM_START + (p - gWRAM));
	if (p >= gHRAM && p < gHRAM + HRAM_SIZE)
		return (u16)(HRAM_START + (p - gHRAM));
	tcg_fatal("gbaddr: pointer outside of WRAM/HRAM");
	return 0;
}

/* ------------------------------------------------------------------ card_data.asm */

/* GetCardPointer: return carry if card id e is out of bounds */
static const u8 *GetCardPointer(u8 e)
{
	if (e > NUM_CARDS)
		return NULL;
	return gCardData[e];
}

static bool LoadCardDataToHL_FromCardID(struct CardData *dst, u8 e)
{
	const u8 *src = GetCardPointer(e);
	if (!src)
		return true;
	memcpy(dst, src, PKMN_CARD_DATA_LENGTH);
	return false;
}

/* load data of card with text id of name at de to wLoadedCard1 */
void LoadCardDataToBuffer1_FromName(u16 name)
{
	for (u8 id = 1; id <= NUM_CARDS; id++) {
		const u8 *c = gCardData[id];
		if (c[CARD_DATA_NAME] == (u8)name && c[CARD_DATA_NAME + 1] == (u8)(name >> 8)) {
			memcpy(&wLoadedCard1, c, PKMN_CARD_DATA_LENGTH);
			return;
		}
	}
}

/* load data of card with id e to wLoadedCard2; returns carry if out of bounds */
bool LoadCardDataToBuffer2_FromCardID(u8 e)
{
	return LoadCardDataToHL_FromCardID(&wLoadedCard2, e);
}

/* load data of card with id e to wLoadedCard1; returns carry if out of bounds */
bool LoadCardDataToBuffer1_FromCardID(u8 e)
{
	return LoadCardDataToHL_FromCardID(&wLoadedCard1, e);
}

/* return the type (TYPE_* constant) of the card with id e */
u8 GetCardType(u8 e)
{
	const u8 *c = GetCardPointer(e);
	if (!c)
		return e; /* out of bounds: a is left as computed by GetCardPointer */
	return c[CARD_DATA_TYPE];
}

/* return the 2-byte text id of the name of the card with id e */
u16 GetCardName(u8 e)
{
	const u8 *c = GetCardPointer(e);
	if (!c)
		return e;
	return rd16(&c[CARD_DATA_NAME]);
}

/* from the card id, returns type (a), rarity (b) and set (c) */
u8 GetCardTypeRarityAndSet(u8 id, u8 *rarity, u8 *set)
{
	const u8 *c = GetCardPointer(id);
	if (!c)
		return id;
	if (rarity)
		*rarity = c[CARD_DATA_RARITY];
	if (set)
		*set = c[CARD_DATA_SET];
	return c[CARD_DATA_TYPE];
}

/* ------------------------------------------------------------------ card_color.asm */

/* return the turn holder's arena card's color, accounting for Venomoth's Shift Pokemon Power if active */
u8 GetArenaCardColor(void)
{
	return GetPlayAreaCardColor(PLAY_AREA_ARENA);
}

/* return the turn holder's card's color at play area location e,
 * accounting for Venomoth's Shift Pokemon Power if active */
u8 GetPlayAreaCardColor(u8 e)
{
	if (GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD_CHANGED_TYPE)) & HAS_CHANGED_COLOR) {
		if (!CheckIsIncapableOfUsingPkmnPower(e))
			return GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD_CHANGED_TYPE)) & 0xf;
	}
	/* regular_color */
	u8 a = GetCardType(GetCardIDFromDeckIndex(GetTurnDuelistVariable((u8)(e + DUELVARS_ARENA_CARD))));
	if (a == TYPE_TRAINER)
		a = COLORLESS;
	return a;
}

static u8 GetCardWeakness(u8 off)
{
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable(off));
	return wLoadedCard2.Weakness;
}

static u8 GetCardResistance(u8 off)
{
	LoadCardDataToBuffer2_FromDeckIndex(GetTurnDuelistVariable(off));
	return wLoadedCard2.Resistance;
}

/* return the weakness of the turn holder's arena or benchx Pokemon given the PLAY_AREA_* value
 * if arena and [DUELVARS_ARENA_CARD_CHANGED_WEAKNESS] != 0, return it instead */
u8 GetPlayAreaCardWeakness(u8 loc)
{
	if (loc == 0)
		return GetArenaCardWeakness();
	return GetCardWeakness((u8)(loc + DUELVARS_ARENA_CARD));
}

u8 GetArenaCardWeakness(void)
{
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_CHANGED_WEAKNESS);
	if (a)
		return a;
	return GetCardWeakness(DUELVARS_ARENA_CARD);
}

u8 GetPlayAreaCardResistance(u8 loc)
{
	if (loc == 0)
		return GetArenaCardResistance();
	return GetCardResistance((u8)(loc + DUELVARS_ARENA_CARD));
}

u8 GetArenaCardResistance(void)
{
	u8 a = GetTurnDuelistVariable(DUELVARS_ARENA_CARD_CHANGED_RESISTANCE);
	if (a)
		return a;
	return GetCardResistance(DUELVARS_ARENA_CARD);
}

/* this function checks if turn holder's CHARIZARD energy burn is active, and if so, turns
 * all energies at wAttachedEnergies except double colorless energies into fire energies */
void HandleEnergyBurn(void)
{
	if (GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD)) != CHARIZARD)
		return;
	if (CheckIsIncapableOfUsingPkmnPower(PLAY_AREA_ARENA))
		return;
	for (u8 i = 0; i < NUM_COLORED_TYPES; i++)
		wAttachedEnergies[i] = 0;
	wAttachedEnergies[FIRE] = wTotalAttachedEnergies;
}

/* ------------------------------------------------------------------ damage.asm, math.asm */

/* [wDamage] += a */
void AddToDamage(u8 a)
{
	wr16(wDamage, (u16)(rd16(wDamage) + a));
}

/* [wDamage] -= a */
void SubtractFromDamage(u8 a)
{
	wr16(wDamage, (u16)(rd16(wDamage) - a));
}

u8 ATimes10(u8 a)
{
	return (u8)(a * 10);
}

u16 HLTimes10(u16 hl)
{
	return (u16)(hl * 10);
}

/* cp de, bc: returns carry if de < bc */
bool CompareDEtoBC(u16 de, u16 bc)
{
	return de < bc;
}

/* ------------------------------------------------------------------ random.asm */

/* returns h * l */
u16 HtimesL(u8 h, u8 l)
{
	return (u16)(h * l);
}

/* return a random number between 0 and a (exclusive) */
u8 Random(u8 a)
{
	u8 l = UpdateRNGSources();
	return (u8)(HtimesL(a, l) >> 8);
}

/* get the next random numbers of the wRNG1 and wRNG2 sequences */
u8 UpdateRNGSources(void)
{
	u8 r1 = wRNG1, r2 = wRNG2;
	u8 rot = (u8)((r2 << 2) | (r2 >> 6));
	u8 c = (rot ^ r1) & 1;
	u8 d = r2 ^ r1;
	u8 e = wRNGCounter ^ r1;
	u8 c2 = e >> 7;
	e = (u8)((e << 1) | c);
	d = (u8)((d << 1) | c2);
	wRNGCounter++;
	wRNG2 = d;
	wRNG1 = e;
	TRACE_RNG((u8)(d ^ e));
	return d ^ e;
}

/* ------------------------------------------------------------------ effect_commands.asm */

const struct EffectCommandList *GetEffectCommandList(u16 address)
{
	int lo = 0, hi = NUM_EFFECT_COMMAND_LISTS - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		u16 a = gEffectCommandLists[mid].address;
		if (a == address)
			return &gEffectCommandLists[mid];
		if (a < address)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return NULL;
}

/* a byte of the Game Boy address space as CheckMatchingCommand sees it (ROM bank 6
 * mapped at $4000). Garbage command list pointers do occur: e.g. the AI loads the
 * "attack" of a non-Pokemon card, whose record tail belongs to the next card. */
static u8 EffectCommandsBusRead(u16 a)
{
	if (a < 0x4000)
		return *RomPtr(0x00, a);
	if (a < 0x8000)
		return *RomPtr(0x06, a);
	if (a < 0xa000)
		return 0; /* VRAM: not modelled */
	if (a < 0xc000)
		return 0xff; /* SRAM is disabled during duels: open bus */
	if (a < 0xfe00)
		return gWRAM[(a - 0xc000) & 0x1fff];
	if (a >= 0xff80 && a < 0xffff)
		return gHRAM[a - 0xff80];
	return 0xff; /* OAM, IO: not modelled */
}

static EffectFunc EffectFunctionAt(u16 address)
{
	int lo = 0, hi = NUM_EFFECT_FUNCTIONS - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		u16 a = gEffectFunctionsByAddress[mid].address;
		if (a == address)
			return gEffectFunctionsByAddress[mid].func;
		if (a < address)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	static char msg[48] = "jump to a non-effect function at $0b:";
	static const char hex[] = "0123456789abcdef";
	for (int i = 0; i < 4; i++)
		msg[37 + i] = hex[(address >> (12 - 4 * i)) & 0xf];
	msg[41] = 0;
	tcg_fatal(msg);
	return NULL;
}

/* CheckMatchingCommand: return the function for command type in the list at the
 * Game Boy address `list`, or NULL (carry) if not found */
EffectFunc CheckMatchingCommand(u16 list, u8 type)
{
	if (list == 0)
		return NULL; /* return carry if pointer is NULL */
	/* store the bank number of command functions ($b) in wEffectFunctionsBank */
	wEffectFunctionsBank = 0x0b;
	const struct EffectCommandList *l = GetEffectCommandList(list);
	if (l) {
		for (const struct EffectCommand *c = l->cmds; c->type; c++)
			if (c->type == type)
				return c->func;
		return NULL;
	}
	/* not one of the effect command lists: scan memory like the original does */
	for (u16 p = list;; p = (u16)(p + 3)) {
		u8 t = EffectCommandsBusRead(p);
		if (t == 0)
			return NULL;
		if (t == type) {
			u16 f = (u16)(EffectCommandsBusRead((u16)(p + 1)) | EffectCommandsBusRead((u16)(p + 2)) << 8);
			return EffectFunctionAt(f);
		}
	}
}

EffectFunc GetEffectCommandFunction(u16 list, u8 type)
{
	return CheckMatchingCommand(list, type);
}

/* Checks if the command type is one of the commands of the attack or card effect
 * currently in use (wLoadedAttackEffectCommands), and executes its associated
 * function if so. returns the function's carry; nc if there was no such command. */
/* register b and the a an effect function returned, modelled only where the original
 * reads a b that a callee left in it (see AIDecide_GustOfWind .check_bench_energy):
 * - after TryExecuteEffectCommandFunction ran a function, b = its a (push af / pop bc);
 *   the AI effect functions' a is set by the helpers they end with
 *   (SetExpectedAIDamage...), the others leave the bank number they are called with
 * - CalculateDamage_VersusDefendingPokemon sets b = CARD_LOCATION_ARENA when the
 *   Defending Pokemon can be damaged, and HandleDamageReductionExceptSubstatus2's
 *   Invisible Wall / PREVENT_LESS_THAN_40 checks set b = 0 (ld bc, 30/40) */
u8 gRegB;
u8 gEffectFunctionA;

bool TryExecuteEffectCommandFunction(u8 type)
{
	EffectFunc f = CheckMatchingCommand(rd16(wLoadedAttack.EffectCommands), type);
	if (!f)
		return false;
	gEffectFunctionA = BANK_EFFECT_FUNCTIONS; /* a = [wEffectFunctionsBank] at CallHL */
	bool carry = f();
	gRegB = gEffectFunctionA;
	return carry;
}

/* ------------------------------------------------------------------ load_deck.asm */

/* loads the deck id from DeckPointers and copies it to wPlayerDeck or to
 * wOpponentDeck, depending on whose turn it is.
 * sets carry flag if an invalid deck id is used. */
bool LoadDeck(u8 deck)
{
	if (deck >= NUM_VALID_DECKS || !gDeckLists[deck])
		return true;
	CopyDeckData(gDeckLists[deck]);
	return false;
}
