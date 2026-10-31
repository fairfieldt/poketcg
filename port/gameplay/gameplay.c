/* Native portable-C counterparts of card_collection.asm, starter_deck.asm,
   booster_packs.asm, auto_deck_machines.asm and the Card Pop selection logic. */
#include "gameplay.h"
#include <string.h>
#include <stdlib.h>

static bool valid_card(unsigned card) { return card>0 && card<=GP_NUM_CARDS; }
static unsigned deck_count(const uint8_t cards[60], unsigned card)
{
    unsigned n=0;
    if (!cards[0]) return 0;
    for (unsigned i=0;i<60;i++) n+=cards[i]==card;
    return n;
}

void gp_init(GpState *s, const uint8_t seed[3])
{
    memset(s,0,sizeof(*s));
    memset(s->collection,GP_CARD_NOT_OWNED,sizeof(s->collection));
    static const unsigned starters[]={5,7,9}; /* DeckPointers indexes, not *_ID. */
    for(unsigned i=0;i<3;i++) s->saved[i]=gp_preset_decks[starters[i]];
    if(seed) memcpy(s->rng,seed,3);
    else s->rng[0]=1;
    gp_give_mail(s,1);
    s->challenge.record=1;
    memcpy(s->challenge.record_name,gp_challenge_initial_name,16);
}

static uint8_t rng_byte(GpState *s)
{
    /* UpdateRNGSources / Random: emulate the rotates' carry explicitly. */
    uint8_t e=s->rng[0],d=s->rng[1];
    uint8_t a=(uint8_t)((d<<2)|(d>>6));
    a^=e;
    unsigned carry=a&1;
    d^=e;
    e^=s->rng[2];
    unsigned next=e>>7;
    e=(uint8_t)((e<<1)|carry);
    d=(uint8_t)((d<<1)|next);
    s->rng[0]=e;s->rng[1]=d;s->rng[2]++;
    return d^e;
}
uint8_t gp_random(GpState *s, uint8_t max)
{ return (uint8_t)(((unsigned)rng_byte(s)*max)>>8); }

unsigned gp_collection_count(const GpState *s, uint8_t card)
{ return valid_card(card) ? s->collection[card]&0x7f : 0; }
unsigned gp_card_count(const GpState *s, uint8_t card)
{
    if(!valid_card(card)) return 0;
    unsigned n=gp_collection_count(s,card);
    for(unsigned i=0;i<4;i++) n+=deck_count(s->built[i].cards,card);
    return n;
}
unsigned gp_total_cards(const GpState *s)
{
    unsigned total=0;
    for(unsigned card=1;card<=GP_NUM_CARDS;card++) total+=gp_card_count(s,card);
    return total;
}
unsigned gp_album_count(const GpState *s, unsigned *total)
{
    unsigned n=0,denominator=GP_NUM_CARDS;
    /* Original album denominator omits the two hidden Card Pop cards until seen. */
    if(s->collection[10]&GP_CARD_NOT_OWNED) denominator--;
    if(s->collection[0xa1]&GP_CARD_NOT_OWNED) denominator--;
    for(unsigned i=1;i<=GP_NUM_CARDS;i++) n+=!(s->collection[i]&GP_CARD_NOT_OWNED);
    if(total) *total=denominator;
    return n;
}
bool gp_add_card(GpState *s, uint8_t card)
{
    if(!valid_card(card) || gp_card_count(s,card)>=99) return false;
    s->collection[card]=(uint8_t)(gp_collection_count(s,card)+1);
    return true;
}
bool gp_remove_card(GpState *s, uint8_t card)
{
    unsigned n=gp_collection_count(s,card);
    if(!n)return false;
    s->collection[card]=(uint8_t)(n-1);
    return true;
}

bool gp_give_starter(GpState *s, unsigned choice)
{
    if(choice>=3) return false;
    static const unsigned main_decks[]={5,7,9}, extras[]={6,8,10};
    /* Only the original one-time script may call this; do not duplicate cards
       by overwriting an already constructed first deck. */
    if(s->built[0].cards[0]) return false;
    s->built[0]=gp_preset_decks[main_decks[choice]];
    for(unsigned i=0;i<60;i++) s->collection[s->built[0].cards[i]]&=0x7f;
    for(unsigned i=0;i<30;i++) gp_add_card(s,gp_preset_decks[extras[choice]].cards[i]);
    s->selected_deck=0;
    return true;
}

