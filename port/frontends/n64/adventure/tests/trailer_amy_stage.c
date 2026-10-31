/* Private capture staging: play ordinary source decks until a natural attack
 * knocks out Amy's only remaining Pokemon and actually wins the duel. */
#include "engine.h"
#include "tcg.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {TcgRequest request;int answer;uint32_t hash;} Step;
static Step segment[512];
static uint8_t latest[TCG_SAVE_SIZE],checkpoint[TCG_SAVE_SIZE];
static TcgDuelSetup setup;
static jmp_buf escape;
static unsigned seed,policy_state,requests,count,step,mode,damage_before,hp_before,final_hash;
static bool have_save,last_ko,won;
static FILE *transcript;
static unsigned stats[8];
void tcg_fatal(const char *message)
{fprintf(stderr,"AMY_STAGE seed%u: %s\n",seed,message);longjmp(escape,3);}
static unsigned random_choice(unsigned n)
{policy_state=policy_state*1664525u+1013904223u;return n?(policy_state>>8)%n:0;}
static void save(void *ctx,const uint8_t *data,uint32_t size)
{(void)ctx;if(size==TCG_SAVE_SIZE){memcpy(latest,data,size);have_save=true;}}
static void event(void *ctx,const TcgEvent *e)
{
 (void)ctx;
 if(mode&&transcript&&e->type==TCG_EV_TEXT&&e->str)fprintf(transcript,"TEXT %s\n",e->str);
 if(e->type==TCG_EV_DUEL_END){stats[0]++;if(wDuelResult==DUEL_WIN)stats[1]++;}
 if(e->type==TCG_EV_DAMAGE&&e->arg[0]==TCG_OPPONENT&&e->arg[1]==0&&hWhoseTurn==TCG_PLAYER&&
    count&&segment[0].answer==1&&tcg_duelvars(TCG_OPPONENT)[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA]==1){
  unsigned amount=e->arg[2]|e->arg[3]<<8,hp=tcg_duelvars(TCG_OPPONENT)[DUELVARS_ARENA_CARD_HP];
  if(hp&&amount>=hp){stats[2]++;last_ko=true;damage_before=amount;hp_before=hp;
   if(mode)fprintf(transcript,"LAST_POKEMON_KO damage=%u hp=%u card=%u in_play=1\n",amount,hp,
    tcg_card_id(TCG_OPPONENT,tcg_duelvars(TCG_OPPONENT)[DUELVARS_ARENA_CARD]));}
 }
 if(e->type==TCG_EV_DUEL_END&&last_ko&&wDuelResult==DUEL_WIN){won=true;
  if(mode)fprintf(transcript,"REAL_DUEL_WIN finished=%u turn=%u text=%u\n",e->arg[0],e->arg[1],e->text);}
}
static int policy(const TcgRequest *r)
{
 if(r->type==TCG_REQ_ACK)return 0;
 if(r->type==TCG_REQ_DUEL_MENU){
  static const unsigned weights[]={48,34,0,3,3,12};unsigned t=random_choice(100);
  for(unsigned i=0;i<6;i++){if(t<weights[i])return (int)i;t-=weights[i];}return 5;
 }
 if(r->type==TCG_REQ_CARD_LIST)return r->arg[0]||!r->num_items||!random_choice(10)?TCG_CANCEL:(int)random_choice(r->num_items);
 if(r->type==TCG_REQ_CARD_ITEM_MENU)return 0;
 if(r->type==TCG_REQ_YES_NO)return random_choice(4)!=0;
 if(r->type==TCG_REQ_CONFIRM)return 0;
 if(r->type==TCG_REQ_PRIZE){for(unsigned i=0;i<6;i++)if(r->arg[0]&(1u<<i))return (int)i;return 0;}
 if(r->type==TCG_REQ_COLOR)return r->items&&r->num_items?r->items[random_choice(r->num_items)]:(int)random_choice(6);
 if(r->type==TCG_REQ_PLAY_AREA)return r->arg[1]?TCG_CANCEL:(!random_choice(5)?r->arg[0]+(int)random_choice(r->num_items):r->cursor);
 if(r->type==TCG_REQ_MENU&&(r->menu==TCG_MENU_TWO_ITEMS||r->menu==TCG_MENU_PEEK_AREA))return (int)random_choice(2);
 if(r->type==TCG_REQ_MENU&&r->menu==TCG_MENU_PEEK_TARGET)return 7;
 return !r->num_items||!random_choice(12)?TCG_CANCEL:(int)random_choice(r->num_items);
}
static int request(void *ctx,const TcgRequest *r)
{
 (void)ctx;if(++requests>50000)longjmp(escape,1);
 if(!mode&&r->type==TCG_REQ_DUEL_MENU&&r->duelist==TCG_PLAYER){
  count=0;last_ko=won=false;if(have_save)memcpy(checkpoint,latest,sizeof(checkpoint));
 }
 if(mode){
  if(step>=count)tcg_fatal("Replay exceeded legal request transcript");
  const Step *s=&segment[step];
  if(s->request.type!=r->type||s->request.menu!=r->menu||s->request.duelist!=r->duelist||
     s->request.shown!=r->shown||s->hash!=tcg_state_hash())tcg_fatal("Replay differs from original legal duel");
 }
 int answer=mode?segment[step++].answer:policy(r);
 if(!mode){if(count>=512)longjmp(escape,1);segment[count++]=(Step){*r,answer,tcg_state_hash()};}
 else fprintf(transcript,"REQUEST step=%u type=%u menu=%u owner=%u shown=%u cursor=%u count=%u answer=%d hash=%u\n",
  step,r->type,r->menu,r->duelist,r->shown,r->cursor,r->num_items,answer,tcg_state_hash());
 return answer;
}
static bool simple_finish(void)
{
 if(!have_save||!last_ko||!won||count<3||count>16||segment[0].request.type!=TCG_REQ_DUEL_MENU||segment[0].answer!=1)return false;
 for(unsigned i=1;i<count;i++){
  const Step *s=&segment[i];
  if(s->request.type==TCG_REQ_ACK)continue;
  if(i==1&&s->request.type==TCG_REQ_MENU&&s->request.menu==TCG_MENU_ATTACKS&&s->answer>=0&&s->answer<2)continue;
  if(s->request.type==TCG_REQ_PRIZE&&s->answer>=0)continue;
  return false;
 }
 return true;
}
int main(int argc,char **argv)
{
 if(argc!=2){fprintf(stderr,"usage: trailer_amy_stage OUTPUT_DIR\n");return 2;}
 static const TcgHost host={event,request,save,NULL};tcg_set_host(&host);
 setup=(TcgDuelSetup){.duelist={{.kind=TCG_HUMAN,.deck_id=PIKACHU_DECK_ID,.name="MARK"},
  {.kind=TCG_AI,.deck_id=GO_GO_RAIN_DANCE_DECK_ID,.name="AMY"}},.prizes=6,.music=MUSIC_DUEL_THEME_2};
 for(seed=1;seed<=20000;seed++){
  mode=0;policy_state=seed*7919u;requests=count=0;have_save=last_ko=won=false;
  setup.rng[0]=(uint8_t)seed;setup.rng[1]=(uint8_t)(seed>>8);setup.rng[2]=0x5a;
  int reason=setjmp(escape);if(!reason){int result=tcg_run_duel(&setup);if(result==DUEL_WIN&&simple_finish())break;}else stats[3+reason]++;
  if(last_ko)stats[3]++;
 }
 if(seed>20000){fprintf(stderr,"No legal final-Pokemon victory found stats=%u,%u,%u,%u,%u,%u,%u,%u\n",stats[0],stats[1],stats[2],stats[3],stats[4],stats[5],stats[6],stats[7]);return 1;}
 final_hash=tcg_state_hash();TcgSavedDuel info;
 if(tcg_saved_duel_info(checkpoint,sizeof(checkpoint),&info)||info.opponent_deck_id!=GO_GO_RAIN_DANCE_DECK_ID)return 1;
 char path[1024];snprintf(path,sizeof(path),"%s/amy.duel",argv[1]);FILE *out=fopen(path,"wb");if(!out)return 1;
 if(fwrite(checkpoint,1,sizeof(checkpoint),out)!=sizeof(checkpoint))return 1;fclose(out);
 snprintf(path,sizeof(path),"%s/transcript.txt",argv[1]);transcript=fopen(path,"w");if(!transcript)return 1;
 fprintf(transcript,"LEGAL_ENGINE_CHECKPOINT seed=%u player_deck=%u opponent_deck=%u music=%u turns=%u prizes=%u,%u bytes=%u requests=%u\n",
  seed,PIKACHU_DECK_ID,info.opponent_deck_id,setup.music,info.turns,info.prizes_left[0],info.prizes_left[1],TCG_SAVE_SIZE,count);
 mode=1;step=requests=0;last_ko=won=false;int result=-1,reason=setjmp(escape);
 if(!reason)result=tcg_continue_duel(&setup,checkpoint,sizeof(checkpoint));
 bool exact=!reason&&result==DUEL_WIN&&last_ko&&won&&step==count&&tcg_state_hash()==final_hash;
 fprintf(transcript,"CONTINUATION result=%d last_pokemon_ko=%u win=%u requests=%u/%u exact_final_state_rng=%u\n",result,last_ko,won,step,count,exact);
 fclose(transcript);if(!exact)return 1;
 snprintf(path,sizeof(path),"%s/answers.json",argv[1]);out=fopen(path,"w");if(!out)return 1;
 fprintf(out,"{\"seed\":%u,\"player_deck\":%u,\"opponent_deck\":%u,\"music\":%u,\"turns\":%u,\"damage\":%u,\"hp_before\":%u,\"only_one_opponent_pokemon\":true,\"real_win\":true,\"requests\":[",
  seed,PIKACHU_DECK_ID,info.opponent_deck_id,setup.music,info.turns,damage_before,hp_before);
 for(unsigned i=0;i<count;i++){const Step *s=&segment[i];fprintf(out,"%s{\"type\":%u,\"menu\":%u,\"owner\":%u,\"shown\":%u,\"cursor\":%u,\"count\":%u,\"answer\":%d,\"hash\":%u}",
  i?",":"",s->request.type,s->request.menu,s->request.duelist,s->request.shown,s->request.cursor,s->request.num_items,s->answer,s->hash);}
 fprintf(out,"]}\n");fclose(out);
 printf("AMY_STAGE_PASS seed=%u turn=%u requests=%u last_pokemon=1 real_win=1 exact_state_rng=1\n",seed,info.turns,count);return 0;
}
