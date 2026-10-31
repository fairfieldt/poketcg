/* Actual profile UI/serialization and SRAM journal, with scripted menus and
   cartridge DMA fault injection. No replacement implementations of those files. */
#define ADVENTURE_SRAM_TEST 1
#include "../game.h"
#include "../duel_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../save_sram.c"
#include "../profiles.c"

Game game;
const OwHost game_host={0};
static uint8_t cartridge[32768],baseline[32768];
static int cut_after=-1,operation,partial_bytes;
static unsigned messages,details,duels,card_to_send,quantity,source_recipe,destination_recipe;
static bool cancel_overwrite;
static bool second_controller=true;
static const char *recipe_expected;
static char last_message[256];
static TcgDuelSetup captured_duel;
static char captured_names[2][16];

void dma_wait(void) {}
void rspq_wait(void) {}
bool joypad_is_connected(joypad_port_t port){assert(port==JOYPAD_PORT_2);return second_controller;}
void data_cache_hit_writeback(const void *p,unsigned long n){(void)p;(void)n;}
void data_cache_hit_invalidate(void *p,unsigned long n){(void)p;(void)n;}
void dma_read_async(void *p,unsigned long address,unsigned long n)
{
 assert(address>=0x08000000u && address+n<=0x08008000u);
 memcpy(p,cartridge+address-0x08000000u,n);
}
void dma_write_raw_async(const void *p,unsigned long address,unsigned long n)
{
 assert(address>=0x08000000u && address+n<=0x08008000u);
 int this_operation=operation++;
 if(cut_after<0 || this_operation<cut_after)memcpy(cartridge+address-0x08000000u,p,n);
 else if(this_operation==cut_after && partial_bytes>0) {
  unsigned amount=(unsigned)partial_bytes;if(amount>n)amount=(unsigned)n;
  memcpy(cartridge+address-0x08000000u,p,amount);
 }
}
void n64_message(const char *title,const char *text){assert(title && text);messages++;snprintf(last_message,sizeof(last_message),"%s",text);}
void n64_card_name(unsigned id,char *out,int capacity){snprintf(out,(size_t)capacity,"Card%u",id);}
const char *tcg_text(uint16_t text){assert(text);return "Preset deck";}
void n64_card_detail(unsigned id){assert(id>=1 && id<=228);details++;}
void n64_duel_rng(uint8_t out[3]){out[0]=37;out[1]=51;out[2]=73;}
int n64_duel_play(const TcgDuelSetup *setup,const uint8_t *saved)
{
 assert(!saved);captured_duel=*setup;
 for(unsigned i=0;i<2;i++)snprintf(captured_names[i],16,"%.15s",setup->duelist[i].name);
 /* Link duels do not save/continue in the original or the N64 bridge. */
 game.play_seconds+=12;
 duels++;return 0;
}
int n64_menu(const char *title,const char *const *items,int count,int initial,bool cancel)
{
 (void)initial;(void)cancel;
 if(!strncmp(title,"Cards to send:",14)) {
  if(!quantity)return 0;
  char wanted[32];snprintf(wanted,sizeof(wanted),"Card%u",card_to_send);
  for(int i=1;i<count;i++)if(strstr(items[i],wanted))return i;
  fprintf(stderr,"Sender does not expose requested %s\n",wanted);assert(0);
 }
 if(count==3 && !strcmp(items[0],"Add")){assert(quantity);quantity--;return 0;}
 if(!strcmp(title,"Choose deck to send")) {
  assert(source_recipe<(unsigned)count);
  assert(strstr(items[0],"Preset deck"));
  if(recipe_expected)assert(strstr(items[source_recipe],recipe_expected));
  return (int)source_recipe;
 }
 if(!strcmp(title,"Choose recipe slot")){assert(destination_recipe<(unsigned)count);return (int)destination_recipe;}
 if(!strcmp(title,"Recipe slot"))return cancel_overwrite?1:0;
 if(!strcmp(title,"Battle Center")){assert(count==5);return 4;}
 fprintf(stderr,"Unexpected menu %s count%d\n",title,count);assert(0);return -1;
}
static void reboot(void)
{
 for(unsigned i=0;i<MAX_FILES;i++)free(files[i].data);
 memset(files,0,sizeof(files));active=-1;generation=0;opened=false;cut_after=-1;operation=partial_bytes=0;
 assert(save_store_open(NULL));
}
static void restore_baseline(void){memcpy(cartridge,baseline,sizeof(cartridge));reboot();game_profile=1;assert(game_load());}
static void same_game(const Game *a,const Game *b)
{
 uint8_t ow_a[2048],ow_b[2048],gp_a[GP_SAVE_SIZE],gp_b[GP_SAVE_SIZE];
 assert(ow_save_size()<=sizeof(ow_a));
 assert(ow_save_encode(&a->ow,ow_a,sizeof(ow_a)) && ow_save_encode(&b->ow,ow_b,sizeof(ow_b)));
 assert(gp_save_encode(&a->cards,gp_a,sizeof(gp_a)) && gp_save_encode(&b->cards,gp_b,sizeof(gp_b)));
 assert(!memcmp(ow_a,ow_b,ow_save_size()) && !memcmp(gp_a,gp_b,sizeof(gp_a)));
 assert(!memcmp(a->name,b->name,16) && !memcmp(a->link_name,b->link_name,16));
 assert(a->animations_disabled==b->animations_disabled && a->text_speed==b->text_speed && a->duel_mode==b->duel_mode);
 assert(a->skip_delay_allowed==b->skip_delay_allowed);
 assert(a->play_seconds==b->play_seconds);
}
static void create_profile(unsigned profile,const char *name,unsigned starter_choice,unsigned extra_deck,unsigned selected,unsigned gift_card,unsigned gift_count)
{
 memset(&game,0,sizeof(game));game_profile=profile;
 game.ow.version=OW_SAVE_VERSION;game.ow.map=1;game.ow.x=14;game.ow.y=26;game.ow.direction=0;
 game.ow.overworld_selection=1;game.ow.rng=0x154201+profile;
 uint8_t seed[3]={(uint8_t)profile,29,77};gp_init(&game.cards,seed);
 snprintf(game.name,sizeof(game.name),"%s",name);assert(gp_name_encode(&game.cards,name,game.link_name));
 assert(gp_give_starter(&game.cards,starter_choice));
 GpDeck second=gp_preset_decks[extra_deck];
 for(unsigned i=0;i<60;i++)assert(gp_add_card(&game.cards,second.cards[i]));
 assert(gp_set_deck(&game.cards,selected,&second)==GP_DECK_OK);game.cards.selected_deck=(uint8_t)selected;
 for(unsigned i=0;i<gift_count;i++)assert(gp_add_card(&game.cards,(uint8_t)gift_card));
 game.cards.saved[4]=gp_preset_decks[extra_deck];snprintf(game.cards.saved[4].name,24,"%s RECIPE",name);
 game.animations_disabled=profile==2;game.text_speed=profile;game.duel_mode=profile-1;
 game.skip_delay_allowed=1;
 game.play_seconds=0x010203u+profile;
 game_rng_to_story();assert(game_save());
}
static void initial_diaries(void)
{
 memset(cartridge,255,sizeof(cartridge));reboot();
 create_profile(1,"MARK",0,7,1,228,3);
 create_profile(2,"AMY",1,9,2,227,4);
 uint8_t duel[1660]={0},settings[32]={19};
 assert(save_store_write(SAVE_STORE_DUEL,duel,sizeof(duel)));
 assert(save_store_write("settings",settings,sizeof(settings)));
 memcpy(baseline,cartridge,sizeof(baseline));
}
static void canonical_and_rng(void)
{
 restore_baseline();Game first=game,second;assert(game_read_profile(2,&second));
 assert(first.cards.selected_deck==1 && second.cards.selected_deck==2);
 assert(!memcmp(first.cards.built[1].cards,gp_preset_decks[7].cards,60));
 assert(!memcmp(second.cards.built[2].cards,gp_preset_decks[9].cards,60));
 const uint8_t mark[]={3,0x3c,3,0x30,3,0x41,3,0x3a};assert(!memcmp(first.link_name,mark,8));
 assert(first.link_name[14] || first.link_name[15]);
 for(unsigned i=0;i<3;i++)assert(first.cards.rng[i]==(uint8_t)(first.ow.rng>>(8*i)));
 reboot();assert(game_read_profile(1,&game));same_game(&first,&game);assert(game_read_profile(2,&game));same_game(&second,&game);
}
static void card_pop_persistence(void)
{
 restore_baseline();Game first=game,second;assert(game_read_profile(2,&second));unsigned before_details=details;
 assert(game_card_pop());assert(details==before_details+2);
 assert(game.cards.card_pop_count==1 && !memcmp(game.link_name,first.link_name,16));
 assert(!memcmp(game.cards.card_pop_names[0],second.link_name,16));
 reboot();Game a,b;assert(game_read_profile(1,&a) && game_read_profile(2,&b));
 assert(a.cards.card_pop_count==1 && b.cards.card_pop_count==1);
 assert(gp_total_cards(&a.cards)==gp_total_cards(&first.cards)+1);
 assert(gp_total_cards(&b.cards)==gp_total_cards(&second.cards)+1);
 assert(!memcmp(b.cards.card_pop_names[0],first.link_name,16));
 assert(!memcmp(a.link_name,first.link_name,16) && !memcmp(b.link_name,second.link_name,16));
 for(unsigned i=0;i<3;i++)assert(b.cards.rng[i]==(uint8_t)(b.ow.rng>>(8*i)));
 game=a;before_details=details;assert(game_card_pop());assert(before_details==details);
 same_game(&game,&a);assert(game_read_profile(2,&game));same_game(&game,&b);
}
static void interrupted_pair_commit(void)
{
 for(int cut=0;cut<3;cut++)for(unsigned partial=0;partial<2;partial++) {
  restore_baseline();Game old_a=game,old_b;assert(game_read_profile(2,&old_b));
  operation=0;cut_after=cut;partial_bytes=partial ? (cut==1?8000:16):0;
  assert(game_card_pop());same_game(&game,&old_a);
  Game current_b;assert(game_read_profile(2,&current_b));same_game(&current_b,&old_b);
  reboot();assert(game_read_profile(1,&game));same_game(&game,&old_a);assert(game_read_profile(2,&game));same_game(&game,&old_b);
 }
}
static void commit_header_prefixes(void)
{
 restore_baseline();Game old_a=game,old_b,new_a,new_b;
 assert(game_read_profile(2,&old_b));assert(game_card_pop());
 assert(game_read_profile(1,&new_a) && game_read_profile(2,&new_b));
 /* A partially written final header can be complete enough to validate.
    Whichever state wins must contain BOTH whole diaries, never a mixture. */
 for(int prefix=0;prefix<=32;prefix++) {
  restore_baseline();operation=0;cut_after=2;partial_bytes=prefix;assert(game_card_pop());reboot();
  Game a,b;assert(game_read_profile(1,&a) && game_read_profile(2,&b));
  assert(a.cards.card_pop_count==b.cards.card_pop_count);
  if(a.cards.card_pop_count){same_game(&a,&new_a);same_game(&b,&new_b);}
  else{same_game(&a,&old_a);same_game(&b,&old_b);}
 }
}
static void gift_directions_and_rollback(void)
{
 restore_baseline();Game before_a=game,before_b;assert(game_read_profile(2,&before_b));
 card_to_send=228;quantity=2;assert(game_link(0));assert(!quantity);
 reboot();Game a,b;assert(game_read_profile(1,&a) && game_read_profile(2,&b));
 assert(gp_card_count(&a.cards,228)+2==gp_card_count(&before_a.cards,228));
 assert(gp_card_count(&b.cards,228)==gp_card_count(&before_b.cards,228)+2);
 game=a;card_to_send=227;quantity=1;assert(game_link(1));assert(!quantity);
 reboot();assert(game_read_profile(1,&a) && game_read_profile(2,&b));
 assert(gp_card_count(&a.cards,227)==gp_card_count(&before_a.cards,227)+1);
 assert(gp_card_count(&b.cards,227)+1==gp_card_count(&before_b.cards,227));
 /* A stopped paired write cannot persist just the sender's decrement. */
 for(int cut=0;cut<3;cut++) {
  restore_baseline();Game old_a=game,old_b;assert(game_read_profile(2,&old_b));
  card_to_send=228;quantity=1;operation=0;cut_after=cut;partial_bytes=cut==1?8000:0;
  assert(game_link(0));same_game(&game,&old_a);reboot();
  assert(game_read_profile(1,&game));same_game(&game,&old_a);assert(game_read_profile(2,&game));same_game(&game,&old_b);
 }
}
static void recipe_directions(void)
{
 restore_baseline();Game before_a=game,before_b;assert(game_read_profile(2,&before_b));
 source_recipe=4;destination_recipe=59;recipe_expected="MARK RECIPE";cancel_overwrite=false;
 assert(game_link(2));reboot();Game a,b;assert(game_read_profile(1,&a) && game_read_profile(2,&b));
 assert(!memcmp(&b.cards.saved[59],&before_a.cards.saved[4],sizeof(GpDeck)));
 assert(!memcmp(&a.cards.saved[4],&before_a.cards.saved[4],sizeof(GpDeck)));
 assert(gp_total_cards(&a.cards)==gp_total_cards(&before_a.cards) && gp_total_cards(&b.cards)==gp_total_cards(&before_b.cards));
 game=a;destination_recipe=58;recipe_expected="AMY RECIPE";assert(game_link(3));reboot();
 assert(game_read_profile(1,&a) && game_read_profile(2,&b));
 assert(!memcmp(&a.cards.saved[58],&before_b.cards.saved[4],sizeof(GpDeck)));
 assert(!memcmp(&b.cards.saved[4],&before_b.cards.saved[4],sizeof(GpDeck)));
 game=a;destination_recipe=59;recipe_expected="MARK RECIPE";cancel_overwrite=true;
 uint32_t old_generation=generation;assert(game_link(2));assert(generation==old_generation);
}
static void reverse_profile_and_guards(void)
{
 restore_baseline();game_profile=2;assert(game_load());
 Game before_b=game,before_a;assert(game_read_profile(1,&before_a));
 card_to_send=227;quantity=2;assert(game_link(0));reboot();Game a,b;
 assert(game_read_profile(1,&a) && game_read_profile(2,&b));
 assert(gp_card_count(&a.cards,227)==gp_card_count(&before_a.cards,227)+2);
 assert(gp_card_count(&b.cards,227)+2==gp_card_count(&before_b.cards,227));
 restore_baseline();game.ow.duel_pending=1;game.ow.pending_duel=(OwDuel){1,4,3,0};
 Game pending=game;unsigned old_details=details;
 assert(game_card_pop());same_game(&game,&pending);assert(details==old_details && strstr(last_message,"Finish the saved duel"));
 card_to_send=228;quantity=1;assert(game_link(0));same_game(&game,&pending);assert(quantity==1);
 restore_baseline();Game peer;assert(game_read_profile(2,&peer));
 peer.ow.duel_pending=1;peer.ow.pending_duel=(OwDuel){1,4,3,0};assert(game_save_peer(&peer));
 Game own_before=game;old_details=details;assert(game_card_pop());same_game(&game,&own_before);assert(details==old_details);
 assert(strstr(last_message,"Finish the saved duel"));
}
static void selected_deck_battle(void)
{
 restore_baseline();Game peer;assert(game_read_profile(2,&peer));
 unsigned before=duels;assert(game_link(4));assert(duels==before+1);
 assert(captured_duel.prizes==6 && captured_duel.duelist[0].kind==TCG_HUMAN && captured_duel.duelist[1].kind==TCG_HUMAN);
 assert(!memcmp(captured_duel.duelist[0].cards,game.cards.built[1].cards,60));
 assert(!memcmp(captured_duel.duelist[1].cards,peer.cards.built[2].cards,60));
 assert(!strcmp(captured_names[0],"MARK") && !strcmp(captured_names[1],"AMY"));
 uint8_t duel[1660],zero[1660]={0};assert(save_store_read(SAVE_STORE_DUEL,duel,sizeof(duel))==1660 && !memcmp(duel,zero,sizeof(duel)));
 reboot();Game completed_a,completed_b;assert(game_read_profile(1,&completed_a) && game_read_profile(2,&completed_b));
 assert(completed_a.play_seconds==0x010204u+12 && completed_b.play_seconds==0x010205u+12);
 for(unsigned i=0;i<3;i++){const uint8_t final_rng[]={37,51,73};assert(completed_a.cards.rng[i]==final_rng[i] && completed_b.cards.rng[i]==final_rng[i]);}
 assert(completed_a.ow.rng==0x493325 && completed_b.ow.rng==completed_a.ow.rng);
 game=completed_a;
 Game before_game=game;unsigned before_messages=messages;before=duels;second_controller=false;
 assert(game_link(4));assert(duels==before && messages>before_messages);same_game(&game,&before_game);
 assert(strstr(last_message,"second controller"));
 second_controller=true;
}
int main(void)
{
 initial_diaries();canonical_and_rng();card_pop_persistence();interrupted_pair_commit();commit_header_prefixes();gift_directions_and_rollback();recipe_directions();reverse_profile_and_guards();selected_deck_battle();
 puts("PROFILES PASS: 2 canonical diaries+duel/settings,salted names,RNG+playtime,CardPop/Gift both persistence,directions0/1/2/3,partialDMA+all33commitheaderprefixes atomic,selecteddecks1/2,controller2+pendingduelguards");
}
