/* Private host staging only: find a naturally reached legal Ronald knockout,
 * retain the engine's own SRAM image, then verify its real continuation. */
#include "engine.h"
#include "tcg.h"
#include "gen/decks.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
static jmp_buf escape;
static uint8_t latest[TCG_SAVE_SIZE],checkpoint[TCG_SAVE_SIZE];
static TcgDuelSetup setup;
static unsigned policy_state,seed,requests,mode,step,segment_count;
static bool have_save,ko,prize,captured;
static FILE *logfile;
typedef struct {TcgRequest request;int answer;uint32_t hash;} Step;
static Step segment[128],verified[128];
static unsigned verified_count;
void tcg_fatal(const char *message){fprintf(stderr,"STAGE fatal seed%u: %s\n",seed,message);longjmp(escape,3);}
static unsigned random_choice(unsigned n){policy_state=policy_state*1664525u+1013904223u;return n?(policy_state>>8)%n:0;}
static void save(void *ctx,const uint8_t *data,uint32_t size)
{(void)ctx;if(size==TCG_SAVE_SIZE){memcpy(latest,data,size);have_save=true;}}
static void event(void *ctx,const TcgEvent *e)
{
 (void)ctx;
 if(mode==1&&logfile&&e->type==TCG_EV_TEXT&&e->str)fprintf(logfile,"TEXT %s\n",e->str);
 if(e->type==TCG_EV_DAMAGE&&e->arg[0]==TCG_OPPONENT&&e->arg[1]==0&&hWhoseTurn==TCG_PLAYER) {
  unsigned amount=e->arg[2]|e->arg[3]<<8,hp=tcg_duelvars(TCG_OPPONENT)[DUELVARS_ARENA_CARD_HP];
  if(amount>=hp&&hp&&segment_count&&segment[0].answer==1&&tcg_duelvars(TCG_PLAYER)[DUELVARS_PRIZES]&&tcg_duelvars(TCG_OPPONENT)[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]>1) {
   ko=true;if(mode==1)fprintf(logfile,"KO damage=%u previous_hp=%u opponent_card=%u\n",amount,hp,tcg_card_id(TCG_OPPONENT,tcg_duelvars(TCG_OPPONENT)[DUELVARS_ARENA_CARD]));
  }
 }
 if(prize&&e->type==TCG_EV_TURN&&e->arg[0]!=TCG_PLAYER){captured=true;longjmp(escape,2);}
}
static int policy(const TcgRequest *r)
{
 if(r->type==TCG_REQ_ACK)return 0;
 if(r->type==TCG_REQ_DUEL_MENU) {
  static const unsigned weights[]={48,34,0,3,3,12};unsigned t=random_choice(100);
  for(unsigned i=0;i<6;i++){if(t<weights[i])return i;t-=weights[i];}return 5;
 }
 if(r->type==TCG_REQ_CARD_LIST)return r->arg[0]||!r->num_items||!random_choice(10)?TCG_CANCEL:(int)random_choice(r->num_items);
 if(r->type==TCG_REQ_CARD_ITEM_MENU)return 0;
 if(r->type==TCG_REQ_YES_NO)return random_choice(4)!=0;
 if(r->type==TCG_REQ_CONFIRM)return 0;
 if(r->type==TCG_REQ_PRIZE){for(unsigned i=0;i<6;i++)if(r->arg[0]&(1u<<i))return i;return 0;}
 if(r->type==TCG_REQ_COLOR)return r->items&&r->num_items?r->items[random_choice(r->num_items)]:(int)random_choice(6);
 if(r->type==TCG_REQ_PLAY_AREA)return r->arg[1]?TCG_CANCEL:r->arg[0]+(int)random_choice(r->num_items);
 if(r->type==TCG_REQ_MENU&&(r->menu==TCG_MENU_TWO_ITEMS||r->menu==TCG_MENU_PEEK_AREA))return random_choice(2);
 if(r->type==TCG_REQ_MENU&&r->menu==TCG_MENU_PEEK_TARGET)return 7;
 return !r->num_items||!random_choice(12)?TCG_CANCEL:(int)random_choice(r->num_items);
}
static int request(void *ctx,const TcgRequest *r)
{
 (void)ctx;if(++requests>50000)longjmp(escape,1);
 if(mode==0&&r->type==TCG_REQ_DUEL_MENU&&r->duelist==TCG_PLAYER) {
  if(ko&&prize){captured=true;longjmp(escape,2);}
  segment_count=0;ko=prize=false;if(have_save)memcpy(checkpoint,latest,sizeof(checkpoint));
 }
 if(mode==1) {
  if(step>=segment_count)tcg_fatal("Continuation exceeded recorded transcript");
  const Step *expected=&segment[step];
  if(expected->request.type!=r->type||expected->request.menu!=r->menu||expected->request.duelist!=r->duelist||expected->request.shown!=r->shown||expected->hash!=tcg_state_hash())tcg_fatal("Continuation diverged from legal source duel");
 }
 int answer=mode==0?policy(r):segment[step++].answer;
 if(mode==0) {
  if(segment_count>=128)longjmp(escape,1);segment[segment_count++]=(Step){.request=*r,.answer=answer,.hash=tcg_state_hash()};
 } else {
  if(verified_count>=128)longjmp(escape,1);verified[verified_count++]=(Step){.request=*r,.answer=answer,.hash=tcg_state_hash()};
  fprintf(logfile,"REQUEST step=%u type=%u menu=%u owner=%u shown=%u cursor=%u count=%u answer=%d arg=%u,%u,%u,%u\n",verified_count,r->type,r->menu,r->duelist,r->shown,r->cursor,r->num_items,answer,r->arg[0],r->arg[1],r->arg[2],r->arg[3]);
 }
 if(r->type==TCG_REQ_PRIZE&&r->duelist==TCG_PLAYER&&ko&&have_save)prize=true;
 return answer;
}
int main(int argc,char **argv)
{
 if(argc!=2){fprintf(stderr,"usage: trailer_ronald_stage OUTPUT_DIR\n");return 2;}
 static const TcgHost host={event,request,save,NULL};tcg_set_host(&host);
 setup=(TcgDuelSetup){.duelist={{.kind=TCG_HUMAN,.deck_id=SQUIRTLE_AND_FRIENDS_DECK_ID,.name="MARK"},{.kind=TCG_AI,.deck_id=LEGENDARY_RONALD_DECK_ID,.name="RONALD"}},.prizes=6,.music=MUSIC_DUEL_THEME_3};
 for(seed=1;seed<=2000;seed++) {
  mode=0;policy_state=seed*7919u;requests=segment_count=0;have_save=ko=prize=captured=false;
  setup.rng[0]=(uint8_t)seed;setup.rng[1]=(uint8_t)(seed>>8);setup.rng[2]=0x5a;
  int reason=setjmp(escape);if(!reason)tcg_run_duel(&setup);
  if(reason==2&&captured)break;
 }
 if(seed>2000){fprintf(stderr,"No staged knockout found\n");return 1;}
 TcgSavedDuel info;if(tcg_saved_duel_info(checkpoint,sizeof(checkpoint),&info)){fprintf(stderr,"Bad captured save\n");return 1;}
 char path[1024];snprintf(path,sizeof(path),"%s/ronald.duel",argv[1]);FILE *out=fopen(path,"wb");if(!out)return 1;fwrite(checkpoint,1,sizeof(checkpoint),out);fclose(out);
 snprintf(path,sizeof(path),"%s/transcript.txt",argv[1]);logfile=fopen(path,"w");if(!logfile)return 1;
 fprintf(logfile,"LEGAL_ENGINE_CHECKPOINT seed=%u player_deck=%u opponent_deck=%u music=%u turns=%u prizes=%u,%u bytes=%u original_segment_requests=%u\n",seed,SQUIRTLE_AND_FRIENDS_DECK_ID,info.opponent_deck_id,setup.music,info.turns,info.prizes_left[0],info.prizes_left[1],TCG_SAVE_SIZE,segment_count);
 mode=1;step=verified_count=requests=0;ko=prize=captured=false;
 int reason=setjmp(escape);if(!reason)tcg_continue_duel(&setup,checkpoint,sizeof(checkpoint));
 fprintf(logfile,"CONTINUATION reason=%d captured=%d ko=%d prize=%d steps=%u/%u\n",reason,captured,ko,prize,step,segment_count);fclose(logfile);
 if(reason!=2||!captured||!ko||!prize){fprintf(stderr,"Continuation did not verify\n");return 1;}
 snprintf(path,sizeof(path),"%s/answers.json",argv[1]);out=fopen(path,"w");if(!out)return 1;fprintf(out,"{\"seed\":%u,\"player_deck\":%u,\"opponent_deck\":%u,\"music\":%u,\"requests\":[",seed,SQUIRTLE_AND_FRIENDS_DECK_ID,info.opponent_deck_id,setup.music);
 for(unsigned i=0;i<verified_count;i++){Step *s=&verified[i];fprintf(out,"%s{\"type\":%u,\"menu\":%u,\"owner\":%u,\"shown\":%u,\"cursor\":%u,\"count\":%u,\"answer\":%d,\"hash\":%u}",i?",":"",s->request.type,s->request.menu,s->request.duelist,s->request.shown,s->request.cursor,s->request.num_items,s->answer,s->hash);}fprintf(out,"]}\n");fclose(out);
 printf("RONALD_STAGE_PASS seed=%u checkpoint=%u bytes turn=%u requests=%u actualKO=1 actualPrize=1\n",seed,TCG_SAVE_SIZE,info.turns,verified_count);return 0;
}
