/*
 * Public interface of the portable Pokémon TCG duel engine.
 *
 * The engine is a faithful port of the original Game Boy duel engine. It runs
 * a whole duel synchronously (tcg_run_duel) and talks to the frontend through
 * two callbacks:
 *
 *   event()    output that needs no answer: text boxes, screens, animations,
 *              coin tosses, sounds. The frontend may block in it (e.g. to play
 *              an animation) but it must not change engine state.
 *   request()  a blocking question for a human duelist, one per original
 *              input routine (duel menu, card list, play area cursor, yes/no,
 *              ...). It returns the selected item, or TCG_CANCEL for B.
 *
 * Board state is read directly from the engine's memory (tcg_duelvars(), the
 * gen/wram.h macros, or the convenience accessors below).
 *
 * The engine is deterministic: given the decks, the RNG seed and the sequence
 * of answers to request(), a duel always plays out identically. Network play
 * runs the same engine on both ends and only exchanges request() answers.
 */
#ifndef TCG_H
#define TCG_H

#include <stdint.h>

#define TCG_CANCEL (-1)

/* who answers a request / who an event concerns */
#define TCG_PLAYER   0xc2  /* PLAYER_TURN: duelist 0 (wPlayerDuelVariables) */
#define TCG_OPPONENT 0xc3  /* OPPONENT_TURN: duelist 1 (wOpponentDuelVariables) */

enum TcgEventType {
	TCG_EV_TEXT = 1,      /* text, str. arg[0] = waits for ACK; arg[1] = TcgTextMode */
	TCG_EV_SCREEN,        /* screen drawn: arg[0] = TCG_SCREEN_*, arg[1] = duelist shown (hWhoseTurn), arg[2..] screen specific */
	TCG_EV_CARD,          /* a card is shown big (drawn/played/detail): arg[0] = deck index, arg[1] = owner, text = caption */
	TCG_EV_ANIM,          /* attack/duel animation: arg[0] = id, arg[1] = 0 attack anim / 1 duel anim, arg[2] = side, arg[3] = location */
	TCG_EV_COIN,          /* coin toss: arg[0] = HEADS/TAILS, arg[1] = coin number (1-based), arg[2] = total (0 = single), arg[3] = tossing duelist */
	TCG_EV_DAMAGE,        /* HP changed by damage: arg[0] = side, arg[1] = location, arg[2..3] = damage (lo, hi) */
	TCG_EV_SFX,           /* arg[0] = SFX_* */
	TCG_EV_MUSIC,         /* arg[0] = MUSIC_* */
	TCG_EV_WAIT,          /* the original waits arg[0] frames (pacing hint) */
	TCG_EV_TURN,          /* new turn: arg[0] = turn holder, arg[1] = wDuelTurns */
	TCG_EV_DUEL_END,      /* arg[0] = wDuelFinished, arg[1] = turn holder at the end */
	TCG_EV_THINKING,      /* AI is deciding: arg[0] = duelist */
};

enum TcgTextMode {
	TCG_TEXT_MESSAGE = 0,
	/* Complete persistent tutorial checklist, separate from acknowledged
	 * speech. str="" clears it; text=heading ID; arg[2]=number of steps. */
	TCG_TEXT_PRACTICE_CHECKLIST = 1,
	/* Dr. Mason's acknowledged tutorial explanation. Frontends use one
	 * consistent reading panel; local page turns do not add engine requests. */
	TCG_TEXT_PRACTICE_SPEECH = 2,
};

enum TcgScreen {
	TCG_SCREEN_NONE = 0,
	TCG_SCREEN_MAIN,          /* duel main scene (both active Pokemon + HUDs) of arg[1]'s perspective.
	                           * arg[2] = 1: only the HUDs were redrawn (DrawDuelHUDs: HP, energies, status) */
	TCG_SCREEN_PLAY_AREA,     /* play area list of duelist arg[1] */
	TCG_SCREEN_CARD_LIST,     /* card list (wDuelTempList) with header/info texts */
	TCG_SCREEN_LARGE_CARD,    /* large picture of a card */
	TCG_SCREEN_COIN_TOSS,
	TCG_SCREEN_ENERGY_DISCARD,/* energy discard screen for play area slot arg[2] */
	TCG_SCREEN_EMPTY,
	TCG_SCREEN_BOX_MESSAGE,   /* big box message (BOXMSG_* in arg[2]) */
	TCG_SCREEN_OTHER,
};

typedef struct TcgEvent {
	uint8_t type;            /* TcgEventType */
	uint8_t arg[4];
	uint16_t text;           /* text id (gen/text.h), 0 if none */
	const char *str;         /* formatted UTF-8 text, NULL if none; valid during the callback */
} TcgEvent;

