/*
 * N64 specifics of save_store.h (save_store_n64.c, the cartridge's 16 Kbit EEPROM).
 *
 * save_store_write and save_store_remove only update a RAM mirror of the
 * EEPROM and return at once: an EEPROM block (8 bytes) takes ~15 ms to write,
 * so the mirror is written back by save_store_poll(), one block per call (call
 * it once per frame: the write then overlaps the next frame instead of stalling
 * the game), and only the blocks whose contents changed.
 */
#ifndef SAVE_STORE_N64_H
#define SAVE_STORE_N64_H

#include <stdint.h>

void save_store_poll(void);            /* write at most one changed block */
void save_store_flush(void);           /* write every changed block now (blocks the caller) */
int save_store_pending(void);          /* blocks still to be written */
uint32_t save_store_blocks_written(void); /* EEPROM block writes since power-on */

#endif
