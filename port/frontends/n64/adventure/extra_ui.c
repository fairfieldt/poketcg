#include "game.h"
#include "pack.h"
#include "duel_bridge.h"
#include "gen/constants.h"
#include "gen/text.h"
#include "../../../overworld/gen/data.h"
#include <libdragon.h>
#include <mempak.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "extra_data.h"

static const char *deck_name(const GpDeck *deck)
{
 if(!deck->cards[0])return "Empty deck";
 return deck->name[0]?deck->name:deck->name_text?tcg_text(deck->name_text):"Deck";
}
static void compact_text(const char *source,char *out,size_t capacity)
{
 size_t used=0;bool start=true;
 while(*source && used+1<capacity) {
  unsigned char c=(unsigned char)*source++;
  if(start && (c==' '||c=='\t'))continue;
  if(c=='\n') {
   while(used && out[used-1]==' ')used--;
   if(!used || out[used-1]=='\n'){start=true;continue;}
   start=true;
  } else start=false;
  out[used++]=(char)c;
 }
 while(used && (out[used-1]=='\n'||out[used-1]==' '))used--;
 out[used]=0;
}
static void show_text(const char *title,uint16_t id)
{
 char text[4096];size_t used=0;const unsigned char *source=(const unsigned char *)tcg_text(id);
 for(;*source && used+1<sizeof(text);source++) {
  if(*source==9) {
   size_t n=strlen(game.name);if(n>sizeof(text)-used-1)n=sizeof(text)-used-1;
   memcpy(text+used,game.name,n);used+=n;
  } else text[used++]=(char)*source;
 }
 text[used]=0;n64_message(title,text);
}
void game_glossary(void)
{
 const char *items[18];
 for(unsigned i=0;i<18;i++)items[i]=tcg_text(glossary[i][0]);
 int selected=0;
 for(;;) {
  selected=n64_menu("Glossary",items,18,selected,true);
  if(selected<0)return;
  show_text(items[selected],glossary[selected][1]);
 }
}

static void show_mail(void)
{
 int current=0;
 for(;;) {
  char labels[15][40];const char *items[15];uint8_t slots[15];int count=0;
  for(unsigned slot=0;slot<15;slot++) {
   unsigned value=game.cards.pc_packs[slot],id=value&127;
   if(!id || id>15)continue;
   snprintf(labels[count],sizeof(labels[count]),"Mail %u%s",id,value&128?" - unopened":"");
   slots[count]=(uint8_t)slot;items[count]=labels[count];count++;
  }
  if(!count){n64_message("Mail","No mail.");return;}
  current=n64_menu("Read Mail",items,count,current,true);if(current<0)return;
  unsigned slot=slots[current],id=game.cards.pc_packs[slot]&127;
  show_text(labels[current],gp_mail_text[id][0]);
  if(game.cards.pc_packs[slot]&128) {
   uint8_t cards[20];game_rng_to_cards();unsigned drawn=gp_open_mail(&game.cards,slot,cards);game_rng_to_story();
   if(drawn) {
#ifdef N64
    for(unsigned pack=0;pack<drawn/10;pack++)
     adventure_pack_open(gp_mail_boosters[id][pack],cards+pack*10,game.name);
#else
    char names[20][40];const char *cards_menu[20];
    for(unsigned i=0;i<drawn;i++){n64_card_name(cards[i],names[i],40);cards_menu[i]=names[i];}
    int card=0;
    for(;;){card=n64_menu("Received cards",cards_menu,(int)drawn,card,true);if(card<0)break;n64_card_detail(cards[card]);}
#endif
    game_save();
   }
  }
  if(gp_mail_text[id][1])show_text(labels[current],gp_mail_text[id][1]);
 }
}

