/* The actual deck editor in menus.c, driven by scripted list buttons. */
#include "../game.h"
#include "../duel_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../menus.c"

Game game;
const OwHost game_host={0};
static unsigned details,last_detail,list_calls;
static char last_message[128],last_title[64];
typedef struct {int row;int key;} Press;
static const Press *script;static unsigned script_at,script_len;
static const int *menu_answers;static unsigned menu_at;

const char *tcg_text(uint16_t id){assert(id);return "Bulbasaur & Friends";}
void n64_card_name(unsigned id,char *out,int capacity){snprintf(out,(size_t)capacity,"Card%u",id);}
void n64_card_detail(unsigned id){assert(id>=1 && id<=228);details++;last_detail=id;}
void n64_message(const char *title,const char *text){(void)title;snprintf(last_message,sizeof(last_message),"%s",text);}
void n64_ui_text_speed(unsigned speed){(void)speed;}
int n64_menu(const char *title,const char *const *items,int count,int initial,bool cancel)
{
 (void)title;(void)items;(void)count;(void)initial;(void)cancel;
 return menu_answers[menu_at++];
}
int n64_list(const char *title,const char *const *items,int count,int *cursor,const char *help)
{
 assert(help && strchr(help,'\n'));
 snprintf(last_title,sizeof(last_title),"%s",title);list_calls++;
 assert(!strncmp(items[0],"Show: ",6));
 assert(script_at<script_len);
 const Press *p=&script[script_at++];
 if(p->row>=0)*cursor=p->row;
 assert(*cursor<count);
 return p->key;
}
bool game_save(void){return true;}
void game_store_settings(void){}
void game_rng_to_cards(void){}
void game_rng_to_story(void){}

static unsigned deck_count(const uint8_t cards[60],unsigned id)
{unsigned n=0;for(unsigned i=0;i<60;i++)n+=cards[i]==id;return n;}
static void run(const Press *presses,unsigned n,const int *answers)
{
 script=presses;script_len=n;script_at=0;menu_answers=answers;menu_at=0;
}
int main(void)
{
 uint8_t seed[3]={1,2,3};gp_init(&game.cards,seed);
 assert(gp_give_starter(&game.cards,2));
 GpDeck before=game.cards.built[0];assert(before.name_text && !before.name[0]);
 /* The starter's extra cards are owned beyond the deck, so the first owned
    card shown can go past 60. Find one with a spare copy. */
 unsigned spare=0;for(unsigned id=1;id<=GP_NUM_CARDS&&!spare;id++)if(gp_collection_count(&game.cards,id))spare=id;
 assert(spare);
 unsigned row=1;for(unsigned id=1;id<spare;id++){unsigned owned=gp_collection_count(&game.cards,id);for(unsigned i=0;i<60;i++)owned+=before.cards[i]==id;row+=owned!=0;}
 int r=(int)row;
 /* Add past 60, try to save, remove back to 60, show details, save. */
 const Press first[]={{r,N64_LIST_RIGHT},{-1,N64_LIST_START},{-1,N64_LIST_B},{-1,N64_LIST_LEFT},{-1,N64_LIST_A},{-1,N64_LIST_B}};
 const int answers[]={0,0};
 run(first,6,answers);
 assert(edit_deck(0));
 assert(!strcmp(last_message,"Remove 1 card to save this deck."));
 assert(details==2 && last_detail==spare);
 assert(!strcmp(last_title,"Bulbasaur & Friends 60/60"));
 assert(!game.cards.built[0].name[0] && game.cards.built[0].name_text==before.name_text);
 for(unsigned id=1;id<=GP_NUM_CARDS;id++)assert(deck_count(game.cards.built[0].cards,id)==deck_count(before.cards,id));
 /* The filter row cycles with Left/Right; Discard keeps the saved deck. */
 const Press second[]={{0,N64_LIST_RIGHT},{0,N64_LIST_LEFT},{0,N64_LIST_LEFT},{-1,N64_LIST_B}};
 const int discard[]={2};
 GpDeck saved=game.cards.built[0];run(second,4,discard);
 assert(!edit_deck(0));assert(!memcmp(&saved,&game.cards.built[0],sizeof(saved)));
 puts("DECK EDITOR PASS: Left/Right counts past 60, save blocked until 60, START/A details, filter cycling, preset name kept");
 return 0;
}
