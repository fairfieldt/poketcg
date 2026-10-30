/*
 * ref.h - reference side of the differential test harness.
 *
 * Runs duels in the ORIGINAL Pokemon TCG ROM inside the headless DMG core
 * (gb/gb.h) and reports synchronisation events (trace points, RNG calls,
 * human input requests, duel end) so that the C port can be compared against
 * it in lockstep.
 *
 * Typical use:
 *
 *     Ref *r = ref_new("poketcg.gbc", "poketcg.sym");
 *     ref_trace_add(r, "DrawCardFromDeck");
 *     ref_trace_rng(r, 1);
 *     RefDuel d;
 *     ref_duel_defaults(&d);
 *     d.player_deck_id = 5; d.opp_deck_id = 8;   // *_DECK_ID values
 *     ref_start_duel(r, &d);
 *     RefEvent ev;
 *     while (ref_next(r, &ev) < REF_EV_END) { ... }
 *
 * Everything the library writes into the machine to set a duel up is listed
 * in ref.c (see "Hijack") and in README-style comments below; the C port must
 * start from the same values.
 */
#ifndef REF_H
#define REF_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "gb/gb.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Ref Ref;

#define REF_DECK_SIZE 60
#define REF_STACK_MAX 48
#define REF_LIST_MAX 64

/* ------------------------------------------------------------------------ */
/* lifetime, symbols                                                         */

/* Loads ROM + .sym, boots the ROM to the title screen (first entry of
 * HandleTitleScreen, i.e. after Start/GameLoop/ValidateSRAM have run on a
 * blank SRAM) and keeps that machine state as the snapshot every duel starts
 * from. Returns NULL on failure (message on stderr). */
Ref *ref_new(const char *rom_path, const char *sym_path);
void ref_free(Ref *ref);
GB  *ref_gb(Ref *ref);

/* Symbol lookup by exact name (global or local, e.g. "DisplayCardList.wait_button",
 * WRAM/HRAM/SRAM labels too). bank: ROM bank (0 for ROM0/RAM labels, SRAM
 * bank for SRAM labels). Returns 0 if found, -1 otherwise. */
int  ref_sym(Ref *ref, const char *name, int *bank, uint16_t *addr);
/* Nearest ROM label at or below (bank, addr): "Name" or "Name+off" (local labels
 * included). bank is ignored for addr < $4000. Returns buf. */
const char *ref_addr_name(Ref *ref, int bank, uint16_t addr, char *buf, size_t n);

/* Expands deck `deck` (a *_DECK constant = *_DECK_ID + 2) from DeckPointers in
 * the ROM exactly like CopyDeckData: (count, card id) pairs until a 0 count.
 * Returns the number of cards (60 for every valid deck), -1 for an invalid /
 * NULL deck pointer. Cards past 60 are not written. */
int  ref_rom_deck(Ref *ref, int deck, uint8_t out[REF_DECK_SIZE]);

/* ------------------------------------------------------------------------ */
/* duel setup                                                                */

typedef struct RefDuel {
	/* PLAYER_TURN side. If player_human == 0 it is an AI using the deck AI of
	 * player_deck_id: wPlayerDuelistType = DUELIST_TYPE_AI_OPP | player_deck_id.
	 * NOTE: InitVariablesToBeginDuel sets wDuelType = [wPlayerDuelistType]
	 * when it has bit 7 set, so player_deck_id 0 (SAMS_PRACTICE_DECK_ID) makes
	 * wDuelType == DUELTYPE_PRACTICE ($80): decks are not shuffled and the
	 * "practice" texts are shown (wIsPracticeDuel stays 0, so no practice
	 * instructions and no 15-turn limit). Avoid deck id 0 on the player side.
	 * If player_human != 0 (phase 2) the side is DUELIST_TYPE_PLAYER, the duel
	 * starts at StartDuel_VSAIOpp, and the player's deck is put in SRAM
	 * (sDeck1Cards, sCurrentlySelectedDeck = 0). */
	uint8_t player_human;
	uint8_t player_deck_id;
	uint8_t opp_deck_id;        /* OPPONENT_TURN side, always AI: wOpponentDeckID / wNPCDuelDeckID */
	/* 60 card ids each. If player_deck[0] == 0 (resp. opp_deck[0]) the ROM list
	 * of player_deck_id + 2 (resp. opp_deck_id + 2) is used (ref_rom_deck). */
	uint8_t player_deck[REF_DECK_SIZE];
	uint8_t opp_deck[REF_DECK_SIZE];
	uint8_t prizes;             /* wNPCDuelPrizes (default 6) */
	uint8_t duel_theme;         /* wDuelTheme (default MUSIC_DUEL_THEME_1 = $02) */
	uint8_t rng[3];             /* wRNG1, wRNG2, wRNGCounter */
	uint8_t animations_disabled;/* wAnimationsDisabled (+ sAnimationsDisabled), default 0 */
	uint8_t text_speed;         /* wTextSpeed (+ sTextSpeed), default 0 = TEXT_SPEED_5 (no delay) */
	uint16_t opp_name;          /* wOpponentName text id, default 0 (CopyOpponentName prints "Player2"-style default) */
	uint8_t opp_portrait;       /* wOpponentPortrait, default 0 */
	uint8_t per_side_ai;        /* default 1: AIDoAction hook (see below) */
	uint8_t report_acks;        /* default 0: also report TCG_REQ_ACK inputs (auto-answered with A) */
	uint32_t hang_frames;       /* default 3600: no progress for this many frames -> REF_EV_HANG */
} RefDuel;