static unsigned booster_type(uint8_t type)
{
    static const uint8_t types[]={1,0,3,2,4,5,6,7,8,8,8,8,8,8,8,7,7};
    return type<sizeof(types) ? types[type] : 1;
}
bool gp_open_booster(GpState *s, unsigned id, uint8_t cards[10])
{
    if(id>=GP_BOOSTERS || !cards) return false;
    GpBooster pack=gp_boosters[id];
    unsigned energy_count=0,drawn=0;
    uint8_t energy[10]={0};
    switch(pack.energy_mode) {
    case 0: if(pack.energy_card) energy[energy_count++]=pack.energy_card; break;
    case 1: energy[energy_count++]=(uint8_t)(1+gp_random(s,6)); break;
    case 2: case 3: case 4: {
        static const uint8_t pairs[][2]={{4,2},{3,5},{1,6}};
        for(unsigned i=0;i<10;i++) energy[energy_count++]=pairs[pack.energy_mode-2][i/5];
        memset(pack.rarity,0,3);break;
    }
    case 5:
        for(unsigned i=0;i<10;i++) energy[energy_count++]=(uint8_t)(1+gp_random(s,6));
        memset(pack.rarity,0,3);break;
    default: return false;
    }
    unsigned sum=0,nonzero=0;
    for(unsigned i=0;i<9;i++) if(pack.chances[i]){sum+=pack.chances[i];nonzero++;}
    unsigned average=nonzero ? sum/nonzero : 0;
    uint8_t seen[256]={0};
    for(unsigned i=0;i<energy_count;i++) seen[energy[i]]=1;
    for(int rarity=2;rarity>=0;rarity--) for(unsigned n=0;n<pack.rarity[rarity];n++) {
        uint8_t counts[9]={0},chances[9]={0};
        sum=0;
        for(unsigned card=1;card<=GP_NUM_CARDS;card++) {
            const GpCard *c=&gp_cards[card];
            unsigned type=booster_type(c->type);
            if(!seen[card] && c->rarity==rarity && (type==8 || c->set>>4==pack.set)) counts[type]++;
        }
        for(unsigned t=0;t<9;t++) if(counts[t] && pack.chances[t]) sum+=(chances[t]=pack.chances[t]);
        if(!sum || sum>255 || drawn>=10-energy_count) return false;
        unsigned roll=gp_random(s,(uint8_t)sum),type=0;
        while(type<8 && roll>=chances[type]) roll-=chances[type++];
        unsigned pick=gp_random(s,counts[type]);
        unsigned card;
        for(card=1;card<=GP_NUM_CARDS;card++) {
            const GpCard *c=&gp_cards[card];
            if(!seen[card] && c->rarity==rarity && booster_type(c->type)==type && (type==8 || c->set>>4==pack.set)) {
                if(!pick)break;
                pick--;
            }
        }
        if(card>GP_NUM_CARDS)return false;
        cards[drawn++]=(uint8_t)card;seen[card]=1;
        pack.chances[type]=(uint8_t)(pack.chances[type]>average ? pack.chances[type]-average : 1);
    }
    for(unsigned i=0;i<energy_count;i++)cards[drawn++]=energy[i];
    if(drawn!=10)return false;
    for(unsigned i=0;i<10;i++)gp_add_card(s,cards[i]);
    return true;
}

int gp_validate_deck(const uint8_t cards[60])
{
    unsigned basic=0;
    for(unsigned i=0;i<60;i++) {
        if(!cards[i])return GP_DECK_BAD_SIZE;
        if(!valid_card(cards[i]))return GP_DECK_BAD_CARD;
        const GpCard *c=&gp_cards[cards[i]];
        basic+=c->type<8 && c->stage==0;
        if(c->type>=8 && c->type<=13)continue; /* Unlimited basic energy only. */
        unsigned same=0;
        for(unsigned j=0;j<60;j++) if(valid_card(cards[j]) && gp_cards[cards[j]].name_text==c->name_text) same++;
        if(same>4)return GP_DECK_TOO_MANY_SAME_NAME;
    }
    return basic ? GP_DECK_OK : GP_DECK_NO_BASIC;
}

