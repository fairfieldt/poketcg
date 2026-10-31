/* Browser platform for the same native story, inventory, menus and diary sessions.
 * Only host I/O crosses into JS; source gameplay always stays in portable C. */
#include "../../../overworld/gen/data.h"
#include "../../common/save_store.h"
#include "../../n64/adventure/duel_bridge.h"
#include "../../n64/adventure/game.h"
#include "../../n64/adventure/presentation.h"
#include "adventure/mempak.h"
#include "gen/constants.h"
#include "gen/text.h"
#include "generated/world3d_maps.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IMPORT(name) __attribute__((import_module("env"), import_name(name)))
#define EXPORT(name) __attribute__((export_name(name)))
IMPORT("js_adventure") int js_adventure(int kind, int a, int b, int c, int d, int e, int f);
IMPORT("js_adv_store")
int js_adv_store(int kind, const char *name, uint8_t *data, unsigned size, const char *second,
                 uint8_t *other, unsigned other_size);
IMPORT("js_adv_clock") double js_adv_clock(void);
IMPORT("js_adv_sound") void js_adv_sound(int kind, int id);

/* JS event kinds: frame, message, menu, card, duel, report, wait, peer. */
enum { AV_FRAME = 1, AV_MESSAGE, AV_MENU, AV_CARD, AV_DUEL, AV_REPORT, AV_AUDIO_WAIT, AV_SELECT_PEER };
bool game_choose_peer(void) { return js_adventure(AV_SELECT_PEER, 0, 0, 0, 0, 0, 0) != 0; }
Game game;
bool game_credits_finished;
static Presentation presentation;
static bool continue_duel, running, duel_save_pending;
static uint8_t saved_duel[TCG_SAVE_SIZE], duel_rng[3], duel_setup_buffer[256];
static uint8_t io_buffer[16384];
static char view_json[12288], text_buffer[8192];
static double previous_time;

