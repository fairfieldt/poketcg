#ifndef TRANSFER_IMPORT_H
#define TRANSFER_IMPORT_H
#include "gameplay.h"
#include <stdint.h>
#include <stddef.h>
typedef enum { TI_OK, TI_NO_CONTROLLER, TI_NO_PAK, TI_NO_CART, TI_IO, TI_TIMEOUT,
 TI_BAD_HEADER, TI_WRONG_GAME, TI_UNSUPPORTED, TI_BAD_SAVE, TI_NO_DECKS, TI_CONTROLLER_PAK } TiResult;
typedef struct {
 void *ctx;
 int (*connected)(void *);
 int (*accessory)(void *);
 int (*read)(void *, uint16_t, uint8_t *);
 int (*write)(void *, uint16_t, const uint8_t *);
 uint64_t (*millis)(void *);
 void (*progress)(void *, unsigned);
} TiBus;
TiResult ti_import_read(const TiBus *, GpDeck decks[4], uint8_t valid[4]);
TiResult ti_parse(const uint8_t *, size_t, const uint8_t *, size_t,
                  const uint8_t *, size_t, GpDeck [4], uint8_t [4]);
void game_transfer_import(void);
#endif