int gp_set_deck(GpState *s, unsigned slot, const GpDeck *deck)
{
    if(slot>=4 || !deck)return GP_DECK_BAD_CARD;
    int result=gp_validate_deck(deck->cards);
    if(result)return result;
    GpDeck copy=*deck;
    for(unsigned card=1;card<=GP_NUM_CARDS;card++)
        if(deck_count(copy.cards,card)>gp_collection_count(s,card)+deck_count(s->built[slot].cards,card))return GP_DECK_NOT_OWNED;
    for(unsigned i=0;i<60 && s->built[slot].cards[0];i++) {
        unsigned card=s->built[slot].cards[i];
        s->collection[card]=(uint8_t)(gp_collection_count(s,card)+1);
    }
    for(unsigned i=0;i<60;i++)gp_remove_card(s,copy.cards[i]);
    s->built[slot]=copy;
    return GP_DECK_OK;
}
int gp_dismantle_deck(GpState *s, unsigned slot)
{
    if(slot>=4)return GP_DECK_BAD_CARD;
    unsigned decks=0;
    for(unsigned i=0;i<4;i++)decks+=s->built[i].cards[0]!=0;
    if(!s->built[slot].cards[0])return GP_DECK_OK;
    if(decks<=1)return GP_DECK_LAST;
    for(unsigned i=0;i<60;i++) {
        unsigned card=s->built[slot].cards[i];
        s->collection[card]=(uint8_t)(gp_collection_count(s,card)+1);
    }
    memset(&s->built[slot],0,sizeof(s->built[slot]));
    if(s->selected_deck==slot) for(unsigned i=0;i<4;i++) if(s->built[i].cards[0]){s->selected_deck=(uint8_t)i;break;}
    return GP_DECK_OK;
}
int gp_save_recipe(GpState *s, unsigned slot, const GpDeck *deck)
{
    if(slot>=GP_SAVED_DECKS || !deck)return GP_DECK_BAD_CARD;
    int result=gp_validate_deck(deck->cards);
    if(result)return result;
    s->saved[slot]=*deck;return GP_DECK_OK;
}
static bool can_build(const GpState *s,const uint8_t cards[60],unsigned mask)
{
    for(unsigned card=1;card<=GP_NUM_CARDS;card++) {
        unsigned n=gp_collection_count(s,card);
        for(unsigned i=0;i<4;i++) if(mask&(1u<<i))n+=deck_count(s->built[i].cards,card);
        if(deck_count(cards,card)>n)return false;
    }
    return true;
}
int gp_find_dismantle_mask(const GpState *s, const uint8_t cards[60])
{
    static const uint8_t masks[]={0,1,2,4,8,3,5,9,6,10,12,7,11,13,14,15};
    for(unsigned i=0;i<sizeof(masks);i++)if(can_build(s,cards,masks[i]))return masks[i];
    return -1;
}
int gp_build_auto_deck(GpState *s, unsigned recipe, unsigned slot, unsigned mask)
{
    if(recipe>=50)return GP_DECK_BAD_CARD;
    GpDeck deck={0};deck.name_text=gp_auto_recipes[recipe].name_text;
    memcpy(deck.cards,gp_auto_recipes[recipe].cards,60);
    return gp_build_deck(s,&deck,slot,mask);
}
int gp_build_deck(GpState *s,const GpDeck *recipe,unsigned slot,unsigned mask)
{
    if(!recipe || slot>=4 || mask>15)return GP_DECK_BAD_CARD;
    mask|=1u<<slot;
    GpDeck deck=*recipe;
    int result=gp_validate_deck(deck.cards);
    if(result)return result;
    if(!can_build(s,deck.cards,mask))return GP_DECK_NOT_OWNED;
    for(unsigned i=0;i<4;i++)if(mask&(1u<<i)) {
        for(unsigned j=0;j<60 && s->built[i].cards[0];j++) {
            unsigned card=s->built[i].cards[j];
            s->collection[card]=(uint8_t)(gp_collection_count(s,card)+1);
        }
        memset(&s->built[i],0,sizeof(s->built[i]));
    }
    result=gp_set_deck(s,slot,&deck);
    if(result==GP_DECK_OK)s->selected_deck=(uint8_t)slot;
    return result;
}

