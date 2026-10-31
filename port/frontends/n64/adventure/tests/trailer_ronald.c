/* No production wiring: the private trailer entry point owns init/presentation.
 * This only loads an untouched engine-generated mid-duel save. */
#include "trailer_ronald.h"
#include "gen/constants.h"
#include <stdio.h>
#include <string.h>
#ifndef TRAILER_RONALD_CHECKPOINT
#define TRAILER_RONALD_CHECKPOINT "rom:/trailer/ronald.duel"
#endif
bool trailer_ronald_load(TcgDuelSetup *setup,uint8_t saved[TCG_SAVE_SIZE])
{
 if(!setup||!saved)return false;
 FILE *f=fopen(TRAILER_RONALD_CHECKPOINT,"rb");if(!f)return false;
 size_t n=fread(saved,1,TCG_SAVE_SIZE,f);int extra=fgetc(f);fclose(f);
 TcgSavedDuel info;
 if(n!=TCG_SAVE_SIZE||extra!=EOF||tcg_saved_duel_info(saved,TCG_SAVE_SIZE,&info)||
    info.opponent_deck_id!=LEGENDARY_RONALD_DECK_ID||info.whose_turn!=TCG_PLAYER)return false;
 *setup=(TcgDuelSetup){.duelist={{.kind=TCG_HUMAN,.deck_id=SQUIRTLE_AND_FRIENDS_DECK_ID,.name="MARK"},
  {.kind=TCG_AI,.deck_id=LEGENDARY_RONALD_DECK_ID,.name="RONALD"}},.prizes=6,.rng={8,0,0x5a},.music=MUSIC_DUEL_THEME_3};
 return true;
}