enum TcgRequestType {
	TCG_REQ_ACK = 1,          /* wait for A/B on a text box (WaitForWideTextBoxInput). answer ignored */
	TCG_REQ_DUEL_MENU,        /* duel menu: 0 Hand, 1 Attack, 2 Check, 3 Pkmn Power, 4 Retreat, 5 Done */
	TCG_REQ_CARD_LIST,        /* DisplayCardList: pick index into items[0..num_items) or cancel.
	                           * arg[0] = 1: view only (A opens the card page, cancel leaves). The answers
	                           * never change the duel: answer locally (not over the network), and with
	                           * TCG_CANCEL when no human watches. `duelist` can be an AI side here
	                           * (the original shows the human the hand checked by the AI's Lass) */
	TCG_REQ_CARD_ITEM_MENU,   /* CardListItemSelectionMenu: 0 = Play/Select, 1 = Check, or cancel. arg[0] = card */
	TCG_REQ_PLAY_AREA,        /* DisplayPlayAreaScreen: pick a play area slot (absolute PLAY_AREA_*) of duelist `shown`, or cancel */
	TCG_REQ_MENU,             /* HandleMenuInput: pick item in [0, num_items) or cancel. menu = TcgMenu context.
	                           * TCG_MENU_PLAY_AREA: item i is play area location arg[0] + i (wExcludeArenaPokemon) */
	TCG_REQ_YES_NO,           /* 1 = yes, 0 = no. cursor = default */
	TCG_REQ_ENERGY_DISCARD,   /* HandleEnergyDiscardMenuInput: pick index into items (wDuelTempList) or cancel */
	TCG_REQ_PRIZE,            /* pick one of the remaining prize cards: index in [0, 6), cursor = default */
	TCG_REQ_COLOR,            /* pick a color (0-5 FIRE..PSYCHIC) */
	TCG_REQ_NUMBER,           /* pick a number in [0, num_items) */
	TCG_REQ_CONFIRM,          /* WaitForButtonAorB after a play area screen (0 = A continue, TCG_CANCEL = B stop).
	                           * unlike TCG_REQ_ACK this can change the duel (Devolution Spray) and must be sent
	                           * in network play. `duelist` can be an AI side: the original shows the human the
	                           * play area an AI's effect changed */
};

enum TcgMenu {
	TCG_MENU_GENERIC = 0,
	TCG_MENU_ATTACKS,         /* attack menu of the arena Pokemon: items[] = card, attack pairs */
	TCG_MENU_PLAY_AREA,       /* cursor over the shown play area */
	TCG_MENU_TWO_ITEMS,       /* TwoItemHorizontalMenu: 0 = left, 1 = right item, both named in `text` (no cancel) */
	TCG_MENU_CARD_LIST,       /* list in wDuelTempList */
	TCG_MENU_PEEK_AREA,       /* Peek: 0 = shown duelist's play area, 1 = the other one */
	TCG_MENU_PEEK_TARGET,     /* Peek: 0-5 prize (arg[0] = prize bits), 6 hand (only if arg[1]), 7 deck top */
};

typedef struct TcgRequest {
	uint8_t type;             /* TcgRequestType */
	uint8_t duelist;          /* TCG_PLAYER / TCG_OPPONENT: who answers */
	uint8_t shown;            /* duelist whose cards are shown (hWhoseTurn at request time) */
	uint8_t menu;             /* TcgMenu, for TCG_REQ_MENU */
	uint8_t num_items;
	uint8_t cursor;           /* initial/default item */
	uint16_t text;            /* prompt / header text id, 0 if none */
	uint16_t text2;           /* secondary text (card list info box), 0 if none */
	const char *str;          /* formatted prompt, may be NULL */
	const uint8_t *items;     /* deck indexes (card lists) or menu specific data, may be NULL */
	uint8_t arg[4];
} TcgRequest;

typedef struct TcgHost {
	void (*event)(void *ctx, const TcgEvent *ev);
	int (*request)(void *ctx, const TcgRequest *req);
	/* optional: called every time the original writes the duel to SRAM: sCurrentDuel
	 * each time the duel menu is printed, sBackupCurrentDuel at the start of the human's
	 * turns. data is the duel's save file, TCG_SAVE_SIZE bytes: both SRAM images
	 * (valid flag, checksum, wDuelType, then the DuelDataToSave data), sCurrentDuel
	 * first. Keep the latest one to offer "Continue Duel" (tcg_continue_duel), and
	 * delete it once the duel returns (the original clears it back in the overworld). */
	void (*save)(void *ctx, const uint8_t *data, uint32_t size);
	void *ctx;
} TcgHost;

#define TCG_SAVE_SIZE 1660        /* 2 * SAVE_DUEL_SIZE: sCurrentDuel, sBackupCurrentDuel */

/* duelist setup */
#define TCG_DECK_SIZE 60
typedef struct TcgDuelistSetup {
	uint8_t kind;             /* TCG_HUMAN or TCG_AI */
	uint8_t deck_id;          /* TCG_AI: *_DECK_ID of the AI opponent (wOpponentDeckID).
	                           * TCG_SAMS_PRACTICE_DECK_ID against a TCG_HUMAN duelist[0] is the
	                           * practice duel with Sam (the human then plays PRACTICE_PLAYER_DECK;
	                           * the original uses 2 prizes and MUSIC_DUEL_THEME_1) */
	uint8_t cards[TCG_DECK_SIZE]; /* TCG_HUMAN: the 60 card ids (0 = use the AI deck_id's list) */
	const char *name;         /* shown for TX_RAM1 (<RAMNAME>) */
} TcgDuelistSetup;