bool gp_card_pop(GpState *s, const uint8_t own[16], const uint8_t peer[16], uint8_t *card)
{
    if(!own || !peer || !card)return false;
    for(unsigned i=0;i<16;i++)if(!memcmp(s->card_pop_names[i],peer,16))return false;
    uint8_t own_xor=0,own_sum=0,peer_xor=0,peer_sum=0;
    for(unsigned i=0;i<16;i++){own_xor^=own[i];own_sum+=own[i];peer_xor^=peer[i];peer_sum+=peer[i];}
    uint8_t high=(uint8_t)(own_xor-peer_xor),low=(uint8_t)(own_sum-peer_sum);
    s->rng[0]=high;s->rng[1]=low;s->rng[2]=0;
    if(low==5)*card=(high&1) ? 0xa1 : 10; /* Original parity bug preserves unobtainable Venusaur. */
    else {
        unsigned rarity=low<64 ? 2 : low<154 ? 1 : 0;
        uint8_t candidates[GP_NUM_CARDS];unsigned n=0;
        for(unsigned i=1;i<=GP_NUM_CARDS;i++) {
            const GpCard *c=&gp_cards[i];
            if(!(c->type&8) && c->rarity==rarity && (c->set&0xf0)!=0x40)candidates[n++]=(uint8_t)i;
        }
        if(!n)return false;
        for(unsigned i=0;i<n;i++) {
            unsigned j=gp_random(s,(uint8_t)n);
            uint8_t temp=candidates[i];candidates[i]=candidates[j];candidates[j]=temp;
        }
        *card=candidates[0];
    }
    gp_add_card(s,*card);
    memcpy(s->card_pop_names[s->card_pop_count++&15],peer,16);
    return true;
}

bool gp_name_encode(GpState *s,const char *name,uint8_t encoded[16])
{
    if(!s || !name || !encoded)return false;
    if(!name[0])name="MARK";
    uint8_t result[16]={0};unsigned count=0;
    for(const unsigned char *p=(const unsigned char *)name;*p;p++) {
        if(count>=6)return false;
        unsigned c=*p,high=3,low;
        if(c>='a' && c<='z')c-=32;
        if(c>='A' && c<='Z')low=0x30+c-'A';
        else if(c>='0' && c<='9'){high=0x0e;low=0x60+c-'0';}
        else switch(c) {
            case '!':high=0x0e;low=0x6e;break;
            case '?':high=0x0e;low=0x6f;break;
            case '+':high=0x0e;low=0x6a;break;
            case '-':high=0x0e;low=0x6b;break;
            case ' ':high=0x0e;low=0x70;break;
            case '&':low=0x5d;break;
            case '.':high=0x0e;low=0x77;break; /* N64 key '.' selects original middle dot. */
            default:return false;
        }
        result[count*2]=(uint8_t)high;result[count*2+1]=(uint8_t)low;count++;
    }
    result[14]=rng_byte(s);result[15]=rng_byte(s);
    memcpy(encoded,result,16);
    return true;
}

typedef struct {uint8_t collection[256],names[16][16],rng[3],count;} PopSnapshot;
static void pop_snapshot(PopSnapshot *out,const GpState *s)
{
    memcpy(out->collection,s->collection,256);memcpy(out->names,s->card_pop_names,256);
    memcpy(out->rng,s->rng,3);out->count=s->card_pop_count;
}
static void pop_restore(GpState *s,const PopSnapshot *in)
{
    memcpy(s->collection,in->collection,256);memcpy(s->card_pop_names,in->names,256);
    memcpy(s->rng,in->rng,3);s->card_pop_count=in->count;
}
bool gp_card_pop_pair(GpState *a,const uint8_t an[16],GpState *b,const uint8_t bn[16],uint8_t cards[2])
{
    if(!a || !b || a==b || !an || !bn || !cards)return false;
    for(unsigned i=0;i<16;i++)
        if(!memcmp(a->card_pop_names[i],bn,16) || !memcmp(b->card_pop_names[i],an,16))return false;
    PopSnapshot sa,sb;pop_snapshot(&sa,a);pop_snapshot(&sb,b);
    uint8_t result[2];
    if(!gp_card_pop(a,an,bn,&result[0]) || !gp_card_pop(b,bn,an,&result[1])) {
        pop_restore(a,&sa);pop_restore(b,&sb);return false;
    }
    cards[0]=result[0];cards[1]=result[1];return true;
}
int gp_transfer_cards(GpState *sender,GpState *receiver,const uint8_t *cards,size_t n,unsigned *received)
{
    if(!sender || !receiver || sender==receiver || !cards || !n || n>60)return GP_LINK_INVALID;
    uint8_t needed[256]={0};
    for(size_t i=0;i<n;i++) {
        if(!valid_card(cards[i]))return GP_LINK_INVALID;
        needed[cards[i]]++;
    }
    for(unsigned i=1;i<=GP_NUM_CARDS;i++)
        if(needed[i]>gp_collection_count(sender,(uint8_t)i))return GP_LINK_NOT_OWNED;
    unsigned added=0;
    for(size_t i=0;i<n;i++) {
        gp_remove_card(sender,cards[i]);
        added+=gp_add_card(receiver,cards[i]);
    }
    if(received)*received=added;
    return GP_LINK_OK;
}
int gp_transfer_recipe(const GpState *sender,unsigned from,GpState *receiver,unsigned to,bool overwrite)
{
    if(!sender || !receiver || sender==receiver || from>=60 || to>=60)return GP_LINK_INVALID;
    const GpDeck *d=&sender->saved[from];
    if(!d->cards[0] || gp_validate_deck(d->cards))return GP_LINK_INVALID;
    if(receiver->saved[to].cards[0] && !overwrite)return GP_LINK_OCCUPIED;
    receiver->saved[to]=*d;
    return GP_LINK_OK;
}