typedef struct {char *data;size_t length,capacity;} Report;
static void append(Report *r,const char *format,...)
{
 if(r->length>=r->capacity)return;
 va_list args;va_start(args,format);
 int n=vsnprintf(r->data+r->length,r->capacity-r->length,format,args);va_end(args);
 if(n>0)r->length+=(size_t)n<r->capacity-r->length?(size_t)n:r->capacity-r->length-1;
}
static void report_deck(Report *r,unsigned slot)
{
 const GpDeck *deck=&game.cards.built[slot];uint8_t counts[256]={0};
 append(r,"%s\n%s\n\n",game.name,deck_name(deck));
 for(unsigned i=0;i<60;i++)if(deck->cards[i])counts[deck->cards[i]]++;
 for(unsigned id=1;id<=GP_NUM_CARDS;id++)if(counts[id]) {
  char name[40];n64_card_name(id,name,sizeof(name));append(r,"%2u %03u %s\n",counts[id],id,name);
 }
}
static void report_collection(Report *r)
{
 unsigned total,owned=gp_album_count(&game.cards,&total);
 append(r,"%s\nCard Album: %u/%u\nCards: %u\n\n",game.name,owned,total,gp_total_cards(&game.cards));
 for(unsigned id=1;id<=GP_NUM_CARDS;id++)if(!(game.cards.collection[id]&128)) {
  char name[40];n64_card_name(id,name,sizeof(name));
  append(r,"%03u %2u %.23s\n",id,gp_card_count(&game.cards,id),name);
 }
}
static unsigned le16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static void report_card(Report *r,unsigned id)
{
 char name[40];n64_card_name(id,name,sizeof(name));const uint8_t *card=tcg_card_data((uint8_t)id);
 append(r,"%03u %s\n\n",id,name);
 if(card[CARD_DATA_TYPE]>=TYPE_ENERGY) {
  append(r,"%s\n",tcg_text((uint16_t)le16(card+CARD_DATA_NONPKMN_DESCRIPTION)));return;
 }
 append(r,"HP: %u\nLevel: %u\nRetreat: %u\n\n",card[CARD_DATA_HP],card[CARD_DATA_LEVEL],card[CARD_DATA_RETREAT_COST]);
 for(unsigned i=0;i<2;i++) {
  unsigned base=CARD_DATA_ATTACK1+i*(CARD_DATA_ATTACK2-CARD_DATA_ATTACK1);
  unsigned name_id=le16(card+base+4);
  if(!name_id)continue;
  append(r,"%s (%u)\n",tcg_text((uint16_t)name_id),card[base+10]);
  for(unsigned page=0;page<2;page++) {
   unsigned text=le16(card+base+6+page*2);
   if(text)append(r,"%s\n",tcg_text((uint16_t)text));
  }
  append(r,"\n");
 }
}
static void save_report(const Report *report)
{
 static const char *const ports[]={"Controller 1","Controller 2","Cancel"};
 int port=n64_menu("Save text to Controller Pak",ports,3,0,true);
 if(port<0 || port==2)return;
 if(validate_mempak(port)) {n64_message("Controller Pak","Insert a formatted Controller Pak.");return;}
 unsigned blocks=(unsigned)(report->length+1+255)/256;
 if(get_mempak_free_space(port)<(int)blocks){n64_message("Controller Pak","Not enough free space.");return;}
 entry_structure_t entry={0};entry.vendor=0x54434733u;entry.game_id=0x5443;entry.region=0x45;entry.blocks=(uint8_t)blocks;
 bool chosen=false;
 for(unsigned number=1;number<=99 && !chosen;number++) {
  snprintf(entry.name,sizeof(entry.name),"TCG REPORT %02u.TXT",number);chosen=true;
  for(int i=0;i<16;i++) {
   entry_structure_t existing={0};
   if(!get_mempak_entry(port,i,&existing) && existing.valid && !strcmp(existing.name,entry.name)){chosen=false;break;}
  }
 }
 if(!chosen){n64_message("Controller Pak","No unused report name.");return;}
 uint8_t *data=calloc(blocks,256);if(!data){n64_message("Controller Pak","The report could not be saved.");return;}
 memcpy(data,report->data,report->length);
 int result=write_mempak_entry_data(port,&entry,data);free(data);
 n64_message("Controller Pak",result?"The report could not be saved.":"The text report was saved.");
}
void game_print(void)
{
 static const char *const options[]={"Pokemon cards","Deck configuration","Card list","Exit"};
 int choice=n64_menu("Print",options,4,0,true);if(choice<0||choice==3)return;
 Report report={calloc(1,8192),0,8192};if(!report.data)return;
 if(choice==0) {
  char names[228][40];const char *items[228];uint8_t ids[228];unsigned count=0;
  for(unsigned id=1;id<=GP_NUM_CARDS;id++)if(!(game.cards.collection[id]&128)) {
   n64_card_name(id,names[count],40);items[count]=names[count];ids[count++]=(uint8_t)id;
  }
  int card=n64_menu("Pokemon cards",items,(int)count,0,true);
  if(card<0){free(report.data);return;}report_card(&report,ids[card]);
 } else if(choice==1) {
  const char *items[4];for(unsigned i=0;i<4;i++)items[i]=deck_name(&game.cards.built[i]);
  int slot=n64_menu("Deck configuration",items,4,game.cards.selected_deck,true);
  if(slot<0){free(report.data);return;}report_deck(&report,(unsigned)slot);
 } else report_collection(&report);
 n64_message("Print preview",report.data);
 static const char *const actions[]={"Save text to Controller Pak","Close"};
 if(n64_menu("Print",actions,2,1,true)==0)save_report(&report);
 free(report.data);
}
void game_pc(void)
{
 static const char *const items[]={"Card Album","Read Mail","Glossary","Print","Shut Down"};
 int selected=0;
 for(;;) {
  selected=n64_menu("PC",items,5,selected,true);if(selected<0||selected==4)return;
  if(selected==0)game_album();
  if(selected==1)show_mail();
  if(selected==2)game_glossary();
  if(selected==3)game_print();
 }
}

