#include "../game.h"
#include "../duel_bridge.h"
#include "../transfer_import.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
Game game;
static int answers[5],at,saves,save_ok;
static char last_message[128];
void joypad_poll(void){}
int joypad_is_connected(int p){(void)p;return 1;}
int joypad_get_accessory_type(int p){(void)p;return 1;}
int joybus_accessory_read(int p,uint16_t a,uint8_t *d){(void)p;(void)a;(void)d;return 0;}
int joybus_accessory_write(int p,uint16_t a,const uint8_t *d){(void)p;(void)a;(void)d;return 0;}
uint64_t get_ticks(void){return 0;}
TiResult ti_import_read(const TiBus *b,GpDeck decks[4],uint8_t valid[4])
{(void)b;memset(decks,0,4*sizeof(*decks));memset(valid,0,4);decks[0]=gp_preset_decks[5];valid[0]=1;return TI_OK;}
void n64_message(const char *title,const char *text){(void)title;snprintf(last_message,sizeof(last_message),"%s",text);}
void n64_card_name(unsigned id,char *out,int size){snprintf(out,(size_t)size,"Card %u",id);}
void n64_card_detail(unsigned id){(void)id;}
const char *tcg_text(uint16_t id){(void)id;return "Recipe";}
int n64_menu(const char *title,const char *const *items,int count,int initial,bool cancel)
{(void)title;(void)items;(void)count;(void)initial;(void)cancel;return answers[at++];}
bool game_save(void){saves++;return save_ok;}
static void setup(int confirm,int succeeds)
{memset(&game,0,sizeof(game));game.cards.saved[3]=gp_preset_decks[6];at=saves=0;save_ok=succeeds;answers[0]=0;answers[1]=0;answers[2]=3;answers[3]=confirm;}
int main(void)
{
 setup(1,1);GpDeck before=game.cards.saved[3];game_transfer_import();assert(!saves&&!memcmp(&before,&game.cards.saved[3],sizeof(before)));
 setup(0,1);answers[1]=-1;game_transfer_import();assert(!saves&&at==2);
 setup(0,0);before=game.cards.saved[3];game_transfer_import();assert(saves==1&&!memcmp(&before,&game.cards.saved[3],sizeof(before)));
 setup(0,1);game_transfer_import();assert(saves==1&&!memcmp(game.cards.saved[3].cards,gp_preset_decks[5].cards,60));
 setup(0,1);game.ow.duel_pending=1;game_transfer_import();assert(!saves&&at==0);
 puts("Transfer Pak import confirmation and persistence tests passed");return 0;
}
