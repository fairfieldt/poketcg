#include "game.h"
#include "duel_bridge.h"
#include "transfer_import.h"
#include <libdragon.h>
#include <stdio.h>
#include <string.h>

static int connected(void *ctx){(void)ctx;joypad_poll();return joypad_is_connected(JOYPAD_PORT_1);}
static int accessory(void *ctx)
{(void)ctx;int type=joypad_get_accessory_type(JOYPAD_PORT_1);return type==JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK?1:type==JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK?2:0;}
static int read_block(void *ctx,uint16_t a,uint8_t *p){(void)ctx;return joybus_accessory_read(JOYPAD_PORT_1,a,p);}
static int write_block(void *ctx,uint16_t a,const uint8_t *p){(void)ctx;return joybus_accessory_write(JOYPAD_PORT_1,a,p);}
static uint64_t millis(void *ctx){(void)ctx;return get_ticks()/(TICKS_PER_SECOND/1000);}
static void progress(void *ctx,unsigned step)
{(void)ctx;n64_message("Transfer Pak",step==1?"Checking cartridge. Press A to continue.":"Reading saved decks. Press A to continue.");}

void game_transfer_import(void)
{
 if(game.ow.duel_pending){n64_message("Transfer Pak","Finish the saved duel before importing.");return;}
 const TiBus bus={NULL,connected,accessory,read_block,write_block,millis,progress};
 GpDeck decks[4];uint8_t valid[4];
 TiResult result=ti_import_read(&bus,decks,valid);
 if(result!=TI_OK) {
  static const char *const errors[]={"", "Connect a controller to port 1.","Connect a Transfer Pak to controller 1.",
   "Insert a Game Boy cartridge.","Cartridge connection lost. Try again.","Reading timed out. Try again.",
   "Cartridge header could not be read. Try again.","This is not a Pokemon TCG cartridge.",
   "Only the US English Pokemon TCG cartridge is supported.","The saved game is missing or damaged.","No usable built decks were found.","Replace the Controller Pak with a Transfer Pak."};
  n64_message("Transfer Pak",errors[result]);return;
 }
 char labels[4][40];const char *items[4];int indexes[4],n=0;
 for(int i=0;i<4;i++)if(valid[i]){snprintf(labels[n],40,"%d. %.30s",i+1,decks[i].name[0]?decks[i].name:"Deck");items[n]=labels[n];indexes[n++]=i;}
 int choice=n64_menu("Import deck",items,n,0,true);if(choice<0)return;
 GpDeck *deck=&decks[indexes[choice]];
 uint8_t counts[256]={0},ids[229];char cards[229][40];const char *card_items[229];int card_count=1;
 card_items[0]="Choose recipe slot";
 for(unsigned i=0;i<60;i++)counts[deck->cards[i]]++;
 for(unsigned id=1;id<=GP_NUM_CARDS;id++)if(counts[id]){char name[40];n64_card_name(id,name,sizeof(name));snprintf(cards[card_count],40,"%u %.30s",counts[id],name);card_items[card_count]=cards[card_count];ids[card_count++]=id;}
 for(;;){int row=n64_menu("Deck cards",card_items,card_count,0,true);if(row<0)return;if(!row)break;n64_card_detail(ids[row]);}
 char slots[60][40];const char *destinations[60];
 for(int i=0;i<60;i++){GpDeck *old=&game.cards.saved[i];snprintf(slots[i],40,"%02d %.30s",i+1,old->cards[0]?(old->name[0]?old->name:old->name_text?tcg_text(old->name_text):"Recipe"):"Empty");destinations[i]=slots[i];}
 int dest=n64_menu("Choose recipe slot",destinations,60,0,true);if(dest<0)return;
 const char *const confirm[]={game.cards.saved[dest].cards[0]?"Replace recipe":"Save recipe","Cancel"};
 if(n64_menu(slots[dest],confirm,2,1,true)!=0)return;
 GpDeck previous=game.cards.saved[dest];
 if(gp_save_recipe(&game.cards,dest,deck)!=GP_DECK_OK){n64_message("Transfer Pak","The deck could not be imported.");return;}
 if(!game_save()){game.cards.saved[dest]=previous;n64_message("Transfer Pak","Could not save the recipe.");return;}
 n64_message("Transfer Pak","Recipe saved. Build it using your own cards.");
}
