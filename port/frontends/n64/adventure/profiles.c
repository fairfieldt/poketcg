#include "game.h"
#include "duel_bridge.h"
#include "../../common/save_store.h"
#include <libdragon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned game_profile=1;
static const char *profile_file(unsigned profile){return profile==2?"adventure2":"adventure1";}
extern bool adventure_save_pair(const char *,const uint8_t *,unsigned,const char *,const uint8_t *,unsigned);

void game_rng_to_cards(void)
{for(unsigned i=0;i<3;i++)game.cards.rng[i]=game.ow.rng>>(8*i);}
void game_rng_to_story(void)
{game.ow.rng=(uint32_t)game.cards.rng[0]|(uint32_t)game.cards.rng[1]<<8|(uint32_t)game.cards.rng[2]<<16;}
static unsigned encode(const Game *state,uint8_t data[8192])
{
 size_t ow_size=ow_save_size();unsigned size=48+ow_size+GP_SAVE_SIZE;
 if(size>8192)return 0;
 memset(data,0,48);memcpy(data,"TCGADV02",8);memcpy(data+8,state->name,16);
 data[24]=(state->animations_disabled?1:0)|(state->skip_delay_allowed?2:0);data[25]=state->text_speed;data[26]=ow_size>>8;data[27]=ow_size;
 data[28]=state->duel_mode;
 data[29]=state->play_seconds>>16;data[30]=state->play_seconds>>8;data[31]=state->play_seconds;
 memcpy(data+32,state->link_name,16);
 if(!ow_save_encode(&state->ow,data+48,ow_size)||!gp_save_encode(&state->cards,data+48+ow_size,GP_SAVE_SIZE))return 0;
 return size;
}
bool game_read_profile(unsigned profile,Game *out)
{
 static uint8_t data[8192];int size=save_store_read(profile_file(profile),data,sizeof(data));size_t ow_size=ow_save_size();
 if(size!=(int)(48+ow_size+GP_SAVE_SIZE)||memcmp(data,"TCGADV02",8)||((unsigned)data[26]<<8|data[27])!=ow_size)return false;
 Game *temp=calloc(1,sizeof(*temp));if(!temp)return false;
 bool ok=ow_save_decode(&temp->ow,data+48,ow_size)&&gp_save_decode(&temp->cards,data+48+ow_size,GP_SAVE_SIZE);
 if(ok){memcpy(temp->name,data+8,16);temp->name[15]=0;memcpy(temp->link_name,data+32,16);temp->animations_disabled=(data[24]&1)!=0;temp->skip_delay_allowed=(data[24]&2)!=0;temp->text_speed=data[25];temp->duel_mode=data[28];temp->play_seconds=(uint32_t)data[29]<<16|(uint32_t)data[30]<<8|data[31];*out=*temp;}
 free(temp);return ok;
}
bool game_load(void){return game_read_profile(game_profile,&game);}
void game_store_settings(void)
{
 Game *saved=malloc(sizeof(*saved));if(!saved)return;
 if(game_read_profile(game_profile,saved)) {
  saved->animations_disabled=game.animations_disabled;saved->skip_delay_allowed=game.skip_delay_allowed;saved->text_speed=game.text_speed;
  static uint8_t data[8192];unsigned n=encode(saved,data);if(n)save_store_write(profile_file(game_profile),data,n);
 }
 free(saved);
}
bool game_save(void)
{static uint8_t data[8192];unsigned n=encode(&game,data);return n&&save_store_write(profile_file(game_profile),data,n);}
bool game_save_peer(Game *peer)
{
 static uint8_t first[8192],second[8192];unsigned a=encode(&game,first),b=encode(peer,second);
 return a&&b&&adventure_save_pair(profile_file(game_profile),first,a,profile_file(3-game_profile),second,b);
}
static Game *read_peer(void)
{
 if(game.ow.duel_pending){n64_message("Diary","Finish the saved duel before connecting.");return NULL;}
#ifdef TCG_WEB_ADVENTURE
 extern bool game_choose_peer(void);
 if(!game_choose_peer())return NULL;
#endif
 Game *peer=malloc(sizeof(*peer));if(!peer)return NULL;
 if(!game_read_profile(3-game_profile,peer)){free(peer);n64_message("Other diary","Create the other diary before connecting.");return NULL;}
#ifndef TCG_WEB_ADVENTURE
 if(peer->ow.duel_pending){free(peer);n64_message("Other diary","Finish the saved duel before connecting.");return NULL;}
#endif
 return peer;
}
bool game_card_pop(void)
{
 Game *peer=read_peer();if(!peer)return true;
 Game *backup=malloc(sizeof(*backup));if(!backup){free(peer);return false;}*backup=game;
 uint8_t cards[2];game_rng_to_cards();
 bool ok=gp_card_pop_pair(&game.cards,game.link_name,&peer->cards,peer->link_name,cards);
 if(ok){game_rng_to_story();peer->ow.rng=(uint32_t)peer->cards.rng[0]|(uint32_t)peer->cards.rng[1]<<8|(uint32_t)peer->cards.rng[2]<<16;ok=game_save_peer(peer);}
 if(!ok){game=*backup;n64_message("Card Pop!","These diaries could not Card Pop! Check whether they have already connected.");}
 else {n64_card_detail(cards[0]);n64_card_detail(cards[1]);}
 free(backup);free(peer);return true;
}
static unsigned choose_cards(const GpState *sender,uint8_t selected[60])
{
 unsigned total=0;uint8_t counts[256]={0};int cur=0;
 for(;;) {
  char labels[229][40];const char *items[229];uint8_t ids[229];int count=1;
  items[0]="Send selected cards";ids[0]=0;
  for(unsigned id=1;id<=GP_NUM_CARDS;id++)if(gp_collection_count(sender,id)) {
   char name[40];n64_card_name(id,name,sizeof(name));
   snprintf(labels[count],40,"%u/%u %.23s",counts[id],gp_collection_count(sender,id),name);items[count]=labels[count];ids[count++]=id;
  }
  char title[48];snprintf(title,sizeof(title),"Cards to send: %u/60",total);
  cur=n64_menu(title,items,count,cur,true);if(cur<0)return 0;
  if(cur==0){if(!total)continue;unsigned at=0;for(unsigned id=1;id<=GP_NUM_CARDS;id++)for(unsigned k=0;k<counts[id];k++)selected[at++]=id;return total;}
  unsigned id=ids[cur];static const char *const actions[]={"Add","Remove","Card details"};
  int action=n64_menu(labels[cur],actions,3,0,true);
  if(action==0&&total<60&&counts[id]<gp_collection_count(sender,id)){counts[id]++;total++;}
  if(action==1&&counts[id]){counts[id]--;total--;}
  if(action==2)n64_card_detail(id);
 }
}
static int choose_recipe(const Game *owner,const char *title)
{
 char labels[60][40];const char *items[60];
 for(unsigned i=0;i<60;i++){const GpDeck *deck=&owner->cards.saved[i];const char *name=!deck->cards[0]?"Empty":deck->name[0]?deck->name:tcg_text(deck->name_text);snprintf(labels[i],40,"%02u %.30s",i+1,name);items[i]=labels[i];}
 return n64_menu(title,items,60,0,true);
}
bool game_link(unsigned operation)
{
 Game *peer=read_peer();if(!peer)return true;
 Game *backup=malloc(sizeof(*backup));if(!backup){free(peer);return false;}*backup=game;
 bool ok=true;
 if(operation==4) {
  if(!joypad_is_connected(JOYPAD_PORT_2)){n64_message("Battle Center","Connect a second controller.");goto done;}
  TcgDuelSetup setup;memset(&setup,0,sizeof(setup));setup.prizes=6;
  setup.animations_disabled=game.animations_disabled;setup.skip_delay_allowed=game.skip_delay_allowed;
  setup.duelist[0].kind=TCG_HUMAN;setup.duelist[0].name=game.name;
  setup.duelist[1].kind=TCG_HUMAN;setup.duelist[1].name=peer->name;
  memcpy(setup.duelist[0].cards,game.cards.built[game.cards.selected_deck].cards,60);
  memcpy(setup.duelist[1].cards,peer->cards.built[peer->cards.selected_deck].cards,60);
  if(gp_validate_deck(setup.duelist[0].cards)||gp_validate_deck(setup.duelist[1].cards))n64_message("Battle Center","Each diary needs a complete deck.");
  else {
   static const char *const choices[]={"2 prizes","3 prizes","4 prizes","5 prizes","6 prizes"};
   int prize=n64_menu("Battle Center",choices,5,4,true);
   if(prize>=0){setup.prizes=prize+2;game_rng_to_cards();memcpy(setup.rng,game.cards.rng,3);game_save();uint32_t started=game.play_seconds;rspq_wait();int result=n64_duel_play(&setup,NULL);n64_duel_rng(game.cards.rng);game_rng_to_story();memcpy(peer->cards.rng,game.cards.rng,3);peer->ow.rng=game.ow.rng;peer->play_seconds+=game.play_seconds-started;if(peer->play_seconds>3599999)peer->play_seconds=3599999;ok=game_save_peer(peer);n64_message("Battle Center",result==0?"Player 1 wins.":"Player 2 wins.");}
  }
 } else {
  Game *sender=(operation==0||operation==2)?&game:peer,*receiver=sender==&game?peer:&game;
  if(operation<2) {
   uint8_t cards[60];unsigned n=choose_cards(&sender->cards,cards),received=0;
   if(n)ok=gp_transfer_cards(&sender->cards,&receiver->cards,cards,n,&received)==GP_LINK_OK&&game_save_peer(peer);
  } else {
   int from=choose_recipe(sender,"Choose deck to send");
   int to=from>=0?choose_recipe(receiver,"Choose recipe slot"):-1;
   if(to>=0){bool overwrite=false;if(receiver->cards.saved[to].cards[0]){static const char *const options[]={"Replace recipe","Cancel"};overwrite=n64_menu("Recipe slot",options,2,1,false)==0;if(!overwrite)goto done;}
    ok=gp_transfer_recipe(&sender->cards,from,&receiver->cards,to,overwrite)==GP_LINK_OK&&game_save_peer(peer);}
  }
 }
done:
 if(!ok){game=*backup;n64_message("Gift Center","The exchange could not be completed. Both diaries were kept.");}
 free(backup);free(peer);return true;
}
