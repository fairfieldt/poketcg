/*
 * Common definitions for the C port of the Pokémon TCG duel engine.
 *
 * Memory model
 * ------------
 * The original engine keeps all of its state in Game Boy WRAM ($c000-$dfff)
 * and HRAM ($ff80-$fffe). The port keeps the exact same layout in two byte
 * arrays, gWRAM and gHRAM, and gen/wram.h exposes every original label as a
 * macro with the same name (wDuelTurns, hWhoseTurn, wLoadedCard1, ...).
 * This keeps every variable's size, aliasing (UNIONs) and adjacency identical
 * to the original, so out-of-bounds quirks reproduce, state can be compared
 * byte for byte against the original game, and snapshots are a memcpy.
 *
 * 16-bit values are stored little-endian, as on the Game Boy; use rd16/wr16.
 * Pointers that the original stores in WRAM (e.g. wLoadedAttackEffectCommands,
 * hTempListPtr_ff99) are stored as their original 16-bit Game Boy addresses
 * and translated with gbptr()/gbaddr() or the data lookup helpers.
 */
#ifndef TCG_COMMON_H
#define TCG_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;

#include "gen/constants.h"

#define WRAM_START 0xc000
#define WRAM_SIZE  0x2000
#define HRAM_START 0xff80
#define HRAM_SIZE  0x80

extern u8 gWRAM[WRAM_SIZE];
extern u8 gHRAM[HRAM_SIZE];

/* engine/duel structures (src/macros/wram.asm) */
struct AtkData {                /* atk_data_struct, $13 bytes */
	u8 EnergyCost[NUM_TYPES / 2];
	u8 Name[2];
	u8 Description[4];
	u8 Damage;
	u8 Category;
	u8 EffectCommands[2];
	u8 Flag1;
	u8 Flag2;
	u8 Flag3;
	u8 EffectParam;
	u8 Animation;
};

struct CardData {               /* card_data_struct, $41 bytes */
	u8 Type;
	u8 Gfx[2];
	u8 Name[2];
	u8 Rarity;
	u8 Set;
	u8 ID;
	union {
		u8 EffectCommands[2];   /* non-Pokemon cards */
		struct { u8 HP; u8 Stage; };
	};
	union {
		u8 NonPokemonDescription[2];
		u8 PreEvoName[2];
	};
	struct AtkData Atk1;
	struct AtkData Atk2;
	u8 RetreatCost;
	u8 Weakness;
	u8 Resistance;
	u8 Category[2];
	u8 PokedexNumber;
	u8 Unknown;
	u8 Level;
	u8 Length[2];
	u8 Weight[2];
	u8 Description[2];
	u8 AIInfo;
};

struct DuelAnim {               /* duel_anim_struct */
	u8 ID;
	u8 Screen;
	u8 DuelistSide;
	u8 LocationParam;
	u8 Damage[2];
	u8 Unknown2;
	u8 Bank;
};

_Static_assert(sizeof(struct AtkData) == CARD_DATA_ATTACK2 - CARD_DATA_ATTACK1, "AtkData size");
_Static_assert(sizeof(struct CardData) == PKMN_CARD_DATA_LENGTH, "CardData size");
_Static_assert(offsetof(struct CardData, Atk1) == CARD_DATA_ATTACK1, "CardData layout");
_Static_assert(offsetof(struct CardData, RetreatCost) == CARD_DATA_RETREAT_COST, "CardData layout");
_Static_assert(offsetof(struct CardData, AIInfo) == CARD_DATA_AI_INFO, "CardData layout");

#include "gen/wram.h"

static inline u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static inline void wr16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

/* translate a WRAM/HRAM Game Boy address to a host pointer and back */
u8 *gbptr(u16 addr);
u16 gbaddr(const u8 *p);

/* duel variables: the turn holder's block is at hWhoseTurn << 8 ($c2 or $c3) */
static inline u8 *DuelVarsOf(u8 turn) { return &gWRAM[(u16)(turn << 8) - WRAM_START]; }
static inline u8 *TurnDuelistVars(void) { return DuelVarsOf(hWhoseTurn); }
static inline u8 *NonTurnDuelistVars(void)
{
	return DuelVarsOf(hWhoseTurn == PLAYER_TURN ? OPPONENT_TURN : PLAYER_TURN);
}

/*
 * Debug/trace hook. Ported functions that are useful synchronisation points
 * for the differential test harness call TRACE_FN() on entry. It compiles to
 * nothing unless TCG_TRACE is defined.
 */
#ifdef TCG_TRACE
void tcg_trace_enter(const char *fn);
#define TRACE_FN() tcg_trace_enter(__func__)
#else
#define TRACE_FN() ((void)0)
#endif

/* fatal error in the engine (unreachable code in the original, invalid data, ...) */
void tcg_fatal(const char *msg);

#endif
