#include "../../gameplay/gameplay.h"
#include "../src/gen/constants.h"
#include "overworld.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
  OwState ow;
  GpState gp;
  unsigned duels, credits, medals, texts, starters;
  bool ending_saved;
} Game;
static void text(void *ctx, OwState *s, uint16_t id, uint8_t npc) {
  (void)s;
  (void)npc;
  assert(id);
  ((Game *)ctx)->texts++;
}
static int choose(void *ctx, OwState *s, unsigned kind, uint16_t id,
                  unsigned count, unsigned initial) {
  (void)ctx;
  (void)s;
  (void)id;
  (void)count;
  (void)initial;
  return kind == OW_CHOICE_YES_NO      ? ((id == 0x580 || id == 0x58c ||
                                      id == 0x599 || id == 0x5aa || id == 0x546)
                                              ? 0
                                              : 1)
         : kind == OW_CHOICE_SAM       ? 3
         : kind == OW_CHOICE_SAM_RULES ? 7
                                       : 0;
}
static int duel(void *ctx, OwState *s, const OwDuel *d) {
  Game *g = ctx;
  assert(d->prizes >= 2 && d->prizes <= 6);
  assert(s->duel_pending);
  assert(d->deck < 55);
  g->duels++;
  return 1;
}
static bool feature(void *ctx, OwState *s, unsigned f, unsigned a) {
  Game *g = ctx;
  (void)s;
  switch (f) {
  case OW_FEATURE_PC_PACK:
    assert(a >= 1 && a <= 15);
    gp_give_mail(&g->gp, a);
    return true;
  case OW_FEATURE_CARD_RECEIVED:
    assert(a == 255 || a <= 228);
    return true;
  case OW_FEATURE_MEDAL:
    g->medals++;
    return true;
  case OW_FEATURE_MAP_PATCH:
    assert(a < 11);
    return true;
  case OW_FEATURE_CREDITS:
    g->credits++;
    return true;
  case OW_FEATURE_PAUSE:
  case OW_FEATURE_DECK_MACHINE:
    return true;
  default:
    fprintf(stderr, "Unexpected campaign feature%u arg%u\n", f, a);
    return false;
  }
}
static void gift(void *ctx, unsigned c, int delta) {
  Game *g = ctx;
  assert(c >= 1 && c <= 228);
  while (delta > 0) {
    gp_add_card(&g->gp, (uint8_t)c);
    delta--;
  }
  while (delta < 0) {
    assert(gp_remove_card(&g->gp, (uint8_t)c));
    delta++;
  }
}
static unsigned count(void *ctx, unsigned c, bool decks) {
  Game *g = ctx;
  return decks ? gp_card_count(&g->gp, (uint8_t)c)
               : gp_collection_count(&g->gp, (uint8_t)c);
}
static unsigned total(void *ctx) { return gp_total_cards(&((Game *)ctx)->gp); }
static void starter(void *ctx, unsigned choice) {
  Game *g = ctx;
  assert(gp_give_starter(&g->gp, choice));
  g->starters++;
}
static void booster(void *ctx, unsigned id) {
  uint8_t cards[10];
  assert(gp_open_booster(&((Game *)ctx)->gp, id, cards));
}
static bool save(void *ctx, const OwState *s) {
  Game *g = ctx;
  uint8_t data[2048];
  OwState copy;
  assert(ow_save_encode(s, data, sizeof(data)));
  assert(ow_save_decode(&copy, data, ow_save_size()));
  assert(copy.rng == s->rng);
  if (g->ow.map == HALL_OF_HONOR) {
    assert(copy.map == MASON_LABORATORY && copy.x == 2 && copy.y == 4 &&
           copy.direction == SOUTH);
    assert(copy.overworld_selection == OWMAP_MASON_LABORATORY);
    g->ending_saved = true;
  }
  return true;
}
static bool interact(Game *g, const OwHost *h, unsigned npc) {
  OwState *s = &g->ow;
  for (unsigned i = 0; i < s->npc_count; i++)
    if (s->npcs[i].id == npc && s->npcs[i].visible) {
      OwNpc *n = &s->npcs[i];
      s->x = n->x;
      s->y = n->y + 2;
      s->direction = NORTH;
      ow_interact(s, h);
      if (s->error[0]) {
        fprintf(stderr, "NPC%u map%u: %s\n", npc, s->map, s->error);
        return false;
      }
      return true;
    }
  return true;
}
int main(void) {
  Game g = {0};
  const uint8_t seed[] = {53, 117, 0};
  gp_init(&g.gp, seed);
  OwHost h = {.ctx = &g,
              .text = text,
              .choice = choose,
              .duel = duel,
              .feature = feature,
              .card_count = count,
              .card_change = gift,
              .total_cards = total,
              .starter = starter,
              .booster = booster,
              .save = save};
  ow_new_game(&g.ow, &h, 0x007535);
  assert(!g.ow.error[0] && g.starters == 1);
  for (unsigned pass = 0; pass < 8 && ow_event(&g.ow, EVENT_MEDAL_COUNT) < 8;
       pass++) {
    for (unsigned map = 3; map <= 27; map++) {
      ow_load_map(&g.ow, &h, map, 8, 8, NORTH);
      if (g.ow.error[0]) {
        fprintf(stderr, "Loadmap%u: %s\n", map, g.ow.error);
        return 1;
      }
      unsigned ids[OW_NPC_LIMIT], n = 0;
      for (unsigned i = 0; i < g.ow.npc_count; i++) {
        unsigned id = g.ow.npcs[i].id;
        if (id >= NPC_CHRIS && id <= NPC_KEN)
          ids[n++] = id;
        else if (id == NPC_NIKKI)
          ids[n++] = id;
      }
      for (unsigned i = 0; i < n; i++)
        if (!interact(&g, &h, ids[i]))
          return 1;
    }
    printf("Campaign pass%u medals%u duels%u\n", pass,
           ow_event(&g.ow, EVENT_MEDAL_COUNT), g.duels);
  }
  assert(ow_event(&g.ow, EVENT_MEDAL_COUNT) == 8);
  assert(g.medals == 8);
  assert(ow_enter_location(&g.ow, &h, OWMAP_POKEMON_DOME));
  g.ow.x = 22;
  g.ow.y = 2;
  g.ow.direction = NORTH;
  ow_interact(&g.ow, &h);
  assert(!g.ow.error[0] && g.ow.map == POKEMON_DOME);
  assert(ow_move(&g.ow, &h, NORTH));
  assert(ow_move(&g.ow, &h, NORTH));
  assert(ow_move(&g.ow, &h, NORTH));
  if (g.ow.error[0]) {
    fprintf(stderr, "Dome: %s\n", g.ow.error);
    return 1;
  }
  printf("Dome state%u Ronald%u door%u duels%u\n",
         ow_event(&g.ow, EVENT_POKEMON_DOME_STATE),
         ow_event(&g.ow, EVENT_RONALD_POKEMON_DOME_STATE),
         ow_event(&g.ow, EVENT_HALL_OF_HONOR_DOORS_OPEN), g.duels);
  assert(ow_event(&g.ow, EVENT_HALL_OF_HONOR_DOORS_OPEN));
  g.ow.x = 14;
  g.ow.y = 2;
  g.ow.direction = NORTH;
  assert(ow_move(&g.ow, &h, NORTH));
  assert(g.ow.map == HALL_OF_HONOR);
  g.ow.x = 10;
  g.ow.y = 12;
  g.ow.direction = NORTH;
  ow_interact(&g.ow, &h);
  if (g.ow.error[0]) {
    fprintf(stderr, "Hall: %s\n", g.ow.error);
    return 1;
  }
  assert(ow_event(&g.ow, EVENT_RECEIVED_LEGENDARY_CARDS));
  assert(g.credits == 1 && g.ending_saved);
  assert(gp_card_count(&g.gp, ZAPDOS_LV68) &&
         gp_card_count(&g.gp, MOLTRES_LV37) &&
         gp_card_count(&g.gp, ARTICUNO_LV37) &&
         gp_card_count(&g.gp, DRAGONITE_LV41));
  for (unsigned reset = 0;
       reset < 30 && ow_event(&g.ow, EVENT_CHALLENGE_CUP_3_STATE) !=
                         CHALLENGE_CUP_READY_TO_START;
       reset++) {
    g.ow.overworld_selection = 1;
    ow_continue(&g.ow, &h);
    assert(!g.ow.error[0]);
  }
  assert(ow_event(&g.ow, EVENT_CHALLENGE_CUP_3_STATE) ==
         CHALLENGE_CUP_READY_TO_START);
  ow_load_map(&g.ow, &h, CHALLENGE_HALL_ENTRANCE, 8, 14, NORTH);
  assert(!g.ow.error[0]);
  ow_load_map(&g.ow, &h, CHALLENGE_HALL, 8, 20, NORTH);
  assert(!g.ow.error[0]);
  assert(interact(&g, &h, NPC_CLERK12));
  if (g.ow.error[0]) {
    fprintf(stderr, "Challenge Cup: %s\n", g.ow.error);
    return 1;
  }
  printf("Postgame Cupstate%u round%u card%u duels%u\n",
         ow_event(&g.ow, EVENT_CHALLENGE_CUP_3_STATE),
         ow_event(&g.ow, EVENT_CHALLENGE_CUP_OPPONENT_NUMBER),
         g.ow.received_card, g.duels);
  assert(ow_event(&g.ow, EVENT_CHALLENGE_CUP_3_STATE) == CHALLENGE_CUP_WON);
  assert(gp_card_count(&g.gp, g.ow.received_card) > 0);
  printf("Campaign PASS: new game -> starter -> eight medals -> four Grand "
         "Masters + Ronald -> four legendary cards + credits; %u duels, %u "
         "cards\n",
         g.duels, gp_total_cards(&g.gp));
  return 0;
}
