#ifndef ADVENTURE_GAME_H
#define ADVENTURE_GAME_H
#include "overworld.h"
#include "gameplay.h"
#include <stdbool.h>
typedef struct {
 OwState ow;
 GpState cards;
 char name[16];
 uint8_t link_name[16];
 uint8_t animations_disabled,text_speed,duel_mode,skip_delay_allowed;
 uint32_t play_seconds;
} Game;
extern Game game;
extern const OwHost game_host;
extern unsigned game_profile;
extern bool game_credits_finished;
bool game_save(void);
bool game_load(void);
bool game_read_profile(unsigned profile,Game *out);
bool game_save_peer(Game *peer);
void game_store_settings(void);
void game_rng_to_cards(void);
void game_rng_to_story(void);
bool game_link(unsigned operation);
bool game_card_pop(void);
void game_credits(void);
void game_glossary(void);
void game_pc(void);
void game_print(void);
void game_tick(void);
void game_pause(void);
void game_decks(void);
void game_album(void);
void game_deck_machine(unsigned machine);
void game_name(const char *title,char *out,unsigned capacity);
void game_show_booster(unsigned booster);
void game_draw(void);
/* A fenced world frame owned by the caller until display_show(). */
struct surface_s *game_render_frame(void);
void game_story_format(unsigned text,char *out,unsigned capacity);
bool game_feature(unsigned feature,unsigned argument);
#endif