uint64_t get_ticks(void) { return (uint64_t)(js_adv_clock() * 1000); }
bool joypad_is_connected(int port) { return port < 2; }
void rspq_wait(void) {}
void adventure_audio_event(unsigned kind, unsigned id) {
  if (kind == 0x4d)
    js_adventure(AV_AUDIO_WAIT, 0, 0, 0, 0, 0, 0);
  else
    js_adv_sound(kind, id);
}
void adventure_audio_init(void) {}
void adventure_audio_poll(void) { game_tick(); }
void game_tick(void) {
  double now = js_adv_clock();
  if (previous_time && now > previous_time) {
    unsigned elapsed = (unsigned)((now - previous_time) / 1000);
    if (elapsed) {
      game.play_seconds += elapsed;
      if (game.play_seconds > 3599999)
        game.play_seconds = 3599999;
      previous_time += elapsed * 1000;
    }
  } else
    previous_time = now;
}
bool save_store_open(const char *where) {
  (void)where;
  return true;
}
int save_store_read(const char *name, uint8_t *out, uint32_t size) {
  return js_adv_store(0, name, out, size, NULL, NULL, 0);
}
bool save_store_write(const char *name, const uint8_t *data, uint32_t size) {
  return js_adv_store(1, name, (uint8_t *)data, size, NULL, NULL, 0) > 0;
}
bool save_store_remove(const char *name) {
  return js_adv_store(2, name, NULL, 0, NULL, NULL, 0) > 0;
}
bool adventure_save_pair(const char *a, const uint8_t *x, unsigned n, const char *b,
                         const uint8_t *y, unsigned m) {
  return js_adv_store(3, a, (uint8_t *)x, n, b, (uint8_t *)y, m) > 0;
}
int save_store_pending(void) { return 0; }
void save_store_poll(void) {}
void save_store_flush(void) {}
uint32_t save_store_blocks_written(void) { return 0; }
int validate_mempak(int port) {
  (void)port;
  return 0;
}
int get_mempak_free_space(int port) {
  (void)port;
  return 123;
}
int get_mempak_entry(int port, int index, entry_structure_t *entry) {
  (void)port;
  (void)index;
  memset(entry, 0, sizeof(*entry));
  return 0;
}
int write_mempak_entry_data(int port, entry_structure_t *entry, uint8_t *data) {
  (void)port;
  js_adventure(AV_REPORT, (int)entry->name, (int)data, entry->blocks * 256, 0, 0, 0);
  return 0;
}
void n64_message(const char *title, const char *text) {
  js_adventure(AV_MESSAGE, (int)title, (int)text, 0, 0, 0, 0);
}
int n64_menu(const char *title, const char *const *items, int count, int initial, bool cancel) {
  if (count <= 0)
    return -1;
  return js_adventure(AV_MENU, (int)title, (int)items, count, initial, cancel, 0);
}
void n64_card_detail(unsigned id) { js_adventure(AV_CARD, id, 0, 0, 0, 0, 0); }
void n64_card_name(unsigned id, char *out, int capacity) {
  const uint8_t *card = tcg_card_data(id);
  unsigned text = card[CARD_DATA_NAME] | (unsigned)card[CARD_DATA_NAME + 1] << 8;
  snprintf(out, capacity, "%s", tcg_text(text));
}
void n64_ui_text_speed(unsigned speed) { (void)speed; }
void n64_duel_rng(uint8_t out[3]) { memcpy(out, duel_rng, 3); }
int n64_duel_play(const TcgDuelSetup *setup, const uint8_t *saved) {
  memset(duel_setup_buffer, 0, sizeof(duel_setup_buffer));
  for (unsigned i = 0; i < 2; i++) {
    unsigned at = i * 62;
    duel_setup_buffer[at] = setup->duelist[i].kind;
    duel_setup_buffer[at + 1] = setup->duelist[i].deck_id;
    memcpy(duel_setup_buffer + at + 2, setup->duelist[i].cards, 60);
    snprintf((char *)duel_setup_buffer + 160 + i * 32, 32, "%s",
             setup->duelist[i].name ? setup->duelist[i].name : "");
  }
  duel_setup_buffer[124] = setup->prizes;
  memcpy(duel_setup_buffer + 125, setup->rng, 3);
  duel_setup_buffer[128] = setup->music;
  duel_setup_buffer[129] = setup->animations_disabled;
  duel_setup_buffer[130] = setup->received_legendary_cards;
  duel_setup_buffer[131] = setup->skip_delay_allowed;
  return js_adventure(AV_DUEL, (int)duel_setup_buffer, (int)saved, saved ? TCG_SAVE_SIZE : 0, 0, 0,
                      0);
}
void game_draw(void) {
  game_tick();
  presentation_sync(&presentation, &game.ow, running);
  presentation_step(&presentation, &game.ow, &w3_maps[game.ow.map]);
  js_adventure(AV_FRAME, 0, 0, 0, 0, 0, 0);
}
static void changed(void *ctx, OwState *state) {
  (void)ctx;
  unsigned flags = presentation_sync(&presentation, state, running);
  if (flags & (PRESENTATION_PLAYER | PRESENTATION_WORLD)) {
    do {
      game_draw();
    } while (presentation_moving(&presentation));
  } else
    game_draw();
}
static void wait_frames(void *ctx, unsigned source) {
  (void)ctx;
  unsigned frames = presentation_wait_frames(&presentation, source);
  for (unsigned i = 0; i < frames; i++)
    game_draw();
}
static void sound(void *ctx, unsigned kind, unsigned id) {
  (void)ctx;
  adventure_audio_event(kind, id);
}
static void error(void *ctx, const char *message) {
  (void)ctx;
  n64_message("Game stopped", message);
}
static unsigned count_cards(void *ctx, unsigned card, bool decks) {
  (void)ctx;
  return decks ? gp_card_count(&game.cards, card) : gp_collection_count(&game.cards, card);
}
static unsigned total_cards(void *ctx) {
  (void)ctx;
  return gp_total_cards(&game.cards);
}
static void change_card(void *ctx, unsigned card, int delta) {
  (void)ctx;
  while (delta > 0) {
    gp_add_card(&game.cards, card);
    delta--;
  }
  while (delta < 0) {
    gp_remove_card(&game.cards, card);
    delta++;
  }
}
static void booster(void *ctx, unsigned id) {
  (void)ctx;
  game_show_booster(id);
}
static void starter(void *ctx, unsigned id) {
  (void)ctx;
  game_rng_to_cards();
  gp_give_starter(&game.cards, id);
  game_rng_to_story();
}
static bool saved(void *ctx, const OwState *state) {
  (void)ctx;
  OwState old = game.ow;
  game.ow = *state;
  bool ok = game_save();
  game.ow = old;
  return ok;
}
static void format_story(uint16_t id, char *out, size_t cap, unsigned depth) {
  if (depth > 4) {
    *out = 0;
    return;
  }
  const unsigned char *source = (const unsigned char *)tcg_text(id);
  size_t used = 0;
  unsigned tx = 0, num = 0;
  while (*source && used + 1 < cap) {
    char replacement[1024];
    const char *insert = NULL;
    unsigned c = *source++;
    if (c == 9)
      insert = game.name;
    else if (c == 11) {
      format_story(game.ow.txram[tx++ % 2], replacement, sizeof(replacement), depth + 1);
      insert = replacement;
    } else if (c == 12) {
      snprintf(replacement, sizeof(replacement), "%u", game.ow.txram[2 + num++ % 2]);
      insert = replacement;
    }
    if (insert) {
      size_t n = strlen(insert);
      if (n > cap - used - 1)
        n = cap - used - 1;
      memcpy(out + used, insert, n);
      used += n;
    } else
      out[used++] = (char)c;
  }
  out[used] = 0;
}
static void text(void *ctx, OwState *state, uint16_t id, uint8_t npc) {
  (void)ctx;
  (void)state;
  format_story(id, text_buffer, sizeof(text_buffer), 0);
  n64_message(npc ? ow_npc_name(npc) : "", text_buffer);
}
static int choice(void *ctx, OwState *state, unsigned kind, uint16_t id, unsigned count,
                  unsigned initial) {
  (void)ctx;
  (void)state;
  (void)count;
  static const char *const
      yesno[] = {"Yes", "No"},
      *const starters[] = {"Charmander & Friends", "Squirtle & Friends", "Bulbasaur & Friends"},
      *const aaron[] = {"Lightning & Fire", "Water & Fighting", "Grass & Psychic"},
      *const sam[] = {"Normal duel", "Practice", "Rules", "Exit"},
      *const rules[] = {"Energy",         "Attacking",        "Retreating",         "Evolution",
                        "Pokemon Powers", "Ending your turn", "Winning and losing", "Exit"},
      *const gifts[] = {"Send cards", "Receive cards", "Send deck", "Receive deck", "Exit"};
  const char *const *items = yesno;
  int n = 2;
  if (kind == OW_CHOICE_STARTER) {
    items = starters;
    n = 3;
  }
  if (kind == OW_CHOICE_AARON) {
    items = aaron;
    n = 3;
  }
  if (kind == OW_CHOICE_SAM) {
    items = sam;
    n = 4;
  }
  if (kind == OW_CHOICE_SAM_RULES) {
    items = rules;
    n = 8;
  }
  if (kind == OW_CHOICE_GIFT) {
    items = gifts;
    n = 5;
  }
  format_story(id, text_buffer, sizeof(text_buffer), 0);
  int result =
      n64_menu(text_buffer, items, n, kind == OW_CHOICE_YES_NO ? (initial ? 0 : 1) : initial,
               kind != OW_CHOICE_STARTER);
  if (result < 0 && kind != OW_CHOICE_YES_NO)
    result = kind == OW_CHOICE_SAM_RULES ? 7 : kind == OW_CHOICE_GIFT ? 4 : 3;
  return kind == OW_CHOICE_YES_NO ? result == 0 : result;
}
static void finish_save(void) {
  if (duel_save_pending && game_save()) {
    duel_save_pending = false;
    save_store_remove(SAVE_STORE_DUEL);
    save_store_remove("duelowner");
  }
}
static int duel(void *ctx, OwState *state, const OwDuel *opponent) {
  (void)ctx;
  (void)state;
  TcgDuelSetup setup = {0};
  game_rng_to_cards();
  setup.prizes = opponent->prizes;
  setup.music = opponent->music;
  setup.animations_disabled = game.animations_disabled;
  setup.skip_delay_allowed = game.skip_delay_allowed;
  setup.received_legendary_cards = ow_event(&game.ow, EVENT_RECEIVED_LEGENDARY_CARDS) != 0;
  memcpy(setup.rng, game.cards.rng, 3);
  setup.duelist[0].kind = TCG_HUMAN;
  setup.duelist[0].name = game.name;
  memcpy(setup.duelist[0].cards, game.cards.built[game.cards.selected_deck].cards, 60);
  setup.duelist[1].kind = TCG_AI;
  setup.duelist[1].deck_id = opponent->deck;
  setup.duelist[1].name = ow_npc_name(opponent->npc);
  if (!continue_duel)
    save_store_remove(SAVE_STORE_DUEL);
  uint8_t owner = game_profile;
  save_store_write("duelowner", &owner, 1);
  game_save();
  int result = n64_duel_play(&setup, continue_duel ? saved_duel : NULL);
  continue_duel = false;
  n64_duel_rng(game.cards.rng);
  game_rng_to_story();
  duel_save_pending = true;
  return result == 0 ? 1 : 0;
}
static bool feature(void *ctx, OwState *state, unsigned kind, unsigned argument) {
  (void)ctx;
  (void)state;
  return game_feature(kind, argument);
}
const OwHost game_host = {NULL,        text,        choice,      duel,    feature,
                          changed,     wait_frames, sound,       saved,   error,
                          count_cards, change_card, total_cards, booster, starter};

