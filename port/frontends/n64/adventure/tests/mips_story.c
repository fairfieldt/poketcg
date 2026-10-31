/* Diagnostic-ROM entry point: production Ow/Gp functions, scripted duel
 * outcomes, real inventory, no UI, and no SRAM writes. The main campaign is
 * included directly so the host and N64 execute the same fixture. Matrix and
 * navigation checks below use that fixture's callbacks and real GpState too.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef N64
#include <libdragon.h>
#else
#define debugf printf
#endif
#include "../../../../gameplay/gameplay.h"
#include "../../../../overworld/gen/data.h"
#include "../../../../overworld/overworld.h"
#include "../../../../src/gen/constants.h"

#define main adventure_campaign_fixture
#include "../../../../overworld/tests/campaign.c"
#undef main

static int matrix_outcome;
static int matrix_duel(void *ctx, OwState *s, const OwDuel *d) {
  Game *g = ctx;
  assert(s->duel_pending && d->prizes >= 2 && d->prizes <= 6);
  g->duels++;
  return matrix_outcome;
}

static bool matrix_feature(void *ctx, OwState *s, unsigned id,
                           unsigned argument) {
  /* Matrix fixtures exercise VM control flow at these explicit frontend
   * boundaries. Peer transports/screens are covered by frontend tests. */
  if (id == OW_FEATURE_BATTLE_CENTER || id == OW_FEATURE_GIFT_CENTER ||
      id == OW_FEATURE_PC || id == OW_FEATURE_CHALLENGE_MACHINE)
    return true;
  return feature(ctx, s, id, argument);
}

static OwHost diagnostic_host(Game *g) {
  OwHost h = {.ctx = g,
              .text = text,
              .choice = choose,
              .duel = matrix_duel,
              .feature = matrix_feature,
              .card_count = count,
              .card_change = gift,
              .total_cards = total,
              .starter = starter,
              .booster = booster,
              .save = save};
  return h;
}

static bool matrix_fixture(void) {
  Game g = {0};
  const uint8_t seed[] = {0x57, 0x31, 0};
  gp_init(&g.gp, seed);
  for (unsigned card = 1; card <= 228; card++)
    for (unsigned copies = 0; copies < 4; copies++)
      gp_add_card(&g.gp, (uint8_t)card);
  OwHost h = diagnostic_host(&g);
  unsigned encounters = 0;
  for (unsigned stage = 0; stage < 3; stage++)
    for (unsigned map = 1; map < 34; map++) {
      OwState base = {
          .version = OW_SAVE_VERSION, .rng = 0x3157, .overworld_selection = 1};
      ow_set_event(&base, EVENT_MASON_LAB_STATE,
                   MASON_LAB_RECEIVED_STARTER_DECK);
      if (stage >= 1) {
        for (unsigned e = EVENT_BEAT_NIKKI; e <= EVENT_BEAT_MITCH; e++)
          ow_set_event(&base, e, 1);
        ow_set_event(&base, EVENT_MEDAL_COUNT, 8);
        ow_set_event(&base, EVENT_BEAT_JENNIFER, 1);
        ow_set_event(&base, EVENT_BEAT_NICHOLAS, 1);
        ow_set_event(&base, EVENT_BEAT_BRANDON, 1);
        ow_set_event(&base, EVENT_PUPIL_MICHAEL_STATE, PUPIL_DEFEATED);
        ow_set_event(&base, EVENT_PUPIL_CHRIS_STATE, PUPIL_DEFEATED);
        ow_set_event(&base, EVENT_PUPIL_JESSICA_STATE, PUPIL_DEFEATED);
        ow_set_event(&base, EVENT_NIKKI_STATE, NIKKI_IN_GRASS_CLUB);
      }
      if (stage == 2)
        ow_set_event(&base, EVENT_RECEIVED_LEGENDARY_CARDS, 1);
      /* Matrix scans include decorative/link clerks. Their platform feature
       * boundaries are tested separately by the linked gameplay/profile
       * tests; a matrix should stop after reaching that boundary. */
      ow_load_map(&base, &h, map, 8, 8, NORTH);
      if (base.error[0]) {
        debugf("MIPS MATRIX load stage%u map%u: %s\n", stage, map, base.error);
        return false;
      }
      for (unsigned i = 0; i < base.npc_count; i++)
        for (unsigned outcome = 0; outcome < 2; outcome++) {
          g.ow = base;
          matrix_outcome = (int)outcome;
          unsigned id = g.ow.npcs[i].id;
          ow_run(&g.ow, &h, ow_npc_headers[id].script, id);
          ow_resume(&g.ow, &h);
          encounters++;
          if (g.ow.error[0]) {
            debugf("MIPS MATRIX NPC stage%u map%u id%u result%u: %s\n", stage,
                   map, id, outcome, g.ow.error);
            return false;
          }
        }
    }
  debugf("MIPS NPC MATRIX PASS: %u original encounters, win/loss branches\n",
         encounters);
  return encounters == 714;
}

