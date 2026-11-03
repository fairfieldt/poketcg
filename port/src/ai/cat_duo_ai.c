/* A separate profile for the port-only cat encounter; original decks stay exact. */
#include "cat_duo_ai.h"

static bool is_meowth(u8 id) { return id==MEOWTH_LV14 || id==MEOWTH_LV15; }
static bool opener_has(u8 wanted)
{
    const u8 *deck=TurnDuelistVars()+DUELVARS_DECK_CARDS;
    for (unsigned i=0;i<STARTING_HAND_SIZE;i++) {
        u8 id=GetCardIDFromDeckIndex(deck[i]);
        if (id==wanted || (wanted==MEOWTH_LV15 && is_meowth(id))) return true;
    }
    return false;
}

u8 CatDuoStartDuel(void)
{
    InitAIDuelVars();
    const u8 *hand=TurnDuelistVars()+DUELVARS_HAND;
    for (unsigned i=0;i<STARTING_HAND_SIZE;i++) {
        u8 card=*hand;
        RemoveCardFromHand(card);
        ReturnCardToDeck(card);
    }
    /* Conditional random hands. Bound retries; a deterministic fallback also
       makes malformed test RNGs safe, without adding or duplicating cards. */
    unsigned tries=0;
    do { ShuffleDeck(); }
    while ((!opener_has(MEOWTH_LV15) || !opener_has(ELECTABUZZ_LV35) ||
            !opener_has(LIGHTNING_ENERGY)) && ++tries<256);
    if (tries==256) {
        static const u8 required[]={ELECTABUZZ_LV35,MEOWTH_LV15,LIGHTNING_ENERGY};
        u8 *deck=TurnDuelistVars()+DUELVARS_DECK_CARDS;
        for (unsigned i=0;i<3;i++) {
            unsigned j=i;
            for (;j<DECK_SIZE;j++) {
                u8 id=GetCardIDFromDeckIndex(deck[j]);
                if (id==required[i] || (required[i]==MEOWTH_LV15 && is_meowth(id))) break;
            }
            if (j==DECK_SIZE) tcg_fatal("Cat opener: required card missing");
            u8 swap=deck[i];deck[i]=deck[j];deck[j]=swap;
        }
    }
    for (unsigned i=0;i<STARTING_HAND_SIZE;i++) {
        u8 card;
        if (DrawCardFromDeck(&card)) tcg_fatal("Cat opener: empty deck");
        AddCardToHand(card);
    }
    u8 card;
    if (!LookForCardIDInHand(ELECTABUZZ_LV35,&card)) PutHandPokemonCardInPlayArea(card,NULL);
    if (!LookForCardIDInHand(MEOWTH_LV15,&card) || !LookForCardIDInHand(MEOWTH_LV14,&card))
        PutHandPokemonCardInPlayArea(card,NULL);
    AIPlayInitialBasicCards();
    return 0;
}

static unsigned hand_count(u8 id)
{
    unsigned n=0;
    const u8 *vars=TurnDuelistVars();
    for (unsigned i=0;i<vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND];i++)
        n+=GetCardIDFromDeckIndex(vars[DUELVARS_HAND+i])==id;
    return n;
}
static bool in_play(u8 id)
{
    const u8 *vars=TurnDuelistVars();
    for (unsigned i=0;i<vars[DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA];i++)
        if (GetCardIDFromDeckIndex(vars[DUELVARS_ARENA_CARD+i])==id) return true;
    return false;
}
static unsigned active_energy(u8 id)
{
    const u8 *vars=TurnDuelistVars();
    unsigned n=0;
    for (unsigned i=0;i<DECK_SIZE;i++)
        n+=vars[DUELVARS_CARD_LOCATIONS+i]==CARD_LOCATION_ARENA &&
            GetCardIDFromDeckIndex((u8)i)==id;
    return n;
}
/* Higher score means more useful now. Keep this modest and inspectable. */
static unsigned target_score(u8 id)
{
    if (hand_count(id)) return 0;
    unsigned hand=GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_IN_HAND);
    u8 active=GetCardIDFromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
    switch(id) {
    case LIGHTNING_ENERGY:
        if (active==ELECTABUZZ_LV35 && !active_energy(id)) return 125;
        return hand_count(DOUBLE_COLORLESS_ENERGY)?65:100;
    case PSYCHIC_ENERGY:
        if ((active==MEW_LV8 || active==MEW_LV23) && !active_energy(id)) return 125;
        return in_play(MEW_LV8)||in_play(MEW_LV23)||hand_count(MEW_LV8)||hand_count(MEW_LV23)?90:0;
    case DOUBLE_COLORLESS_ENERGY: return 75;
    case PERSIAN: return in_play(MEOWTH_LV15)||in_play(MEOWTH_LV14)?95:0;
    case PROFESSOR_OAK: return hand<=4?110:35;
    case BILL: return 60;
    case ELECTABUZZ_LV35: return in_play(id)?0:80;
    case SCYTHER: return in_play(id)?0:55;
    case MEW_LV8: case MEW_LV23:
        return in_play(MEW_LV8)||in_play(MEW_LV23)?0:55;
    case ENERGY_REMOVAL: return 50;
    case GUST_OF_WIND: case PLUSPOWER: return 30;
    case SWITCH: case SCOOP_UP: return 20;
    default: return 0;
    }
}

bool CatDuoSearch(bool item_finder,u8 *target)
{
    u8 *vars=TurnDuelistVars();
    *target=0xff;
    if (vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND]<3) return false;
    unsigned best=0;
    for (unsigned i=0;i<DECK_SIZE;i++) {
        if (vars[DUELVARS_CARD_LOCATIONS+i] != (item_finder?CARD_LOCATION_DISCARD_PILE:CARD_LOCATION_DECK)) continue;
        u8 id=GetCardIDFromDeckIndex((u8)i);
        if (item_finder) {
            LoadCardDataToBuffer1_FromDeckIndex((u8)i);
            if (wLoadedCard1Type != TYPE_TRAINER) continue;
        }
        unsigned score=target_score(id);
        if (score>best) {best=score;*target=(u8)i;}
    }
    if (!best) return false;
    /* Discard spare copies first; protect the last Energy, draw card and
       live Persian evolution. Never discard the Trainer being resolved. */
    u8 discards[2]={0xff,0xff};unsigned costs[2]={999,999};
    for (unsigned i=0;i<vars[DUELVARS_NUMBER_OF_CARDS_IN_HAND];i++) {
        u8 card=vars[DUELVARS_HAND+i];
        if (card==wAITrainerCardToPlay) continue;
        u8 id=GetCardIDFromDeckIndex(card);
        unsigned cost=25;
        if (id==LIGHTNING_ENERGY || id==PSYCHIC_ENERGY || id==DOUBLE_COLORLESS_ENERGY) cost=hand_count(id)>2?15:120;
        else if (id==PROFESSOR_OAK || id==BILL) cost=hand_count(id)>1?15:110;
        else if (id==PERSIAN) cost=(in_play(MEOWTH_LV15)||in_play(MEOWTH_LV14))?115:20;
        else if (id==ELECTABUZZ_LV35 || is_meowth(id)) cost=hand_count(id)>1||in_play(id)?20:90;
        else if (hand_count(id)>1) cost=10;
        if (cost<costs[0]) {costs[1]=costs[0];discards[1]=discards[0];costs[0]=cost;discards[0]=card;}
        else if (cost<costs[1]) {costs[1]=cost;discards[1]=card;}
    }
    if (discards[1]==0xff || costs[1]>=best) return false;
    wce06=*target;wce1a=discards[0];wce1b=discards[1];
    return true;
}
