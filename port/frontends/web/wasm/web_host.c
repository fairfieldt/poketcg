/*
 * WebAssembly host glue for the duel engine (tcg.h).
 *
 * The engine's host callbacks are forwarded to JavaScript imports:
 *   env.js_event(...)    a TcgEvent, flattened into scalar arguments
 *   env.js_request(...)  a TcgRequest, flattened; returns the answer
 *   env.js_fatal(msg)    tcg_fatal (the engine is built with -DTCG_NO_DEFAULT_FATAL)
 * js_event and js_request may suspend the whole wasm stack with Binaryen's
 * Asyncify (see engine.js), which is how a synchronous request() can wait for
 * a click in the browser.
 *
 * Besides the public tcg.h API, a few read-only helpers export data the UI
 * needs (deck lists, the asyncify data buffer, the setup buffer).
 */
#include <stdint.h>
#include "tcg.h"
#include "common.h"
#include "gen/decks.h"
#include "gen/text.h"

#define IMPORT(name) __attribute__((import_module("env"), import_name(name)))
#define EXPORT(name) __attribute__((export_name(name)))

IMPORT("js_event") void js_event(int type, int a0, int a1, int a2, int a3, int text, const char *str);
IMPORT("js_request") int js_request(int type, int duelist, int shown, int menu, int num_items, int cursor,
                                    int text, int text2, const char *str, const uint8_t *items,
                                    int a0, int a1, int a2, int a3);
IMPORT("js_fatal") void js_fatal(const char *msg);
IMPORT("js_save") void js_save(const uint8_t *data, uint32_t size);

void tcg_fatal(const char *msg)
{
	js_fatal(msg); /* throws in JavaScript; never returns */
	__builtin_trap();
}

static void host_event(void *ctx, const TcgEvent *ev)
{
	(void)ctx;
	js_event(ev->type, ev->arg[0], ev->arg[1], ev->arg[2], ev->arg[3], ev->text, ev->str);
}

static int host_request(void *ctx, const TcgRequest *r)
{
	(void)ctx;
	return js_request(r->type, r->duelist, r->shown, r->menu, r->num_items, r->cursor, r->text, r->text2,
	                  r->str, r->items, r->arg[0], r->arg[1], r->arg[2], r->arg[3]);
}

static void host_save(void *ctx, const uint8_t *data, uint32_t size)
{
	(void)ctx;
	js_save(data, size);
}

static const TcgHost host = { host_event, host_request, host_save, 0 };

/*
 * Duel setup buffer, filled by JavaScript before web_run() / web_continue():
 *   [0]       kind of duelist 0 (TCG_HUMAN / TCG_AI)
 *   [1]       deck_id of duelist 0
 *   [2..61]   60 card ids of duelist 0 (all 0 = deck_id's list)
 *   [62..123] the same for duelist 1
 *   [124]     prizes  [125..127] rng  [128] music  [129] animations_disabled
 *   [130]     received_legendary_cards  [131] skip_delay_allowed
 *   [160..191] name of duelist 0 (NUL terminated)  [192..223] name of duelist 1
 */
#define SETUP_DUELIST 62
#define SETUP_MISC 124
#define SETUP_NAMES 160
#define NAME_SIZE 32
static uint8_t setup_buf[256];

EXPORT("web_setup_buffer") uint8_t *web_setup_buffer(void) { return setup_buf; }

static TcgDuelSetup setup;

static void build_setup(void)
{
	for (int i = 0; i < 2; i++) {
		const uint8_t *d = &setup_buf[i * SETUP_DUELIST];
		setup.duelist[i].kind = d[0];
		setup.duelist[i].deck_id = d[1];
		for (int j = 0; j < TCG_DECK_SIZE; j++)
			setup.duelist[i].cards[j] = d[2 + j];
		char *name = (char *)&setup_buf[SETUP_NAMES + i * NAME_SIZE];
		name[NAME_SIZE - 1] = 0;
		setup.duelist[i].name = name;
	}
	setup.prizes = setup_buf[SETUP_MISC];
	setup.rng[0] = setup_buf[SETUP_MISC + 1];
	setup.rng[1] = setup_buf[SETUP_MISC + 2];
	setup.rng[2] = setup_buf[SETUP_MISC + 3];
	setup.music = setup_buf[SETUP_MISC + 4];
	setup.animations_disabled = setup_buf[SETUP_MISC + 5];
	setup.received_legendary_cards = setup_buf[SETUP_MISC + 6];
	setup.skip_delay_allowed = setup_buf[SETUP_MISC + 7];
	tcg_set_host(&host);
}

