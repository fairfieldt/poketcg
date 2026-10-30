/*
 * Routines used by effects/part1.c that the port does not declare yet.
 * Not implemented here: see the comments for what the original does.
 */
#ifndef TCG_EFFECTS_PART1_NEEDS_H
#define TCG_EFFECTS_PART1_NEEDS_H

#include "../engine.h"

/*
 * SerialRecvByte (home/serial.asm)
 * output: carry (true) if no byte is available; *a = register a
 * effects: if [wSerialRecvCounter] != 0: decrements wSerialRecvCounter,
 *   *a = wSerialRecvBuf[wcba3], wcba3 = (wcba3 + 1) & $1f, returns nc.
 *   otherwise: if [wSerialFlags] != 0 returns nc with *a = [wSerialFlags],
 *   else returns carry with *a = 0.
 * The original caller (DuelistSelectForcedSwitch, link opponent) loops
 * `halt` until it returns nc, so the port's stub must eventually deliver.
 */
bool SerialRecvByte(u8 *a);

#endif