void ref_duel_defaults(RefDuel *d);

/* Restores the boot snapshot and hijacks the CPU into the duel engine:
 *  AI vs AI:  jumps to StartDuel (bank 1) with decks/duelist types already set.
 *  human:     jumps to StartDuel_VSAIOpp (bank 1), which loads the decks itself.
 * A trap return address (PowersOf2, a data table in ROM0 that is never executed)
 * is on the stack; the duel is over when StartDuel returns to it.
 * Returns 0 on success, -1 on error (message on stderr). */
int  ref_start_duel(Ref *ref, const RefDuel *d);

/* Per-side AI (RefDuel.per_side_ai): the original AIDoAction always dispatches
 * on wOpponentDeckID. At every entry of AIDoAction the harness saves
 * wOpponentDeckID and, if the turn duelist's DUELVARS_DUELIST_TYPE
 * ([hWhoseTurn:$f1]) has bit 7 set (AI), writes wOpponentDeckID = type & $7f.
 * When that AIDoAction call returns (PC == the return address that was at [SP]
 * on entry, with SP == entry SP + 2) the saved value is written back. Calls
 * nest (AIDoAction_ForcedSwitch for the defending AI during the other AI's
 * turn). Code that reads wOpponentDeckID outside AIDoAction sees the value set
 * at duel start (the OPPONENT_TURN side's deck id). */

/* ------------------------------------------------------------------------ */
/* tracing                                                                   */

/* Registers a trace point (PC breakpoint) at a ROM symbol. Returns its id
 * (>= 0) or -1 if the symbol is unknown. Trace points persist across duels. */
int  ref_trace_add(Ref *ref, const char *name);
int  ref_trace_add_addr(Ref *ref, int bank, uint16_t addr, const char *label);
void ref_trace_clear(Ref *ref);
/* Report every call to UpdateRNGSources as a REF_EV_RNG event, at the moment
 * it returns (ev.rng_value = a, ev.rng = new wRNG1/wRNG2/wRNGCounter). */
void ref_trace_rng(Ref *ref, int on);
/* Write a PNG of the screen to dir/NNNNNNN.png every `every` frames (0 = off).
 * Also writes dir/hang.png / dir/error.png on those events. */
void ref_set_screens(Ref *ref, const char *dir, int every);

/* ------------------------------------------------------------------------ */
/* events                                                                    */

enum RefEventKind {
	REF_EV_TRACE = 1,   /* a trace point was reached (before its first instruction) */
	REF_EV_RNG,         /* UpdateRNGSources returned */
	REF_EV_INPUT,       /* a human input routine wants an answer (ref_answer) */
	REF_EV_END,         /* StartDuel returned: duel over */
	REF_EV_HANG,        /* no progress for hang_frames frames */
	REF_EV_ERROR,       /* emulator error, game reset, unexpected input routine, bad answer, ... */
};

/* input routine kinds: same numeric values as TCG_REQ_* in port/src/tcg.h */
enum RefInputType {
	REF_IN_ACK = 1,          /* WaitForWideTextBoxInput & co (auto-answered with A) */
	REF_IN_DUEL_MENU,        /* PrintDuelMenuAndHandleInput: 0 Hand 1 Attack 2 Check 3 Pkmn Power 4 Retreat 5 Done */
	REF_IN_CARD_LIST,        /* DisplayCardList: index into wDuelTempList */
	REF_IN_CARD_ITEM_MENU,   /* CardListItemSelectionMenu: 0 Play/Select, 1 Check */
	REF_IN_PLAY_AREA,        /* DisplayPlayAreaScreen: play area location */
	REF_IN_MENU,             /* generic HandleMenuInput loop: item index */
	REF_IN_YES_NO,           /* HandleYesOrNoMenu: 1 yes, 0 no */
	REF_IN_ENERGY_DISCARD,   /* HandleEnergyDiscardMenuInput: index into wDuelTempList */
	REF_IN_PRIZE,            /* SelectPrizeCards: prize slot */
	REF_IN_COLOR,            /* HandleColorChangeScreen: color 0-5 */
};