bool game_feature(unsigned kind, unsigned argument) {
  switch (kind) {
  case OW_FEATURE_PAUSE:
    adventure_audio_event(0x4a, 0);
    adventure_audio_event(0, MUSIC_PAUSE_MENU);
    game_pause();
    adventure_audio_event(0x4b, 0);
    return true;
  case OW_FEATURE_DECK_MACHINE:
    game_deck_machine(argument);
    return true;
  case OW_FEATURE_PC:
    adventure_audio_event(0x4a, 0);
    adventure_audio_event(0, MUSIC_PC_MAIN_MENU);
    game_pc();
    adventure_audio_event(0x4b, 0);
    return true;
  case OW_FEATURE_CARD_RECEIVED:
    if (argument == 255) {
      static const unsigned legendary[] = {MOLTRES_LV37, ZAPDOS_LV68, ARTICUNO_LV37,
                                           DRAGONITE_LV41};
      for (unsigned i = 0; i < 4; i++)
        n64_card_detail(legendary[i]);
    } else
      n64_card_detail(argument ? argument : game.ow.received_card);
    return true;
  case OW_FEATURE_MEDAL: {
    static const char *const names[] = {"Grass Medal", "Science Medal",   "Fire Medal",
                                        "Water Medal", "Lightning Medal", "Psychic Medal",
                                        "Rock Medal",  "Fighting Medal"};
    n64_message("Master Medal", argument >= EVENT_BEAT_NIKKI && argument <= EVENT_BEAT_MITCH
                                    ? names[argument - EVENT_BEAT_NIKKI]
                                    : "Master Medal received.");
    return true;
  }
  case OW_FEATURE_PC_PACK:
    gp_give_mail(&game.cards, argument);
    return true;
  case OW_FEATURE_MAP_PATCH:
    game_draw();
    return true;
  case OW_FEATURE_BATTLE_CENTER:
    return game_link(4);
  case OW_FEATURE_GIFT_CENTER:
    return game_link(argument);
  case OW_FEATURE_CREDITS:
    game_credits();
    game_credits_finished = true;
    return true;
  case OW_FEATURE_CHALLENGE_MACHINE: {
    static const char *const options[] = {"Start challenge", "Record", "Exit"};
    int action = n64_menu("Challenge Machine", options, 3, 0, true);
    if (action == 1) {
      char report[128];
      snprintf(report, sizeof(report), "Consecutive wins: %u\nRecord: %u",
               game.cards.challenge.streak, game.cards.challenge.record);
      n64_message("Record", report);
      return true;
    }
    if (action != 0)
      return true;
    game_rng_to_cards();
    gp_challenge_begin(&game.cards);
    game_rng_to_story();
    for (;;) {
      uint8_t deck, prizes;
      if (!gp_challenge_opponent(&game.cards, &deck, &prizes))
        return false;
      unsigned npc = 0;
      if (deck >= LIGHTNING_AND_FIRE_DECK_ID && deck <= GRASS_AND_PSYCHIC_DECK_ID)
        npc = NPC_AARON;
      else
        for (unsigned n = 1; n < NUM_NPCS; n++)
          if (ow_npc_headers[n].deck == deck) {
            npc = n;
            break;
          }
      OwDuel next = {npc, prizes, deck, npc ? ow_npc_headers[npc].music : 0};
      game.duel_mode = 1;
      game.ow.pending_duel = next;
      game.ow.duel_pending = 1;
      game.cards.challenge.in_duel = 1;
      bool won = duel(NULL, &game.ow, &next) == 1;
      unsigned result = gp_challenge_result(&game.cards, won, game.link_name);
      game.ow.duel_pending = 0;
      game.duel_mode = 0;
      finish_save();
      if (!result || result == 2)
        return true;
      static const char *const again[] = {"Continue", "Quit"};
      if (n64_menu("Challenge Machine", again, 2, 0, false) != 0) {
        gp_challenge_quit(&game.cards, game.link_name);
        return true;
      }
    }
  }
  default:
    return false;
  }
}

