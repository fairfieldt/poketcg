#ifndef TCG_OVERWORLD_H
#define TCG_OVERWORLD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OW_MAP_COUNT 34
#define OW_NPC_LIMIT 24
#define OW_EVENT_BYTES 64
#define OW_SAVE_VERSION 1

typedef struct OwState OwState;
typedef struct {
  uint8_t id, x, y, direction, visible, sprite;
} OwNpc;
typedef struct {
  uint8_t npc, prizes, deck, music;
} OwDuel;
/* Called synchronously. A feature must return false if it cannot be provided;
   execution then stops with a concrete error instead of skipping that feature.
 */
typedef struct {
  void *ctx;
  void (*text)(void *, OwState *, uint16_t text, uint8_t npc);
  int (*choice)(void *, OwState *, unsigned kind, uint16_t text, unsigned count,
                unsigned initial);
  int (*duel)(void *, OwState *, const OwDuel *); /* 1 win, 0 loss, 2 draw */
  bool (*feature)(void *, OwState *, unsigned feature, unsigned argument);
  void (*changed)(void *, OwState *); /* render map/actor change */
  void (*wait)(void *, unsigned frames); /* original 60 Hz frames */
  void (*sound)(void *, unsigned kind, unsigned id);
  bool (*save)(void *, const OwState *);
  void (*error)(void *, const char *);
  unsigned (*card_count)(void *, unsigned card, bool include_decks);
  void (*card_change)(void *, unsigned card, int delta);
  unsigned (*total_cards)(void *);
  void (*booster)(void *, unsigned id);
  void (*starter)(void *, unsigned choice);
} OwHost;

enum OwChoice {
  OW_CHOICE_YES_NO,
  OW_CHOICE_STARTER,
  OW_CHOICE_AARON,
  OW_CHOICE_SAM,
  OW_CHOICE_SAM_RULES,
  OW_CHOICE_GIFT
};
enum OwFeature {
  OW_FEATURE_PAUSE = 1,
  OW_FEATURE_DECK_MACHINE,
  OW_FEATURE_PC,
  OW_FEATURE_CARD_RECEIVED,
  OW_FEATURE_MEDAL,
  OW_FEATURE_PC_PACK,
  OW_FEATURE_BATTLE_CENTER,
  OW_FEATURE_GIFT_CENTER,
  OW_FEATURE_CREDITS,
  OW_FEATURE_CHALLENGE_MACHINE,
  OW_FEATURE_MAP_PATCH,
  OW_FEATURE_CHALLENGE_PRIZE
};

struct OwState {
  uint32_t version, rng;
  uint8_t events[OW_EVENT_BYTES];
  uint8_t map, x, y, direction, overworld_selection;
  uint8_t npc_count, active_npc, dialog_npc, script_control, starter,
      received_card;
  uint8_t challenge_npc, challenge_round, challenge_history[4], masters[10];
  uint8_t requested_card, default_song, song_override, map_patch[11],
      pc_packs[16];
  /* txram: text slots wTxRam2/wTxRam2_b, then number slots wTxRam3/_b. */
  uint16_t next_script, script_pc, txram[4];
  uint8_t next_npc, duel_npc, last_duel_result;
  OwDuel pending_duel;
  uint8_t duel_pending;
  OwNpc npcs[OW_NPC_LIMIT];
  uint8_t npc_flags[OW_NPC_LIMIT], npc_animation[OW_NPC_LIMIT];
  uint8_t native_vars[64];
  uint16_t native_words[16];
  char error[160];
};

typedef struct {
  const char *name;
  uint8_t width, height;
  const uint8_t *permissions;
  uint8_t song;
} OwMap;
extern const OwMap ow_maps[OW_MAP_COUNT];
void ow_new_game(OwState *, const OwHost *, uint32_t seed);
void ow_load_map(OwState *, const OwHost *, unsigned map, unsigned x,
                 unsigned y, unsigned direction);
void ow_resume(OwState *, const OwHost *);
void ow_continue(OwState *, const OwHost *);
void ow_finish_duel(OwState *, const OwHost *, int result);
bool ow_move(OwState *, const OwHost *, unsigned direction);
bool ow_interact(OwState *, const OwHost *);
bool ow_world_select(OwState *, unsigned direction);
unsigned ow_world_path(unsigned start, unsigned target, uint8_t (*points)[2],
                       unsigned capacity);
bool ow_enter_location(OwState *, const OwHost *, unsigned selection);
void ow_run(OwState *, const OwHost *, uint16_t script, unsigned npc);
unsigned ow_event(const OwState *, unsigned event);
void ow_set_event(OwState *, unsigned event, unsigned value);
const char *ow_npc_name(unsigned npc);
uint16_t ow_script_address(const char *name);
bool ow_save_encode(const OwState *, uint8_t *, size_t);
bool ow_save_decode(OwState *, const uint8_t *, size_t);
size_t ow_save_size(void);
#endif