#define REF_CANCEL (-1)

typedef struct RefInput {
	int type;                /* RefInputType */
	const char *routine;     /* name of the original routine */
	const char *caller;      /* routine that called it (nearest global label of the return address) */
	int cursor;              /* current (default) item, in answer units */
	int first;               /* answers are first .. first + num_items - 1 (first != 0 only for PLAY_AREA) */
	int num_items;           /* number of selectable answers */
	int can_cancel;          /* 1 if REF_CANCEL (B) is accepted by the routine */
	int view_only;           /* CARD_LIST / PLAY_AREA opened for viewing: A opens the card page instead of selecting */
	int list_len;            /* wDuelTempList entries ($ff-terminated) when relevant, else 0 */
	uint8_t list[REF_LIST_MAX];
	uint8_t whose_turn;      /* hWhoseTurn when the request was made */
	uint16_t text;           /* CARD_LIST: wCardListHeaderText, else 0 */
	uint16_t text2;          /* CARD_LIST: wCardListInfoBoxText, else 0 */
} RefInput;

typedef struct RefFrame {
	const char *name;        /* nearest label at/below target ("?" if none) */
	uint16_t off;            /* target - label address */
	uint16_t target, ret, sp;
	uint8_t bank, irq;
} RefFrame;

typedef struct RefEvent {
	int kind;                /* RefEventKind */
	const char *name;        /* trace: symbol name; rng: "UpdateRNGSources"; input: routine; else "" */
	int trace_id;            /* REF_EV_TRACE: id from ref_trace_add */
	uint64_t frame;          /* frames since ref_start_duel */
	uint64_t cycles;         /* T-cycles since ref_start_duel */
	struct { uint8_t a, f, b, c, d, e, h, l; uint16_t sp, pc; uint8_t bank; } regs;
	uint8_t whose_turn;      /* hWhoseTurn */
	uint8_t rng_value;       /* REF_EV_RNG: a returned by UpdateRNGSources */
	uint8_t rng[3];          /* wRNG1, wRNG2, wRNGCounter (current) */
	int depth;               /* shadow call stack depth (stack[0] = outermost) */
	RefFrame stack[REF_STACK_MAX];  /* innermost REF_STACK_MAX frames if deeper */
	const uint8_t *wram;     /* live 8KB WRAM ($c000); valid until the next ref_* call that runs the CPU */
	const uint8_t *hram;     /* live HRAM ($ff80) */
	RefInput input;          /* REF_EV_INPUT */
	/* REF_EV_END */
	uint8_t duel_finished;   /* wDuelFinished (TURN_PLAYER_WON/LOST/TIED from the last turn holder's view) */
	uint8_t duel_result;     /* wDuelResult: 0 DUEL_WIN (PLAYER_TURN side won), 1 DUEL_LOSS */
	uint8_t duel_turns;      /* wDuelTurns */
	char msg[512];           /* human-readable details (hang / error / end) */
} RefEvent;

/* Runs the duel until the next event and fills *ev. Returns ev->kind.
 * After REF_EV_END / REF_EV_HANG / REF_EV_ERROR the duel is stopped and further
 * calls return the same kind again. After REF_EV_INPUT, ref_answer must be
 * called before ref_next (except for REF_IN_ACK, which is auto-answered). */
int  ref_next(Ref *ref, RefEvent *ev);

/* Answers the pending REF_EV_INPUT: choice in [0, num_items) or REF_CANCEL.
 * The answer is given to the original only as joypad presses (cursor moves +
 * A, or B). Returns 0 if accepted, -1 if there is no pending input or the
 * choice is out of range. */
int  ref_answer(Ref *ref, int choice);

/* debugging helpers */
void ref_print_stack(Ref *ref, FILE *fp);
const char *ref_input_type_name(int type);
const char *ref_event_kind_name(int kind);
uint64_t ref_frame(Ref *ref);   /* frames since duel start */

#ifdef __cplusplus
}
#endif
#endif /* REF_H */