EXPORT("web_adv_io") uint8_t *web_adv_io(void) { return io_buffer; }
EXPORT("web_adv_name") char *web_adv_name(void) { return game.name; }
EXPORT("web_adv_rng") void web_adv_rng(unsigned a, unsigned b, unsigned c) {
  duel_rng[0] = a;
  duel_rng[1] = b;
  duel_rng[2] = c;
}
EXPORT("web_adv_profile") int web_adv_profile(unsigned number) {
  Game other;
  return game_read_profile(number, &other);
}
EXPORT("web_adv_start") int web_adv_start(unsigned profile, unsigned fresh, unsigned seed) {
  (void)profile;
  unsigned selected = 1;
  uint8_t owner = 0;
  game_profile = selected;
  game_credits_finished = false;
  presentation_init(&presentation, NULL, true);
  previous_time = js_adv_clock();
  if (fresh) {
    char name[16];
    memcpy(name, io_buffer, 16);
    name[15] = 0;
    save_store_remove(SAVE_STORE_DUEL);
    save_store_remove("duelowner");
    memset(&game, 0, sizeof(game));
    snprintf(game.name, sizeof(game.name), "%.6s", name[0] ? name : "MARK");
    game.text_speed = 2;
    uint8_t rng[3] = {seed, seed >> 8, seed >> 16};
    gp_init(&game.cards, rng);
    gp_name_encode(&game.cards, game.name, game.link_name);
    seed = game.cards.rng[0] | (uint32_t)game.cards.rng[1] << 8 | (uint32_t)game.cards.rng[2] << 16;
    ow_new_game(&game.ow, &game_host, seed);
  } else {
    if (!game_load())
      return -1;
    continue_duel =
        save_store_read("duelowner", &owner, 1) == 1 && owner == game_profile &&
        save_store_read(SAVE_STORE_DUEL, saved_duel, sizeof(saved_duel)) == TCG_SAVE_SIZE;
    if (game.ow.duel_pending) {
      int result = duel(NULL, &game.ow, &game.ow.pending_duel);
      if (game.duel_mode == 1) {
        unsigned next = gp_challenge_result(&game.cards, result == 1, game.link_name);
        game.ow.duel_pending = 0;
        game.duel_mode = 0;
        finish_save();
        if (next == 1)
          game_feature(OW_FEATURE_CHALLENGE_MACHINE, 0);
      } else
        ow_finish_duel(&game.ow, &game_host, result);
    } else {
      continue_duel = false;
      ow_continue(&game.ow, &game_host);
    }
  }
  game_save();
  finish_save();
  return 0;
}
EXPORT("web_adv_action") int web_adv_action(unsigned action, unsigned arg) {
  unsigned rendered_before = presentation.render_frames;
  running = action < 4 && arg != 0;
  if (action < 4) {
    if (!game.ow.map)
      ow_world_select(&game.ow, action);
    else
      ow_move(&game.ow, &game_host, action);
  } else if (action == 4) {
    if (!game.ow.map) {
      presentation_world_travel(&presentation, game.ow.overworld_selection);
      while (presentation_moving(&presentation))
        game_draw();
      ow_enter_location(&game.ow, &game_host, game.ow.overworld_selection);
    } else
      ow_interact(&game.ow, &game_host);
  } else if (action == 5)
    game_pause();
  else if (action == 6)
    game_save();
  else if (action == 7) {
    adventure_audio_event(0x4a, 0);
    adventure_audio_event(0, MUSIC_CARD_POP);
    game_card_pop();
    adventure_audio_event(0x4b, 0);
  } else if (action == 8)
    game_feature(OW_FEATURE_PC, 0);
  else if (action == 9)
    game_decks();
  else if (action == 10)
    game_album();
  else if (action == 11)
    game_link(arg);
  else if (action == 12) {
    /* Presentation-only stop: release gait/lean after a held movement chain. */
  }
  if (!game.ow.duel_pending)
    finish_save();
  /* changed() already renders each movement frame through the tile boundary.
     An unconditional tail draw adds an idle beat before the next held step. */
  if (action >= 4 || presentation.render_frames == rendered_before)
    game_draw();
  return game_credits_finished ? 1 : 0;
}
static void append(char **p, size_t *left, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int n = vsnprintf(*p, *left, format, args);
  va_end(args);
  if (n < 0)
    return;
  size_t used = (size_t)n < *left ? (size_t)n : *left - 1;
  *p += used;
  *left -= used;
}
static void pose(char **p, size_t *left, const PresentationPose *v) {
  append(p, left,
         "{\"x\":%.4f,\"z\":%.4f,\"heading\":%.4f,\"gait\":%.4f,\"bob\":%.4f,\"lean\":%.4f}", v->x,
         v->z, v->heading, v->gait, v->bob, v->lean);
}
EXPORT("web_adv_view") const char *web_adv_view(void) {
  presentation_sync(&presentation, &game.ow, running);
  char *p = view_json;
  size_t left = sizeof(view_json);
  append(&p, &left,
         "{\"map\":%u,\"x\":%u,\"y\":%u,\"direction\":%u,\"overworldSelection\":%u,\"activeNpc\":%"
         "u,\"totalCards\":%u,\"selectedDeck\":%u,\"textSpeed\":%u,\"playSeconds\":%u,"
         "\"creditsFinished\":%s,\"mapPatch\":[",
         game.ow.map, game.ow.x, game.ow.y, game.ow.direction, game.ow.overworld_selection,
         game.ow.active_npc, gp_total_cards(&game.cards), game.cards.selected_deck, game.text_speed,
         (unsigned)game.play_seconds, game_credits_finished ? "true" : "false");
  for (unsigned i = 0; i < 11; i++)
    append(&p, &left, "%s%u", i ? "," : "", game.ow.map_patch[i]);
  append(&p, &left, "],\"npcs\":[");
  for (unsigned i = 0; i < game.ow.npc_count; i++) {
    OwNpc *n = &game.ow.npcs[i];
    append(&p, &left,
           "%s{\"id\":%u,\"x\":%u,\"y\":%u,\"direction\":%u,\"visible\":%u,\"sprite\":%u,"
           "\"animation\":%u}",
           i ? "," : "", n->id, n->x, n->y, n->direction, n->visible, n->sprite,
           game.ow.npc_animation[i]);
  }
  append(&p, &left, "],\"presentation\":{\"player\":");
  pose(&p, &left, presentation_player(&presentation));
  append(&p, &left, ",\"world\":");
  pose(&p, &left, presentation_world(&presentation));
  append(&p, &left, ",\"cursor\":");
  pose(&p, &left, presentation_world_cursor(&presentation));
  append(&p, &left, ",\"npcs\":[");
  for (unsigned i = 0; i < game.ow.npc_count; i++) {
    if (i)
      append(&p, &left, ",");
    const PresentationPose *v = presentation_npc(&presentation, game.ow.npcs[i].id);
    append(&p, &left, "{\"id\":%u,\"pose\":", game.ow.npcs[i].id);
    PresentationPose empty = {0};
    pose(&p, &left, v ? v : &empty);
    append(&p, &left, "}");
  }
  const PresentationCamera *cam = presentation_camera(&presentation);
  append(&p, &left,
         "],\"camera\":{\"eye\":[%.4f,%.4f,%.4f],\"target\":[%.4f,%.4f,%.4f],\"fov\":%.4f}}}",
         cam->eye[0], cam->eye[1], cam->eye[2], cam->target[0], cam->target[1], cam->target[2],
         cam->fov);
  return view_json;
}

EXPORT("web_adv_event") unsigned web_adv_event(unsigned event) { return ow_event(&game.ow, event); }
EXPORT("web_adv_load_only") int web_adv_load_only(unsigned profile) {
  game_profile = profile == 2 ? 2 : 1;
  return game_load() ? 0 : -1;
}

EXPORT("web_adv_tick") void web_adv_tick(void) { game_tick(); }
