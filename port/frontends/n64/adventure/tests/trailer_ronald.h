#ifndef TRAILER_RONALD_H
#define TRAILER_RONALD_H
#include <stdbool.h>
#include "tcg.h"
/* Private capture ROM only. Add generated ronald.duel at this DFS path. */
bool trailer_ronald_load(TcgDuelSetup *setup,uint8_t saved[TCG_SAVE_SIZE]);
#endif
