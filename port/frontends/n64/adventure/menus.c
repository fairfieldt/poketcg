#include "game.h"
#include "pack.h"
#include "duel_bridge.h"
#include "gen/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int menu(const char *title,const char *const *items,int n,int initial)
{return n64_menu(title,items,n,initial,true);}
static const char *deck_label(const GpDeck *deck)
{
 if(!deck->cards[0])return "Empty deck";
 return deck->name[0]?deck->name:deck->name_text?tcg_text(deck->name_text):"Deck";
}
static const char *deck_error(int error)
{
 switch(error) {
 case GP_DECK_BAD_SIZE:return "A deck must contain 60 cards.";
 case GP_DECK_BAD_CARD:return "Choose valid cards for this deck.";
 case GP_DECK_NO_BASIC:return "Add at least one Basic Pokemon.";
 case GP_DECK_TOO_MANY_SAME_NAME:return "Use no more than four cards with the same name, except Basic Energy.";
 case GP_DECK_NOT_OWNED:return "You do not have enough copies of these cards.";
 case GP_DECK_LAST:return "Keep at least one complete deck.";
 default:return "The deck could not be changed.";
 }
}
void game_name(const char *title,char *out,unsigned capacity)
{
 static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.!";
 const char *items[sizeof(alphabet)+2];char chars[sizeof(alphabet)][2];
 items[0]="Done";items[1]="Delete";
 unsigned n=(unsigned)strlen(out),count=(unsigned)strlen(alphabet);
 for(unsigned i=0;i<count;i++){chars[i][0]=alphabet[i];chars[i][1]=0;items[i+2]=chars[i];}
 for(;;) {
  char label[80];snprintf(label,sizeof(label),"%s: %.24s",title,out);
  int selected=menu(label,items,count+2,0);
  if(selected<=0)return;
  if(selected==1){if(n)out[--n]=0;}
  else if(n+1<capacity){out[n++]=alphabet[selected-2];out[n]=0;}
 }
}
static void view_deck(const GpDeck *deck)
{
 uint8_t counts[256]={0},ids[256];int n=0;
 char labels[228][40];const char *items[228];
 for(unsigned i=0;i<60;i++)counts[deck->cards[i]]++;
 for(unsigned id=1;id<=GP_NUM_CARDS;id++)if(counts[id]) {
  char name[40];n64_card_name(id,name,sizeof(name));
  snprintf(labels[n],sizeof(labels[n]),"%2u %.30s",counts[id],name);items[n]=labels[n];ids[n++]=id;
 }
 int cur=0;
 while(n){cur=menu(deck_label(deck),items,n,cur);if(cur<0)return;n64_card_detail(ids[cur]);}
}
static bool edit_deck(unsigned slot)
{
 GpDeck draft=game.cards.built[slot];uint8_t counts[256]={0};unsigned total=0;
 for(unsigned i=0;i<60;i++)if(draft.cards[i]){counts[draft.cards[i]]++;total++;}
 int cur=0,filter=0;
 static const char *const filters[]={"All cards","Fire","Grass","Lightning","Water","Fighting","Psychic","Colorless","Trainer","Energy"};
 for(;;) {
  uint8_t ids[229];char labels[229][40];const char *items[229];int n=1;
  snprintf(labels[0],40,"Filter: %s",filters[filter]);items[0]=labels[0];ids[0]=0;
  for(unsigned id=1;id<=GP_NUM_CARDS;id++) {
   unsigned type=gp_cards[id].type;
   if(filter>=1&&filter<=7&&type!=(unsigned)(filter-1))continue;
   if(filter==8&&type<16)continue;
   if(filter==9&&(type<8||type>=16))continue;
   unsigned owned=gp_collection_count(&game.cards,id);
   for(unsigned i=0;i<60;i++)owned+=game.cards.built[slot].cards[i]==id;
   if(!owned)continue;
   char name[40];n64_card_name(id,name,sizeof(name));
   snprintf(labels[n],sizeof(labels[n]),"%u/%u %.23s",counts[id],owned,name);items[n]=labels[n];ids[n++]=id;
  }
  char title[48];snprintf(title,sizeof(title),"Build deck: %u/60 cards",total);
  cur=menu(title,items,n,cur);
  if(cur<0) {
   static const char *const choices[]={"Save deck","Keep editing","Discard changes"};
   int action=menu("Deck",choices,3,0);
   if(action==2)return false;
   if(action!=0){cur=0;continue;}
   if(total!=60){n64_message("Deck",deck_error(GP_DECK_BAD_SIZE));cur=0;continue;}
   unsigned at=0;for(unsigned id=1;id<=GP_NUM_CARDS;id++)for(unsigned k=0;k<counts[id];k++)draft.cards[at++]=id;
   if(!draft.name[0])snprintf(draft.name,sizeof(draft.name),"Deck %u",slot+1);
   int error=gp_set_deck(&game.cards,slot,&draft);
   if(error){n64_message("Deck",deck_error(error));cur=0;continue;}
   return true;
  }
  if(cur==0){int selected=menu("Card type",filters,10,filter);if(selected>=0)filter=selected;continue;}
  unsigned id=ids[cur];static const char *const actions[]={"Add","Remove","Card details"};
  int action=menu(labels[cur],actions,3,0);
  if(action==0) {
   unsigned owned=gp_collection_count(&game.cards,id);
   for(unsigned i=0;i<60;i++)owned+=game.cards.built[slot].cards[i]==id;
   if(total<60&&counts[id]<owned){counts[id]++;total++;}
  } else if(action==1&&counts[id]){counts[id]--;total--;}
  else if(action==2)n64_card_detail(id);
 }
}
static void recipes(unsigned built_slot,bool save)
{
 char labels[60][40];const char *items[60];
 for(unsigned i=0;i<60;i++){snprintf(labels[i],40,"%02u %.30s",i+1,deck_label(&game.cards.saved[i]));items[i]=labels[i];}
 int slot=menu(save?"Save deck recipe":"Load deck recipe",items,60,0);if(slot<0)return;
 int error;
 if(save) {
  if(game.cards.saved[slot].cards[0]){static const char *const choices[]={"Replace recipe","Cancel"};if(menu("Save deck recipe",choices,2,1)!=0)return;}
  error=gp_save_recipe(&game.cards,slot,&game.cards.built[built_slot]);
 } else {
  if(!game.cards.saved[slot].cards[0])return;
  static const char *const choices[]={"Build deck","View cards","Rename recipe","Delete recipe"};
  int action=menu(labels[slot],choices,4,0);
  if(action==1){view_deck(&game.cards.saved[slot]);return;}
  if(action==2){game_name("Recipe name",game.cards.saved[slot].name,GP_NAME_SIZE);return;}
  if(action==3){static const char *const confirm[]={"Delete recipe","Cancel"};if(menu("Saved recipe",confirm,2,1)==0)memset(&game.cards.saved[slot],0,sizeof(GpDeck));return;}
  if(action!=0)return;
  int mask=gp_find_dismantle_mask(&game.cards,game.cards.saved[slot].cards);
  if(mask<0){n64_message("Deck",deck_error(GP_DECK_NOT_OWNED));return;}
  if(mask){static const char *const confirm[]={"Dismantle required decks","Cancel"};if(menu("Build deck",confirm,2,1)!=0)return;}
  error=gp_build_deck(&game.cards,&game.cards.saved[slot],built_slot,(unsigned)mask);
 }
 if(error)n64_message("Deck",deck_error(error));
}
void game_decks(void)
{
 for(;;) {
  char labels[4][40];const char *items[4];
  for(unsigned i=0;i<4;i++){snprintf(labels[i],40,"%s %.29s",i==game.cards.selected_deck?">":" ",deck_label(&game.cards.built[i]));items[i]=labels[i];}
  int slot=menu("Decks",items,4,game.cards.selected_deck);if(slot<0)return;
  static const char *const actions[]={"View cards","Modify deck","Select for duels","Rename","Dismantle","Save recipe","Load recipe"};
  int action=menu(labels[slot],actions,7,0),error=0;
  switch(action) {
   case 0:view_deck(&game.cards.built[slot]);break;
   case 1:edit_deck(slot);break;
   case 2:error=gp_validate_deck(game.cards.built[slot].cards);if(!error)game.cards.selected_deck=slot;break;
   case 3:game_name("Deck name",game.cards.built[slot].name,GP_NAME_SIZE);break;
   case 4:error=gp_dismantle_deck(&game.cards,slot);break;
   case 5:recipes(slot,true);break;
   case 6:recipes(slot,false);break;
  }
  if(error)n64_message("Deck",deck_error(error));
 }
}
void game_album(void)
{
 char labels[228][40];const char *items[228];
 for(unsigned i=1;i<=GP_NUM_CARDS;i++) {
  char name[40];n64_card_name(i,name,sizeof(name));
  bool seen=!(game.cards.collection[i]&GP_CARD_NOT_OWNED)||gp_card_count(&game.cards,i);
  snprintf(labels[i-1],40,"%03u %2u %.25s",i,gp_card_count(&game.cards,i),seen?name:"-----");items[i-1]=labels[i-1];
 }
 int cur=0;
 for(;;){cur=menu("Card Album",items,228,cur);if(cur<0)return;if(!(game.cards.collection[cur+1]&GP_CARD_NOT_OWNED)||gp_card_count(&game.cards,cur+1))n64_card_detail(cur+1);}
}
void game_deck_machine(unsigned machine)
{
 if(!machine) {
  static const char *const actions[]={"Save a deck","Saved recipes","Exit"};
  int action=menu("Deck Save Machine",actions,3,0);if(action<0||action==2)return;
  const char *slots[4];for(unsigned i=0;i<4;i++)slots[i]=deck_label(&game.cards.built[i]);
  int slot=menu(action==0?"Choose deck to save":"Choose deck slot",slots,4,game.cards.selected_deck);
  if(slot>=0)recipes((unsigned)slot,action==0);
  return;
 }
 if(machine>10)return;
 unsigned start=(machine-1)*5,count=5;
 char labels[50][40];const char *items[50];
 for(unsigned i=0;i<count;i++){snprintf(labels[i],40,"%.35s",tcg_text(gp_auto_recipes[start+i].name_text));items[i]=labels[i];}
 int cur=menu("Auto Deck Machine",items,count,0);if(cur<0)return;
 unsigned recipe=start+cur;static const char *const actions[]={"Build this deck","Read description"};
 int action=menu(labels[cur],actions,2,0);
 if(action==1){n64_message(labels[cur],tcg_text(gp_auto_recipes[recipe].description_text));return;}
 if(action!=0)return;
 int mask=gp_find_dismantle_mask(&game.cards,gp_auto_recipes[recipe].cards);
 if(mask<0){n64_message("Deck","You do not have all the cards for this deck.");return;}
 char slots[4][40];const char *slot_items[4];
 for(unsigned i=0;i<4;i++){snprintf(slots[i],40,"%u %.30s",i+1,deck_label(&game.cards.built[i]));slot_items[i]=slots[i];}
 int slot=menu("Choose deck slot",slot_items,4,0);if(slot<0)return;
 if(mask){static const char *const yesno[]={"Dismantle required decks","Cancel"};if(menu("Build deck",yesno,2,1)!=0)return;}
 int error=gp_build_auto_deck(&game.cards,recipe,slot,mask);
 if(error)n64_message("Deck",deck_error(error));
}
void game_show_booster(unsigned booster)
{
 uint8_t cards[10];game_rng_to_cards();bool opened=gp_open_booster(&game.cards,booster,cards);game_rng_to_story();if(!opened)return;
#ifdef N64
 adventure_pack_open(booster,cards,game.name);
#else
 char labels[10][40];const char *items[10];
 for(unsigned i=0;i<10;i++){n64_card_name(cards[i],labels[i],40);items[i]=labels[i];}
 int cur=0;for(;;){cur=menu("Booster pack",items,10,cur);if(cur<0)return;n64_card_detail(cards[cur]);}
#endif
}
void game_pause(void)
{
 static const char *const items[]={"Decks","Card Album","Player","Diary","Config","Return"};
 for(;;) {
  int selected=menu("Menu",items,6,0);
  if(selected<0||selected==5)return;
  if(selected==0)game_decks();
  if(selected==1)game_album();
  if(selected==2){char text[192];unsigned total;unsigned album=gp_album_count(&game.cards,&total);snprintf(text,sizeof(text),"%s\n\nCards: %u\nCard Album: %u / %u\nMaster Medals: %u\nTime: %u:%02u:%02u",game.name,gp_total_cards(&game.cards),album,total,ow_event(&game.ow,0x2e),(unsigned)(game.play_seconds/3600),(unsigned)(game.play_seconds/60%60),(unsigned)(game.play_seconds%60));n64_message("Player",text);}
  if(selected==3)n64_message("Diary",game_save()?"Your diary was saved.":"The diary could not be saved.");
  if(selected==4) {
   static const char *const config[]={"Text speed","Duel animation","Exit"};
   int option=menu("Config",config,3,0);
   if(option==0){static const char *const speeds[]={"Slowest","Slow","Normal","Fast","Fastest"};int speed=menu("Text speed",speeds,5,game.text_speed);if(speed>=0){game.text_speed=speed;n64_ui_text_speed(speed);}}
   if(option==1){static const char *const animations[]={"Show all","Skip some","None"};int setting=menu("Duel animation",animations,3,game.animations_disabled?2:game.skip_delay_allowed?1:0);if(setting>=0){game.animations_disabled=setting==2;game.skip_delay_allowed=setting!=0;}}
   game_store_settings();
  }
 }
}
