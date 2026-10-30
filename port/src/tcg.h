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
	TCG_EV_TEXT = 1,      /* text box printed: text, str. arg[0] = 1 if the engine waits for an ACK next */
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

enum TcgScreen {
	TCG_SCREEN_NONE = 0,
	TCG_SCREEN_MAIN,          /* duel main scene (both active Pokemon + HUDs) of arg[1]'s perspective */
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
	TCG_REQ_MENU,             /* HandleMenuInput: pick item in [0, num_items) or cancel. menu = TcgMenu context */
	TCG_REQ_YES_NO,           /* 1 = yes, 0 = no. cursor = default */
	TCG_REQ_ENERGY_DISCARD,   /* HandleEnergyDiscardMenuInput: pick index into items (wDuelTempList) or cancel */
	TCG_REQ_PRIZE,            /* pick one of the remaining prize cards: index in [0, 6), cursor = default */
	TCG_REQ_COLOR,            /* pick a color (0-5 FIRE..PSYCHIC) */
	TCG_REQ_NUMBER,           /* pick a number in [0, num_items) */
	TCG_REQ_CONFIRM,          /* WaitForButtonAorB where A/B matters (0 = A continue, TCG_CANCEL = B stop).
	                           * unlike TCG_REQ_ACK this changes the duel and must be sent in network play */
};

enum TcgMenu {
	TCG_MENU_GENERIC = 0,
	TCG_MENU_ATTACKS,         /* attack menu of the arena Pokemon: items[] = card, attack pairs */
	TCG_MENU_PLAY_AREA,       /* cursor over the shown play area */
	TCG_MENU_TWO_ITEMS,       /* two horizontal items (text) */
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
	/* optional: called when the original would save the duel to SRAM */
	void (*save)(void *ctx);
	void *ctx;
} TcgHost;

/* duelist setup */
#define TCG_DECK_SIZE 60
typedef struct TcgDuelistSetup {
	uint8_t kind;             /* TCG_HUMAN or TCG_AI */
	uint8_t deck_id;          /* TCG_AI: *_DECK_ID of the AI opponent (wOpponentDeckID) */
	uint8_t cards[TCG_DECK_SIZE]; /* TCG_HUMAN: the 60 card ids (0 = use the AI deck_id's list) */
	const char *name;         /* shown for TX_RAM1 (<RAMNAME>) */
} TcgDuelistSetup;

enum { TCG_HUMAN = 0, TCG_AI = 1 };

typedef struct TcgDuelSetup {
	TcgDuelistSetup duelist[2];   /* [0] = PLAYER_TURN side, [1] = OPPONENT_TURN side */
	uint8_t prizes;               /* wNPCDuelPrizes (usually 6, 4 for some duels) */
	uint8_t rng[3];               /* wRNG1, wRNG2, wRNGCounter at the start of the duel */
	uint8_t music;                /* wDuelTheme */
	uint8_t animations_disabled;  /* wAnimationsDisabled */
	uint8_t received_legendary_cards; /* sReceivedLegendaryCards save flag: nonzero makes every AI deck
	                                   * play like a boss deck (CheckIfNotABossDeckID) */
} TcgDuelSetup;

void tcg_set_host(const TcgHost *host);

/* Runs a whole duel. Returns wDuelResult (DUEL_WIN = 0 if the PLAYER_TURN side won,
 * DUEL_LOSS = 1 otherwise; the duel engine handles draws with sudden death). */
int tcg_run_duel(const TcgDuelSetup *setup);

/* Formats a text id with the current TX_RAM substitutions (wTxRam2/3, names). */
int tcg_format_text(uint16_t text_id, char *out, int out_size);
/* Plain string of a text id (no substitution), "" if invalid. */
const char *tcg_text(uint16_t text_id);

/* engine memory */
uint8_t *tcg_wram(void);              /* 8KB, $c000 */
uint8_t *tcg_hram(void);              /* $ff80 */
uint8_t *tcg_duelvars(uint8_t duelist); /* TCG_PLAYER / TCG_OPPONENT duel variable block (256 bytes) */
const uint8_t *tcg_card_data(uint8_t card_id); /* $41 bytes, see struct CardData */
uint8_t tcg_card_id(uint8_t duelist, uint8_t deck_index);

#endif