void game_credits(void)
{
 /* The original fills the chronological master list before presenting it. */
 for(unsigned id=1;id<=10;id++) {
  unsigned i;for(i=0;i<10 && game.ow.masters[i] && game.ow.masters[i]!=id;i++);
  if(i<10 && !game.ow.masters[i])game.ow.masters[i]=(uint8_t)id;
 }
 OwState *saved=malloc(sizeof(*saved));if(!saved)return;*saved=game.ow;
 if(game_host.sound)game_host.sound(game_host.ctx,0x48,MUSIC_CREDITS);
 for(unsigned i=0;i<sizeof(credit_entries)/sizeof(credit_entries[0]);i++) {
  const CreditEntry *entry=&credit_entries[i];unsigned map=entry->map,count=entry->count;
  const CreditNpc *npcs=entry->npcs;
  if(entry->club!=255) {
   unsigned master=game.ow.masters[entry->club];if(!master || master>10)master=1;
   map=credit_club_maps[master-1];npcs=credit_club_npcs[master-1];count=credit_club_counts[master-1];
  }
  game.ow.map=(uint8_t)map;game.ow.x=14;game.ow.y=24;game.ow.direction=NORTH;
  game.ow.overworld_selection=12;game.ow.npc_count=0;
  for(unsigned n=0;n<count && game.ow.npc_count<OW_NPC_LIMIT;n++) {
   const CreditNpc *actor=&npcs[n];
   if(actor->id==NPC_PLAYER_CREDITS){game.ow.x=actor->x;game.ow.y=actor->y;game.ow.direction=actor->dir;continue;}
   game.ow.npcs[game.ow.npc_count++]=(OwNpc){actor->id,actor->x,actor->y,actor->dir,1,ow_npc_headers[actor->id].sprite};
  }
  for(unsigned frame=0;frame<12;frame++)game_draw();
  char text[2048];compact_text(tcg_text(entry->text),text,sizeof(text));n64_message("",text);
 }
 game.ow=*saved;free(saved);
 if(game_host.sound)game_host.sound(game_host.ctx,0x4c,game.ow.default_song);
 game_draw();
}
