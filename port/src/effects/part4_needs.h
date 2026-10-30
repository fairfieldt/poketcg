/*
 * Routines (and constants) that effects/part4.c needs but that are not
 * declared in any shared header yet. To be provided by the lead.
 */
#ifndef TCG_EFFECTS_PART4_NEEDS_H
#define TCG_EFFECTS_PART4_NEEDS_H

#include "../common.h"

/* constants missing from gen/constants.h */
#ifndef SYM_SPACE
#define SYM_SPACE 0x00     /* src/constants/charmaps.asm, TX_SYMBOL table */
#endif
#ifndef SYM_CURSOR_R
#define SYM_CURSOR_R 0x0f
#endif
#ifndef PAD_A
#define PAD_A (1 << 0)     /* src/constants/hardware.inc */
#endif
#ifndef PAD_START
#define PAD_START (1 << 3)
#endif

#endif
