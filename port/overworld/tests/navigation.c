#include "../../src/gen/constants.h"
#include "gen/data.h"
#include "overworld.h"
#include <assert.h>
#include <stdio.h>
static unsigned song;
static void sound(void *ctx, unsigned kind, unsigned id) {
  (void)ctx;
  if (!kind)
    song = id;
}
static bool feature(void *ctx, OwState *s, unsigned f, unsigned a) {
  (void)ctx;
  (void)s;
  (void)a;
  return f == OW_FEATURE_PC_PACK || f == OW_FEATURE_MAP_PATCH;
}
int main(void) {
  OwState s = {.version = OW_SAVE_VERSION,
               .rng = 123,
               .map = 0,
               .overworld_selection = OWMAP_MASON_LABORATORY};
  OwHost h = {.sound = sound, .feature = feature};
  ow_set_event(&s, EVENT_MASON_LAB_STATE, 3);
  assert(ow_world_select(&s, NORTH) &&
         s.overworld_selection == OWMAP_LIGHTNING_CLUB);
  assert(ow_world_select(&s, NORTH) &&
         s.overworld_selection == OWMAP_ROCK_CLUB);
  assert(ow_world_select(&s, NORTH) &&
         s.overworld_selection == OWMAP_ISHIHARAS_HOUSE);
  assert(!ow_event(&s, EVENT_ISHIHARAS_HOUSE_MENTIONED));
  assert(ow_enter_location(&s, &h, OWMAP_ISHIHARAS_HOUSE));
  assert(s.map == ISHIHARAS_HOUSE && !s.error[0]);
  for (unsigned from = 1; from <= 12; from++)
    for (unsigned to = 1; to <= 12; to++) {
      uint8_t xy[64][2];
      unsigned count = ow_world_path(from, to, xy, 64);
      assert(count <= 64);
      if (from == to)
        assert(!count);
      else {
        assert(count);
        assert(xy[count - 1][0] == ow_location_positions[to][0] &&
               xy[count - 1][1] == ow_location_positions[to][1]);
      }
    }
  assert(ow_enter_location(&s, &h, OWMAP_POKEMON_DOME));
  assert(song == ow_maps[POKEMON_DOME_ENTRANCE].song);
  s.x = 22;
  s.y = 2;
  s.direction = NORTH;
  ow_interact(&s, &h);
  assert(s.map == POKEMON_DOME_ENTRANCE && !s.error[0]);
  assert(!ow_move(&s, &h, NORTH));
  for (unsigned e = EVENT_BEAT_NIKKI; e <= EVENT_BEAT_MITCH; e++)
    ow_set_event(&s, e, 1);
  ow_interact(&s, &h);
  assert(s.map == POKEMON_DOME && !s.error[0]);
  assert(song == ow_maps[POKEMON_DOME].song);
  puts("Navigation OK: original cursor graph, all144 waypoint routes, unnamed "
       "Ishihara access, locked/open Dome portal, source map music");
  return 0;
}
