#include "../game.h"
#include "../duel_bridge.h"
#include "gen/text.h"
#include "gen/constants.h"
#include <mempak.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

Game game;unsigned game_profile=1;
static unsigned mode,pc_calls,mail_calls,glossary_calls,print_calls,credit_texts,draws,saves,writes;
static bool first_mail_page,second_mail_page,printed_card;
static char text_data[4096][24];
const char *tcg_text(uint16_t id){assert(id<4096);return text_data[id];}
const uint8_t *tcg_card_data(uint8_t id){(void)id;static const uint8_t data[65]={0};return data;}
void n64_card_name(unsigned id,char *out,int cap){snprintf(out,(size_t)cap,"Card%u",id);}
void n64_card_detail(unsigned id){assert(id>0 && id<=228);}
void n64_message(const char *title,const char *text)
{
 if(!title[0])credit_texts++;
 if(!strcmp(text,tcg_text(Mail10Part1Text)))first_mail_page=true;
 if(!strcmp(text,"MARK, page two."))second_mail_page=true;
 if(!strcmp(title,"Print preview") && strstr(text,"002  2 Card2"))printed_card=true;
}
int n64_menu(const char *title,const char *const *items,int count,int initial,bool cancel)
{
 (void)initial;(void)cancel;
 if(!strcmp(title,"PC"))return pc_calls++ ? 4 : mode==1 ? 1:3;
 if(!strcmp(title,"Read Mail")) {
  assert(count==3 && strstr(items[1],"Mail 10") && strstr(items[2],"Mail 2"));return mail_calls++<2 ? (int)mail_calls:-1;
 }
 if(!strcmp(title,"Received cards")){assert(count==20 || count==10);return -1;}
 if(!strcmp(title,"Glossary")){assert(count==18);return glossary_calls++ ? -1:17;}
 if(!strcmp(title,"Print"))return print_calls++ ? 0:2;
 if(!strcmp(title,"Save text to Controller Pak"))return 0;
 fprintf(stderr,"Unexpected menu:%s count%d\n",title,count);assert(0);return -1;
}
void game_draw(void){draws++;}
void game_album(void){assert(0);}
bool game_save(void){saves++;return true;}
void game_rng_to_cards(void){for(unsigned i=0;i<3;i++)game.cards.rng[i]=(uint8_t)(game.ow.rng>>(8*i));}
void game_rng_to_story(void){game.ow.rng=game.cards.rng[0]|(uint32_t)game.cards.rng[1]<<8|(uint32_t)game.cards.rng[2]<<16;}
static void sound(void *ctx,unsigned type,unsigned id){(void)ctx;(void)id;assert(type==0x48 || type==0x4c);}
const OwHost game_host={.sound=sound};
int validate_mempak(int port){assert(port==0);return 0;}
int get_mempak_free_space(int port){assert(port==0);return 100;}
int get_mempak_entry(int port,int index,entry_structure_t *entry){assert(port==0 && index<16);memset(entry,0,sizeof(*entry));return 0;}
int write_mempak_entry_data(int port,entry_structure_t *entry,uint8_t *data)
{
 assert(port==0 && entry->blocks>0 && !strcmp(entry->name,"TCG REPORT 01.TXT"));
 assert(strstr((char *)data,"002  2 Card2"));writes++;return 0;
}
int main(void)
{
 for(unsigned i=0;i<4096;i++)snprintf(text_data[i],24,"Text%u",i);
 strcpy(text_data[Mail2Part2Text],"\x09, page two.");
 uint8_t seed[3]={17,28,31};gp_init(&game.cards,seed);game.ow.rng=0x1f1c11;strcpy(game.name,"MARK");
 assert(gp_give_mail(&game.cards,10));assert(gp_give_mail(&game.cards,2));mode=1;game_pc();
 assert(first_mail_page && second_mail_page && saves==2);
 assert(game.cards.pc_packs[0]==0x81 && game.cards.pc_packs[1]==10 && game.cards.pc_packs[2]==2 && gp_total_cards(&game.cards)==30);
 game_glossary();assert(glossary_calls==2);
 game.ow.map=33;game.ow.x=10;game.ow.y=12;game.ow.masters[0]=5;game.ow.masters[1]=1;
 game_credits();assert(credit_texts==35 && draws==35*12+1);
 assert(game.ow.map==33 && game.ow.x==10 && game.ow.y==12 && game.ow.masters[0]==5 && game.ow.masters[1]==1);
 for(unsigned id=1;id<=10;id++){unsigned found=0;for(unsigned i=0;i<10;i++)found+=game.ow.masters[i]==id;assert(found==1);}
 gp_init(&game.cards,seed);assert(gp_add_card(&game.cards,2));assert(gp_add_card(&game.cards,2));
 mode=2;pc_calls=0;game_pc();assert(printed_card && writes==1);
 puts("Extra UI PASS: mail10 real slot+bothpages+20cards+once/save,RNG,18glossarytopics,35credits/restoredworld,ControllerPakTXTreport");
}