bool gp_give_mail(GpState *s, unsigned id)
{
    if(!id || id>15)return false;
    for(unsigned i=0;i<15;i++)if((s->pc_packs[i]&127)==id)return false;
    for(unsigned i=0;i<15;i++)if(!(s->pc_packs[i]&127)) {
        s->pc_packs[i]=(uint8_t)(id|128);return true;
    }
    return false;
}
unsigned gp_open_mail(GpState *s, unsigned slot, uint8_t cards[20])
{
    if(slot>=15 || !(s->pc_packs[slot]&128) || !cards)return 0;
    unsigned id=s->pc_packs[slot]&127;
    if(!id || id>15)return 0;
    if(!gp_open_booster(s,gp_mail_boosters[id][0],cards))return 0;
    unsigned count=10;
    if(gp_mail_boosters[id][1]) {
        if(!gp_open_booster(s,gp_mail_boosters[id][1],cards+10))return 0;
        count=20;
    }
    s->pc_packs[slot]&=127;
    return count;
}
void gp_challenge_begin(GpState *s)
{
    GpChallenge *c=&s->challenge;
    if(c->active)return;
    c->opponents[0]=gp_random(s,24);
    do c->opponents[1]=gp_random(s,24);while(c->opponents[1]==c->opponents[0]);
    do c->opponents[2]=gp_random(s,24);while(c->opponents[2]==c->opponents[0] || c->opponents[2]==c->opponents[1]);
    c->opponents[3]=(uint8_t)(24+gp_random(s,8));
    unsigned roll=rng_byte(s);
    c->opponents[4]=(uint8_t)(32+(roll<224 ? roll/56 : 4+(roll-224)/8));
    memset(c->results,0,5);
    c->round=0;c->new_record=0;c->active=1;c->in_duel=0;c->streak=c->streak_backup;
}
bool gp_challenge_opponent(const GpState *s, uint8_t *deck_id, uint8_t *prizes)
{
    const GpChallenge *c=&s->challenge;
    if(!c->active || c->round>=5 || c->opponents[c->round]>=40)return false;
    if(deck_id)*deck_id=gp_challenge_decks[c->opponents[c->round]];
    if(prizes)*prizes=c->round<3 ? 4:6;
    return true;
}
static void challenge_record(GpState *s,const uint8_t name[16])
{
    GpChallenge *c=&s->challenge;
    if(c->streak>c->record) {
        c->record=c->streak;c->new_record=1;
        if(name)memcpy(c->record_name,name,16);
    }
}
void gp_challenge_quit(GpState *s,const uint8_t name[16])
{
    GpChallenge *c=&s->challenge;
    challenge_record(s,name);
    c->streak=c->streak_backup=0;c->active=c->in_duel=0;
}
unsigned gp_challenge_result(GpState *s,bool won,const uint8_t name[16])
{
    GpChallenge *c=&s->challenge;
    if(!c->active || c->round>=5)return 0;
    c->in_duel=0;
    c->results[c->round]=(uint8_t)(won ? 1:2);
    if(!won){gp_challenge_quit(s,name);return 0;}
    if(c->streak<999)c->streak++;
    if(c->round<4){c->round++;return 1;}
    if(c->total_wins<999)c->total_wins++;
    challenge_record(s,name);
    c->streak_backup=c->streak;c->active=0;
    return 2;
}