static bool navigation_fixture(void) {
  Game g = {0};
  const uint8_t seed[] = {1, 2, 3};
  gp_init(&g.gp, seed);
  OwHost h = diagnostic_host(&g);
  OwState *s = &g.ow;
  *s = (OwState){.version = OW_SAVE_VERSION,
                 .rng = 123,
                 .map = 0,
                 .overworld_selection = OWMAP_MASON_LABORATORY};
  ow_set_event(s, EVENT_MASON_LAB_STATE, MASON_LAB_RECEIVED_STARTER_DECK);
  assert(ow_world_select(s, NORTH) &&
         s->overworld_selection == OWMAP_LIGHTNING_CLUB);
  assert(ow_world_select(s, NORTH) &&
         s->overworld_selection == OWMAP_ROCK_CLUB);
  assert(ow_world_select(s, NORTH) &&
         s->overworld_selection == OWMAP_ISHIHARAS_HOUSE);
  assert(!ow_event(s, EVENT_ISHIHARAS_HOUSE_MENTIONED));
  assert(ow_enter_location(s, &h, OWMAP_ISHIHARAS_HOUSE));
  assert(s->map == ISHIHARAS_HOUSE && !s->error[0]);
  for (unsigned from = 1; from <= 12; from++)
    for (unsigned to = 1; to <= 12; to++) {
      uint8_t xy[64][2];
      unsigned n = ow_world_path(from, to, xy, 64);
      assert(n <= 64);
      if (from == to)
        assert(!n);
      else
        assert(n && xy[n - 1][0] == ow_location_positions[to][0] &&
               xy[n - 1][1] == ow_location_positions[to][1]);
    }
  assert(ow_enter_location(s, &h, OWMAP_POKEMON_DOME));
  s->x = 22;
  s->y = 2;
  s->direction = NORTH;
  ow_interact(s, &h);
  assert(s->map == POKEMON_DOME_ENTRANCE && !s->error[0]);
  assert(!ow_move(s, &h, NORTH));
  for (unsigned e = EVENT_BEAT_NIKKI; e <= EVENT_BEAT_MITCH; e++)
    ow_set_event(s, e, 1);
  ow_interact(s, &h);
  assert(s->map == POKEMON_DOME && !s->error[0]);
  uint8_t encoded[2048];
  OwState copy;
  s->rng = 0x00123456;
  assert(ow_save_encode(s, encoded, sizeof(encoded)));
  assert(encoded[4] == 1 && encoded[5] == 0 && encoded[6] == 0 &&
         encoded[7] == 0);
  assert(encoded[8] == 0x56 && encoded[9] == 0x34 && encoded[10] == 0x12 &&
         encoded[11] == 0);
  assert(ow_save_decode(&copy, encoded, ow_save_size()) &&
         copy.rng == 0x00123456);
  debugf("MIPS NAVIGATION PASS: 144 source paths, cursor graph, real door "
         "patches/warps, canonical LE save\n");
  return true;
}

bool adventure_story_diagnostic(void) {
  debugf("MIPS STORY BEGIN: production Ow/Gp engine, scripted duel outcomes\n");
  if (adventure_campaign_fixture() != 0)
    return false;
  debugf("MIPS CAMPAIGN PASS: new game, eight medals, Grand Masters, Ronald, "
         "four legendary cards, credits, postgame Cup\n");
  if (!matrix_fixture() || !navigation_fixture())
    return false;
  debugf("ADVENTURE MIPS STORY PASS\n");
  return true;
}
