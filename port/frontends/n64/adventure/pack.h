#ifndef ADVENTURE_PACK_H
#define ADVENTURE_PACK_H
#include <stdint.h>
/* Presentation only: the caller has generated and granted these exact ten
   cards. Their source order and the caller's game/save/RNG state are retained. */
void adventure_pack_open(unsigned booster,const uint8_t cards[10],const char *recipient);
/* Static native fixtures are available in diagnostic builds only. */
struct surface_s *adventure_pack_preview(unsigned booster,const uint8_t cards[10],
 const char *recipient,unsigned phase,unsigned index);
#endif
