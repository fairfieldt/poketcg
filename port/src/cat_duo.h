/* Port-only encounter IDs. Keep original ROM tables and save layouts intact. */
#ifndef TCG_CAT_DUO_H
#define TCG_CAT_DUO_H
#include <stdint.h>
#define CAT_DUO_DECK_ID 120
#define CAT_TUXEDO_NPC 240
#define CAT_TABBY_NPC 241
#define CAT_TUXEDO_MODEL 200
#define CAT_TABBY_MODEL 201
#define CAT_DUO_SCALE 0.5f
#define CAT_DUO_NAME "Pepper & Poppy"
#define CAT_TUXEDO_NAME "Poppy"
#define CAT_TABBY_NAME "Pepper"
#define CAT_DUO_CHALLENGE_TEXT 0xff00
#define CAT_DUO_WIN_TEXT 0xff01
#define CAT_DUO_LOSS_TEXT 0xff02
#define CAT_DUO_DRAW_TEXT 0xff03
#define CAT_DUO_REWARD_TEXT 0xff04
/* Original event masks use bytes 0..28 and temporary byte 63. */
#define CAT_DUO_REWARD_EVENT_BYTE 62
#define CAT_DUO_REWARD_EVENT_MASK 1
static inline int cat_duo_npc(unsigned id) {
    return id == CAT_TUXEDO_NPC || id == CAT_TABBY_NPC;
}
static inline const char *cat_duo_text(unsigned id) {
    switch (id) {
    case CAT_DUO_CHALLENGE_TEXT: return "Duel both of us? Six prizes.";
    case CAT_DUO_WIN_TEXT: return "You beat us! Let's play again sometime.";
    case CAT_DUO_LOSS_TEXT: return "We make a good team. Want a rematch?";
    case CAT_DUO_REWARD_TEXT: return "Take this Flying Pikachu!";
    case CAT_DUO_DRAW_TEXT: return "A draw! Let's try again sometime.";
    default: return 0;
    }
}
void cat_duo_deck(uint8_t cards[60]);
#endif
