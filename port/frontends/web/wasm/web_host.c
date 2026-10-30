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

/*
 * Duel setup buffer, filled by JavaScript before web_run():
 *   [0]       kind of duelist 0 (TCG_HUMAN / TCG_AI)
 *   [1]       deck_id of duelist 0
 *   [2..61]   60 card ids of duelist 0 (all 0 = deck_id's list)
 *   [62..123] the same for duelist 1
 *   [124]     prizes  [125..127] rng  [128] music  [129] animations_disabled
 *   [130]     received_legendary_cards
 *   [160..191] name of duelist 0 (NUL terminated)  [192..223] name of duelist 1
 */
#define SETUP_DUELIST 62
#define SETUP_MISC 124
#define SETUP_NAMES 160
#define NAME_SIZE 32
static uint8_t setup_buf[256];

EXPORT("web_setup_buffer") uint8_t *web_setup_buffer(void) { return setup_buf; }

EXPORT("web_run") int web_run(void)
{
	static TcgDuelSetup setup;
	static const TcgHost host = { host_event, host_request, 0, 0 };
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
	tcg_set_host(&host);
	return tcg_run_duel(&setup);
}

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
