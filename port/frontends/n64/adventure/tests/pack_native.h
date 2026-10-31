#ifndef ADVENTURE_PACK_NATIVE_H
#define ADVENTURE_PACK_NATIVE_H
#include <libdragon.h>
#include "../pack_flow.h"
/* Diagnostic24 observes production phases; input still passes through the
   real VI edge capture. Linker wrappers provide the virtual controller. */
void adventure_pack_native_begin(unsigned booster,const PackFlow *flow);
void adventure_pack_native_frame(const PackFlow *flow,const surface_t *display);
void adventure_pack_native_end(const PackFlow *flow);
void adventure_pack_native_regression(void);
#endif