static uint32_t save_crc(const uint8_t *p,size_t n)
{
    uint32_t crc=0xffffffffu;
    while(n--) {
        crc^=*p++;
        for(unsigned i=0;i<8;i++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    }
    return ~crc;
}
static void put16(uint8_t **p,unsigned x){*(*p)++=(uint8_t)x;*(*p)++=(uint8_t)(x>>8);}
static unsigned get16(const uint8_t **p){unsigned x=(*p)[0]|(*p)[1]<<8;*p+=2;return x;}
size_t gp_save_encode(const GpState *s,uint8_t *out,size_t cap)
{
    if(!s || !out || cap<GP_SAVE_SIZE)return 0;
    uint8_t *p=out;
    memcpy(p,"GP01",4);p+=4;
    memcpy(p,s->collection,256);p+=256;
    for(unsigned i=0;i<64;i++) {
        const GpDeck *d=i<4 ? &s->built[i]:&s->saved[i-4];
        put16(&p,d->name_text);memcpy(p,d->name,24);p+=24;memcpy(p,d->cards,60);p+=60;
    }
    *p++=s->selected_deck;memcpy(p,s->rng,3);p+=3;
    memcpy(p,s->card_pop_names,256);p+=256;*p++=s->card_pop_count;
    memcpy(p,s->pc_packs,15);p+=15;
    const GpChallenge *c=&s->challenge;
    memcpy(p,c->opponents,5);p+=5;memcpy(p,c->results,5);p+=5;
    *p++=c->round;*p++=c->active;*p++=c->in_duel;*p++=c->new_record;
    put16(&p,c->total_wins);put16(&p,c->streak);put16(&p,c->streak_backup);put16(&p,c->record);
    memcpy(p,c->record_name,16);p+=16;
    uint32_t crc=save_crc(out,(size_t)(p-out));
    for(unsigned i=0;i<4;i++)*p++=(uint8_t)(crc>>(i*8));
    return (size_t)(p-out);
}
bool gp_save_decode(GpState *s,const uint8_t *data,size_t n)
{
    if(!s || !data || n!=GP_SAVE_SIZE || memcmp(data,"GP01",4))return false;
    uint32_t expected=0;
    for(unsigned i=0;i<4;i++)expected|=(uint32_t)data[n-4+i]<<(i*8);
    if(save_crc(data,n-4)!=expected)return false;
    GpState *temp=malloc(sizeof(*temp));
    if(!temp)return false;
    const uint8_t *p=data+4;
    memcpy(temp->collection,p,256);p+=256;
    for(unsigned i=0;i<256;i++)if(temp->collection[i]>99 && temp->collection[i]!=128)goto reject;
    for(unsigned i=0;i<64;i++) {
        GpDeck *d=i<4 ? &temp->built[i]:&temp->saved[i-4];
        d->name_text=(uint16_t)get16(&p);memcpy(d->name,p,24);p+=24;memcpy(d->cards,p,60);p+=60;
        for(unsigned j=0;j<60;j++)if(d->cards[j]>GP_NUM_CARDS || (!d->cards[0] && d->cards[j]))goto reject;
        if(d->cards[0] && gp_validate_deck(d->cards)!=GP_DECK_OK)goto reject;
    }
    temp->selected_deck=*p++;memcpy(temp->rng,p,3);p+=3;
    if(temp->selected_deck>=4)goto reject;
    memcpy(temp->card_pop_names,p,256);p+=256;temp->card_pop_count=*p++;
    memcpy(temp->pc_packs,p,15);p+=15;
    for(unsigned i=0;i<15;i++)if((temp->pc_packs[i]&127)>15)goto reject;
    GpChallenge *c=&temp->challenge;
    memcpy(c->opponents,p,5);p+=5;memcpy(c->results,p,5);p+=5;
    c->round=*p++;c->active=*p++;c->in_duel=*p++;c->new_record=*p++;
    c->total_wins=(uint16_t)get16(&p);c->streak=(uint16_t)get16(&p);c->streak_backup=(uint16_t)get16(&p);c->record=(uint16_t)get16(&p);
    memcpy(c->record_name,p,16);p+=16;
    if(c->round>=5 || c->active>1 || c->in_duel>1 || c->new_record>1 || c->total_wins>999 || c->streak>999 || c->streak_backup>999 || c->record>999)goto reject;
    for(unsigned i=0;i<5;i++)if(c->opponents[i]>=40 || c->results[i]>2)goto reject;
    for(unsigned i=1;i<=GP_NUM_CARDS;i++)if(gp_card_count(temp,(uint8_t)i)>99)goto reject;
    *s=*temp;
    free(temp);
    return true;
reject:
    free(temp);
    return false;
}
