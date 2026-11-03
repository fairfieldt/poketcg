#include "tcg.h"
#include "engine.h"
#include "cat_duo.h"
#include "ai/cat_duo_ai.h"
#include "../overworld/overworld.h"
#include "../overworld/gen/data.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jmp_buf stop;
static unsigned mode, turns, requests;
static uint8_t saved[TCG_SAVE_SIZE];
static uint32_t saved_size;
static void event(void *ctx,const TcgEvent *e) {
    (void)ctx;
    if (e->type==TCG_EV_TURN) {
        assert(++turns<500);
        if (mode==1) longjmp(stop,1);
    }
}
static int request(void *ctx,const TcgRequest *r) {
    (void)ctx;assert(++requests<100000);
    if (r->type==TCG_REQ_DUEL_MENU) { if(mode==2) longjmp(stop,1);return 5; }
    if (r->type==TCG_REQ_ACK) return 0;
    if (r->type==TCG_REQ_YES_NO) return 1;
    if (r->type==TCG_REQ_CARD_LIST) {
        if (r->arg[0]) return TCG_CANCEL;
        for (unsigned i=0;i<r->num_items;i++)
            if (tcg_card_id(r->duelist,r->items[i])==ELECTABUZZ_LV35) return i;
        return TCG_CANCEL;
    }
    if (r->type==TCG_REQ_CONFIRM) return TCG_CANCEL;
    return 0;
}
static void save(void *ctx,const uint8_t *data,uint32_t size) {
    (void)ctx;assert(size==sizeof(saved));memcpy(saved,data,size);saved_size=size;
}
static void opener(void) {
    assert(tcg_card_id(TCG_OPPONENT,wOpponentArenaCard)==ELECTABUZZ_LV35);
    bool cat=false,energy=false;
    for(unsigned i=1;i<wOpponentNumberOfPokemonInPlayArea;i++) {
        u8 id=tcg_card_id(TCG_OPPONENT,wOpponentDuelVariables[DUELVARS_ARENA_CARD+i]);
        cat |= id==MEOWTH_LV14||id==MEOWTH_LV15;
    }
    for(unsigned i=0;i<wOpponentNumberOfCardsInHand;i++)
        energy |= tcg_card_id(TCG_OPPONENT,wOpponentHand[i])==LIGHTNING_ENERGY;
    assert(cat&&energy);
    uint8_t expected[60];cat_duo_deck(expected);
    assert(!memcmp(expected,wOpponentDeck,60));
    assert(wOpponentDeckID==CAT_DUO_DECK_ID);
}
static unsigned duels,card_rewards,packs,answer=1,outcome=1,messages;
static OwState pending;
static int choice(void *ctx,OwState *s,unsigned k,uint16_t t,unsigned n,unsigned initial) {
    (void)ctx;(void)s;(void)initial;assert(k==OW_CHOICE_YES_NO&&t==CAT_DUO_CHALLENGE_TEXT&&n==2);return answer;
}
static int duel(void *ctx,OwState *s,const OwDuel *d) {
    (void)ctx;duels++;assert(d->deck==CAT_DUO_DECK_ID&&d->npc==CAT_TUXEDO_NPC&&d->prizes==6);
    assert(s->duel_pending);uint8_t bytes[2048];assert(ow_save_size()<=sizeof(bytes));
    assert(ow_save_encode(s,bytes,sizeof(bytes)));assert(ow_save_decode(&pending,bytes,ow_save_size()));
    return outcome;
}
static void text(void *ctx,OwState *s,uint16_t t,uint8_t npc) {
    (void)ctx;(void)s;assert(cat_duo_text(t)&&cat_duo_npc(npc));messages++;
}
static void card(void *ctx,unsigned id,int n) { (void)ctx;assert(id==FLYING_PIKACHU&&n==1);card_rewards++; }
static void booster(void *ctx,unsigned id) { (void)ctx;assert(id==(packs%2?BOOSTER_MYSTERY_LIGHTNING_COLORLESS:BOOSTER_COLOSSEUM_LIGHTNING));packs++; }
static bool feature(void *ctx,OwState *s,unsigned f,unsigned a) {
    (void)ctx;(void)s;(void)a;return f==OW_FEATURE_CARD_RECEIVED||f==OW_FEATURE_MAP_PATCH||f==OW_FEATURE_PC_PACK;
}
static unsigned cats(OwState *s) { unsigned n=0;for(unsigned i=0;i<s->npc_count;i++)n+=cat_duo_npc(s->npcs[i].id);return n; }
static void talk(OwState *s,OwHost *h,unsigned x) { s->x=x;s->y=10;s->direction=NORTH;assert(ow_interact(s,h));assert(!s->error[0]); }
static void story(void) {
    for(unsigned i=0;i<NUM_EVENT_FLAGS;i++)
        assert(ow_event_masks[2*i]!=CAT_DUO_REWARD_EVENT_BYTE);
    OwState s={.version=OW_SAVE_VERSION,.rng=123};
    OwHost h={.choice=choice,.duel=duel,.text=text,.card_change=card,.booster=booster,.feature=feature};
    ow_load_map(&s,&h,ISHIHARAS_HOUSE,8,20,NORTH);assert(cats(&s)==0);
    ow_set_event(&s,EVENT_MEDAL_FLAGS,1);ow_load_map(&s,&h,ISHIHARAS_HOUSE,8,20,NORTH);assert(cats(&s)==2);
    assert(!ow_maps[3].permissions[(8/2)*(ow_maps[3].width/2)+12/2]);
    assert(!ow_maps[3].permissions[(8/2)*(ow_maps[3].width/2)+14/2]);
    s.x=12;s.y=10;s.direction=NORTH;assert(!ow_move(&s,&h,NORTH));
    answer=0;talk(&s,&h,12);assert(!duels&&!card_rewards&&!packs);answer=1;
    outcome=0;talk(&s,&h,14);assert(duels==1&&!card_rewards&&!packs);
    outcome=2;talk(&s,&h,12);assert(duels==2&&!card_rewards&&!packs);
    outcome=1;talk(&s,&h,12);assert(duels==3&&card_rewards==1&&!packs);
    talk(&s,&h,14);assert(duels==4&&card_rewards==1&&packs==2);
    /* Resume a pending rematch from an encoded save; the shared reward flag
       survives and finishing through either NPC cannot award the promo twice. */
    ow_finish_duel(&pending,&h,1);assert(card_rewards==1&&packs==4&&!pending.duel_pending);
    ow_finish_duel(&pending,&h,1);assert(card_rewards==1&&packs==4);
    ow_continue(&s,&h);assert(s.events[CAT_DUO_REWARD_EVENT_BYTE]&CAT_DUO_REWARD_EVENT_MASK);
    assert(!s.error[0]);
    puts("Cat story PASS: both actors, unlock, collision, decline, loss/draw/win, shared rewards, save/resume");
}
static u8 fixture_card(u8 id) {
    for(unsigned i=0;i<60;i++)
        if(wOpponentDeck[i]==id && wOpponentCardLocations[i]==CARD_LOCATION_DECK) return (u8)i;
    assert(!"fixture card missing");return 0;
}
static u8 fixture_hand(u8 id) {
    u8 card=fixture_card(id);SearchCardInDeckAndAddToHand(card);AddCardToHand(card);return card;
}
static void search_logic(void) {
    memset(gWRAM,0,sizeof(gWRAM));memset(gHRAM,0,sizeof(gHRAM));
    hWhoseTurn=OPPONENT_TURN;wOpponentDeckID=CAT_DUO_DECK_ID;cat_duo_deck(wOpponentDeck);
    for(unsigned i=0;i<60;i++)wOpponentDeckCards[i]=(u8)i;
    u8 cat=fixture_hand(MEOWTH_LV15);PutHandPokemonCardInPlayArea(cat,NULL);
    wAITrainerCardToPlay=fixture_hand(COMPUTER_SEARCH);
    fixture_hand(PLUSPOWER);fixture_hand(PLUSPOWER);fixture_hand(BILL);
    fixture_hand(LIGHTNING_ENERGY);fixture_hand(SCYTHER);
    u8 target;assert(CatDuoSearch(false,&target));
    assert(wOpponentDeck[target]==PERSIAN);
    assert(wce1a!=wce1b && wce1a!=wAITrainerCardToPlay && wce1b!=wAITrainerCardToPlay);
    assert(wOpponentDeck[wce1a]==PLUSPOWER && wOpponentDeck[wce1b]==PLUSPOWER);
    assert(wOpponentCardLocations[target]==CARD_LOCATION_DECK);
    u8 removal=fixture_hand(ENERGY_REMOVAL);MoveHandCardToDiscardPile(removal);
    wAITrainerCardToPlay=fixture_hand(ITEM_FINDER);
    assert(CatDuoSearch(true,&target));assert(target==removal);
    assert(wOpponentCardLocations[target]==CARD_LOCATION_DISCARD_PILE);
    /* A Mew without Psychic Energy must be able to search for its attack cost. */
    u8 mew=fixture_hand(MEW_LV8);PutHandPokemonCardInPlayArea(mew,NULL);
    wOpponentArenaCard=mew;
    wOpponentDuelVariables[DUELVARS_ARENA_CARD+1]=cat;
    wOpponentCardLocations[cat]=CARD_LOCATION_PLAY_AREA|1;
    wOpponentCardLocations[mew]=CARD_LOCATION_ARENA;
    wAITrainerCardToPlay=fixture_hand(COMPUTER_SEARCH);
    assert(CatDuoSearch(false,&target));assert(wOpponentDeck[target]==PSYCHIC_ENERGY);
    wOpponentNumberOfCardsInHand=2;assert(!CatDuoSearch(false,&target));
    puts("Cat search PASS: live Persian evolution, safe distinct discards, Energy Removal recovery, Mew Psychic search, insufficient hand");
}
int main(void) {
    story();
    search_logic();
    uint8_t cards[60];cat_duo_deck(cards);unsigned counts[256]={0};
    for(unsigned i=0;i<60;i++)assert(cards[i]&&++counts[cards[i]]<=60);
    assert(counts[MEOWTH_LV14]+counts[MEOWTH_LV15]==4&&counts[PERSIAN]==3&&counts[LIGHTNING_ENERGY]==8&&counts[PSYCHIC_ENERGY]==4&&counts[MEW_LV8]==1&&counts[MEW_LV23]==1&&counts[KANGASKHAN]==1&&!counts[CHANSEY]&&!counts[FARFETCHD]);
    TcgHost host={event,request,save,NULL};tcg_set_host(&host);
    TcgDuelSetup setup={.prizes=6};setup.duelist[0].kind=TCG_AI;setup.duelist[0].deck_id=ROCK_CRUSHER_DECK_ID;
    setup.duelist[1].kind=TCG_AI;setup.duelist[1].deck_id=CAT_DUO_DECK_ID;
    unsigned variations[256]={0};mode=1;
    for(unsigned seed=0;seed<2000;seed++) {
        setup.rng[0]=seed;setup.rng[1]=seed>>8;setup.rng[2]=seed*71;turns=requests=0;
        if(!setjmp(stop))tcg_run_duel(&setup);
        opener();variations[tcg_card_id(TCG_OPPONENT,wOpponentHand[0])]++;
    }
    unsigned distinct=0;for(unsigned i=0;i<256;i++)distinct+=variations[i]!=0;assert(distinct>5);
    puts("Cat opener PASS: 2000 seeds, Electabuzz Active, Meowth Benched, Lightning in hand, varying remaining cards");
    mode=0;
    for(unsigned seed=1;seed<=24;seed++) {
        setup.rng[0]=seed;setup.rng[1]=seed*3;setup.rng[2]=seed*7;turns=requests=0;
        int result=tcg_run_duel(&setup);assert(result>=0&&result<=2);
    }
    puts("Cat battles PASS: 24 complete AI duels");
    setup.duelist[0].kind=TCG_HUMAN;
    for(unsigned i=0;i<60;i++)setup.duelist[0].cards[i]=i<4?ELECTABUZZ_LV35:LIGHTNING_ENERGY;
    mode=1;turns=requests=0;
    if(!setjmp(stop))tcg_run_duel(&setup);
    opener();
    mode=2;turns=requests=0;saved_size=0;
    if(!setjmp(stop))tcg_run_duel(&setup);
    assert(saved_size==TCG_SAVE_SIZE);
    TcgSavedDuel info;assert(!tcg_saved_duel_info(saved,saved_size,&info));assert(info.opponent_deck_id==CAT_DUO_DECK_ID);
    uint8_t deck[60];memcpy(deck,wOpponentDeck,60);turns=requests=0;
    if(!setjmp(stop))tcg_continue_duel(&setup,saved,saved_size);
    assert(wOpponentDeckID==CAT_DUO_DECK_ID&&!memcmp(deck,wOpponentDeck,60));
    puts("Cat duel save PASS: player-vs-AI setup and interrupted duel resume retain custom deck/profile");
    return 0;
}