enum { TCG_HUMAN = 0, TCG_AI = 1 };

#define TCG_SAMS_PRACTICE_DECK_ID 0   /* SAMS_PRACTICE_DECK_ID */

typedef struct TcgDuelSetup {
	TcgDuelistSetup duelist[2];   /* [0] = PLAYER_TURN side, [1] = OPPONENT_TURN side */
	uint8_t prizes;               /* wNPCDuelPrizes (usually 6, 4 for some duels) */
	uint8_t rng[3];               /* wRNG1, wRNG2, wRNGCounter at the start of the duel */
	uint8_t music;                /* wDuelTheme */
	uint8_t animations_disabled;  /* wAnimationsDisabled */
	uint8_t received_legendary_cards; /* sReceivedLegendaryCards save flag: nonzero makes every AI deck
	                                   * play like a boss deck (CheckIfNotABossDeckID) */
	uint8_t skip_delay_allowed;   /* wSkipDelayAllowed (debug option sSkipDelayAllowed): the original
	                               * then cuts its draw/shuffle/prize animations and delays short while
	                               * B is held. Only the engine's copy of the flag changes; frontends
	                               * skip their own pacing (TCG_EV_WAIT, animations) while B is held */
} TcgDuelSetup;

void tcg_set_host(const TcgHost *host);

/* Runs a whole duel. Returns wDuelResult (DUEL_WIN = 0 if the PLAYER_TURN side won,
 * DUEL_LOSS = 1 otherwise; the duel engine handles draws with sudden death).
 * Every call starts from cleared engine memory, so after a host that longjmps out of
 * request() or tcg_fatal, the next tcg_run_duel starts a fresh duel. */
int tcg_run_duel(const TcgDuelSetup *setup);

/* Hash (FNV-1a) of the duel state the original saves to SRAM (SaveDuelData: both duel
 * variable blocks, decks, wDuelStates, hWhoseTurn, the RNG, the AI's duel variables).
 * Two engines fed the same setup and answers have the same hash at every request. */
uint32_t tcg_state_hash(void);

/* Continues a duel from an image given to host->save: the original's TryContinueDuel
 * ("Continue Duel" in the main menu). Decks, prizes, the duelists' types and deck ids,
 * the turn, the RNG and the music come from the save; setup (may be NULL) only gives
 * the names and settings (animations_disabled, received_legendary_cards,
 * skip_delay_allowed). Returns wDuelResult like tcg_run_duel, or TCG_CONTINUE_BROKEN
 * (after the "backup is broken" text box, like the original) if data is not a valid
 * save (size, valid flag or checksum). */
#define TCG_CONTINUE_BROKEN (-1)
int tcg_continue_duel(const TcgDuelSetup *setup, const uint8_t *data, uint32_t size);

/* What a save image holds, for a "Continue Duel" entry. Returns 0, or -1 if data is not
 * a valid save. */
typedef struct TcgSavedDuel {
	uint8_t duel_type;            /* wDuelType (the original never continues DUELTYPE_LINK saves) */
	uint8_t duelist_type[2];      /* DUELIST_TYPE_PLAYER, or DUELIST_TYPE_AI_OPP | deck id, per side */
	uint8_t opponent_deck_id;     /* wOpponentDeckID */
	uint8_t turns;                /* wDuelTurns */
	uint8_t whose_turn;           /* TCG_PLAYER / TCG_OPPONENT */
	uint8_t prizes_left[2];       /* prize cards not taken yet, per side */
} TcgSavedDuel;
int tcg_saved_duel_info(const uint8_t *data, uint32_t size, TcgSavedDuel *out);

/* Formats a text id with the current TX_RAM substitutions (wTxRam2/3, names). */
int tcg_format_text(uint16_t text_id, char *out, int out_size);
/* Plain string of a text id (no substitution), "" if invalid. */
const char *tcg_text(uint16_t text_id);
/* Optional frontend control-copy lookup. Return NULL to keep the source text.
 * The engine supplies a weak default; replacements also apply before text
 * formatting and tutorial checklist composition. */
const char *tcg_text_override(uint16_t text_id);

/* engine memory */
uint8_t *tcg_wram(void);              /* 8KB, $c000 */
uint8_t *tcg_hram(void);              /* $ff80 */
uint8_t *tcg_duelvars(uint8_t duelist); /* TCG_PLAYER / TCG_OPPONENT duel variable block (256 bytes) */
const uint8_t *tcg_card_data(uint8_t card_id); /* $41 bytes, see struct CardData */
uint8_t tcg_card_id(uint8_t duelist, uint8_t deck_index);

#endif
