#ifndef TCG_GAMEPLAY_H
#define TCG_GAMEPLAY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GP_NUM_CARDS 228
#define GP_DECK_SIZE 60
#define GP_BUILT_DECKS 4
#define GP_SAVED_DECKS 60
#define GP_AUTO_RECIPES 50
#define GP_BOOSTERS 29
#define GP_NAME_SIZE 24
#define GP_CARD_NOT_OWNED 0x80

typedef struct { uint16_t name_text; char name[GP_NAME_SIZE]; uint8_t cards[GP_DECK_SIZE]; } GpDeck;
typedef struct {
    uint8_t opponents[5], results[5], round, active, in_duel, new_record;
    uint16_t total_wins, streak, streak_backup, record;
    uint8_t record_name[16];
} GpChallenge;
typedef struct {
    /* Counts exclude cards committed to built decks. Bit7 means never owned;
       a zero count with bit7 clear remains visible in the Card Album. */
    uint8_t collection[256];
    GpDeck built[GP_BUILT_DECKS], saved[GP_SAVED_DECKS];
    uint8_t selected_deck;
    uint8_t rng[3];
    uint8_t card_pop_names[16][16], card_pop_count;
    uint8_t pc_packs[15];
    GpChallenge challenge;
} GpState;

typedef struct { uint16_t name_text; uint8_t type, rarity, set, stage; } GpCard;
typedef struct { uint8_t set, energy_mode, energy_card, chances[9], rarity[3]; } GpBooster;
typedef struct { uint16_t name_text, description_text; uint8_t cards[60]; } GpRecipe;
extern const GpCard gp_cards[GP_NUM_CARDS+1];
extern const GpBooster gp_boosters[GP_BOOSTERS];
extern const GpRecipe gp_auto_recipes[GP_AUTO_RECIPES];
extern const GpDeck gp_preset_decks[55];
extern const uint8_t gp_challenge_decks[40], gp_challenge_initial_name[16];
extern const uint8_t gp_mail_boosters[16][2];
extern const uint16_t gp_mail_text[16][2];

void gp_init(GpState *s, const uint8_t seed[3]);
uint8_t gp_random(GpState *s, uint8_t exclusive_max);
unsigned gp_collection_count(const GpState *s, uint8_t card);
unsigned gp_card_count(const GpState *s, uint8_t card);
unsigned gp_total_cards(const GpState *s);
unsigned gp_album_count(const GpState *s, unsigned *total);
bool gp_add_card(GpState *s, uint8_t card);
bool gp_remove_card(GpState *s, uint8_t card);
bool gp_give_starter(GpState *s, unsigned choice);
/* GenerateBoosterPack order is rare -> uncommon -> common, then energies. */
bool gp_open_booster(GpState *s, unsigned booster, uint8_t cards[10]);
enum GpDeckError { GP_DECK_OK=0, GP_DECK_BAD_SIZE, GP_DECK_BAD_CARD,
    GP_DECK_NO_BASIC, GP_DECK_TOO_MANY_SAME_NAME, GP_DECK_NOT_OWNED, GP_DECK_LAST };
int gp_validate_deck(const uint8_t cards[60]);
int gp_set_deck(GpState *s, unsigned slot, const GpDeck *deck);
int gp_dismantle_deck(GpState *s, unsigned slot);
int gp_save_recipe(GpState *s, unsigned slot, const GpDeck *deck);
/* Return original game's preferred set of decks to dismantle, or -1. */
int gp_find_dismantle_mask(const GpState *s, const uint8_t cards[60]);
int gp_build_auto_deck(GpState *s, unsigned recipe, unsigned slot, unsigned dismantle_mask);
int gp_build_deck(GpState *s,const GpDeck *recipe,unsigned slot,unsigned dismantle_mask);
/* Exact original Card Pop name bytes: compare/hash all16 bytes, not UTF-8.
   Peer protocol must separately reject when the other save already knows us. */
bool gp_card_pop(GpState *s, const uint8_t own_name[16], const uint8_t peer_name[16], uint8_t *card);
/* Call ONCE at player naming; persist the output, including random salt bytes.
   Up to6 original-keyboard ASCII characters; empty input selects MARK. */
bool gp_name_encode(GpState *s, const char *name, uint8_t encoded[16]);
bool gp_card_pop_pair(GpState *first,const uint8_t first_name[16],GpState *second,
                     const uint8_t second_name[16],uint8_t cards[2]);
enum GpLinkResult { GP_LINK_OK=0,GP_LINK_INVALID,GP_LINK_NOT_OWNED,GP_LINK_OCCUPIED };
/* The original receiver caps total copies at99; excess sent copies are lost.
   Both states change only after complete input validation. Persist both states
   atomically in the transport before acknowledging a successful transfer. */
int gp_transfer_cards(GpState *sender,GpState *receiver,const uint8_t *cards,
                      size_t count,unsigned *received);
/* Copy a saved recipe; sending retains it and does not transfer owned cards. */
int gp_transfer_recipe(const GpState *sender,unsigned source_slot,GpState *receiver,
                       unsigned destination_slot,bool overwrite);
bool gp_give_mail(GpState *s, unsigned mail_id);
/* Mail slot0..14: opens once, outputs10 or20 cards and returns card count. */
unsigned gp_open_mail(GpState *s, unsigned slot, uint8_t cards[20]);
void gp_challenge_begin(GpState *s);
bool gp_challenge_opponent(const GpState *s, uint8_t *deck_id, uint8_t *prizes);
/* Record one result: 0 loss/end,1 next opponent,2 five-win complete. */
unsigned gp_challenge_result(GpState *s, bool won, const uint8_t player_name[16]);
void gp_challenge_quit(GpState *s, const uint8_t player_name[16]);

/* Versioned, endian-stable save. Decode is transactional and rejects invalid
   cards/decks/counters as well as corrupt or truncated data. */
#define GP_SAVE_SIZE 6082u
size_t gp_save_encode(const GpState *s, uint8_t *out, size_t capacity);
bool gp_save_decode(GpState *s, const uint8_t *data, size_t size);

#endif