/* (with Asyncify, JavaScript calls the same entry point again to rewind a suspended duel:
 * the rewind skips the code before the suspended call) */
EXPORT("web_run") int web_run(void)
{
	build_setup();
	return tcg_run_duel(&setup);
}

/* save image for web_continue / web_saved_duel_info, filled by JavaScript */
static uint8_t save_buf[TCG_SAVE_SIZE];
EXPORT("web_save_buffer") uint8_t *web_save_buffer(void) { return save_buf; }
EXPORT("web_save_size") int web_save_size(void) { return TCG_SAVE_SIZE; }

/* tcg_continue_duel with the setup buffer's names and settings and the save buffer */
EXPORT("web_continue") int web_continue(int size)
{
	build_setup();
	return tcg_continue_duel(&setup, save_buf, (uint32_t)size);
}

/* tcg_saved_duel_info of the save buffer into out[8]: duel_type, duelist_type[2],
 * opponent_deck_id, turns, whose_turn, prizes_left[2]. returns 0 or -1 */
EXPORT("web_saved_duel_info") int web_saved_duel_info(int size, uint8_t *out)
{
	TcgSavedDuel info;
	if (tcg_saved_duel_info(save_buf, (uint32_t)size, &info))
		return -1;
	out[0] = info.duel_type;
	out[1] = info.duelist_type[0];
	out[2] = info.duelist_type[1];
	out[3] = info.opponent_deck_id;
	out[4] = info.turns;
	out[5] = info.whose_turn;
	out[6] = info.prizes_left[0];
	out[7] = info.prizes_left[1];
	return 0;
}

EXPORT("tcg_state_hash") uint32_t web_state_hash(void) { return tcg_state_hash(); }

/* Asyncify data buffer: [0] current position, [4] end, then the saved stack. */
#define ASYNCIFY_DATA_SIZE (512 * 1024)
static uint32_t asyncify_data[ASYNCIFY_DATA_SIZE / 4];
EXPORT("web_asyncify_data") uint32_t *web_asyncify_data(void) { return asyncify_data; }
EXPORT("web_asyncify_data_size") int web_asyncify_data_size(void) { return ASYNCIFY_DATA_SIZE; }

/* scratch buffer for tcg_format_text */
static char text_buf[1024];
EXPORT("web_text_buffer") char *web_text_buffer(void) { return text_buf; }
EXPORT("web_text_buffer_size") int web_text_buffer_size(void) { return sizeof(text_buf); }

/* static data */
EXPORT("web_num_cards") int web_num_cards(void) { return NUM_CARDS; }
EXPORT("web_num_texts") int web_num_texts(void) { return NUM_TEXTS; }
EXPORT("web_num_decks") int web_num_decks(void) { return NUM_VALID_DECKS; }
/* DeckPointers entry: (count, card id) pairs terminated by 0, then the 2 bytes that follow in ROM */
EXPORT("web_deck_list") const uint8_t *web_deck_list(int deck)
{
	return deck >= 0 && deck < NUM_VALID_DECKS ? gDeckLists[deck] : 0;
}
EXPORT("web_deck_label") const char *web_deck_label(int deck)
{
	return deck >= 0 && deck < NUM_VALID_DECKS ? gDeckLabels[deck] : "";
}

/* the public tcg.h API, exported under its own names */
EXPORT("tcg_format_text") int web_format_text(int id, char *out, int n) { return tcg_format_text((uint16_t)id, out, n); }
EXPORT("tcg_text") const char *web_text(int id) { return tcg_text((uint16_t)id); }
EXPORT("tcg_wram") uint8_t *web_wram(void) { return tcg_wram(); }
EXPORT("tcg_hram") uint8_t *web_hram(void) { return tcg_hram(); }
EXPORT("tcg_duelvars") uint8_t *web_duelvars(int d) { return tcg_duelvars((uint8_t)d); }
EXPORT("tcg_card_data") const uint8_t *web_card_data(int id) { return tcg_card_data((uint8_t)id); }
EXPORT("tcg_card_id") int web_card_id(int d, int i) { return tcg_card_id((uint8_t)d, (uint8_t)i); }
