/* Private capture only: untouched engine-generated final-Pokemon checkpoint. */
#include "trailer_amy.h"
#include "gen/constants.h"
#include <stdio.h>
#ifndef TRAILER_AMY_CHECKPOINT
#define TRAILER_AMY_CHECKPOINT "rom:/trailer/amy.duel"
#endif
bool trailer_amy_load(TcgDuelSetup *setup,uint8_t saved[TCG_SAVE_SIZE])
{
 if(!setup||!saved)return false;
 FILE *f=fopen(TRAILER_AMY_CHECKPOINT,"rb");if(!f)return false;
 size_t n=fread(saved,1,TCG_SAVE_SIZE,f);int extra=fgetc(f);fclose(f);
 TcgSavedDuel info;
 if(n!=TCG_SAVE_SIZE||extra!=EOF||tcg_saved_duel_info(saved,TCG_SAVE_SIZE,&info)||
    info.opponent_deck_id!=GO_GO_RAIN_DANCE_DECK_ID||info.whose_turn!=TCG_PLAYER)return false;
 *setup=(TcgDuelSetup){.duelist={{.kind=TCG_HUMAN,.deck_id=PIKACHU_DECK_ID,.name="MARK"},
  {.kind=TCG_AI,.deck_id=GO_GO_RAIN_DANCE_DECK_ID,.name="AMY"}},.prizes=6,.rng={25,1,0x5a},.music=MUSIC_DUEL_THEME_2};
 return true;
}
