#include "gameplay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static GpState s;
static const uint8_t seed[3]={0x12,0x34,0x56};
static void inventory(void)
{
    gp_init(&s,seed);
    unsigned denominator;
    assert(gp_album_count(&s,&denominator)==0 && denominator==226);
    assert(gp_give_starter(&s,0));
    assert(gp_total_cards(&s)==90);
    assert(gp_validate_deck(s.built[0].cards)==GP_DECK_OK);
    assert(!gp_give_starter(&s,0));
    assert(gp_dismantle_deck(&s,0)==GP_DECK_LAST);
    unsigned committed=gp_card_count(&s,2)-gp_collection_count(&s,2);
    while(gp_add_card(&s,2));
    assert(gp_card_count(&s,2)==99);
    assert(gp_collection_count(&s,2)==99-committed);
    assert(!gp_add_card(&s,2));
    gp_init(&s,seed);
    assert(gp_add_card(&s,10));assert(gp_remove_card(&s,10));
    assert(gp_album_count(&s,&denominator)==1 && denominator==227);
    assert(gp_collection_count(&s,10)==0 && !(s.collection[10]&128));
    assert(!gp_remove_card(&s,10));
}
static void boosters(void)
{
    for(unsigned pack=0;pack<29;pack++) for(unsigned trial=0;trial<256;trial++) {
        uint8_t rng[3]={(uint8_t)trial,(uint8_t)(trial*47),34},cards[10];
        gp_init(&s,rng);
        assert(gp_open_booster(&s,pack,cards));
        assert(gp_total_cards(&s)==10);
        unsigned rarities[3]={0},energies=0;
        for(unsigned i=0;i<10;i++) {
            assert(cards[i]>=1 && cards[i]<=228);
            unsigned type=gp_cards[cards[i]].type;
            if(pack<25 && (pack>=14 || i<9))rarities[gp_cards[cards[i]].rarity]++;
            if(type>=8 && type<=14)energies++;
            else {
                assert(gp_cards[cards[i]].set>>4==gp_boosters[pack].set);
                for(unsigned j=0;j<i;j++)assert(cards[i]!=cards[j]);
            }
        }
        if(pack<25) {
            assert(rarities[2]==1 && rarities[1]==3);
            assert(rarities[0]==(pack<14 ? 5u:6u));
            if(pack<14)assert(energies==1);
        } else assert(energies==10);
    }
}
static void decks(void)
{
    for(unsigned choice=0;choice<3;choice++) {
        gp_init(&s,seed);assert(gp_give_starter(&s,choice));
        assert(gp_total_cards(&s)==90);
    }
    for(unsigned recipe=0;recipe<50;recipe++) {
        gp_init(&s,seed);
        const GpRecipe *r=&gp_auto_recipes[recipe];
        assert(gp_validate_deck(r->cards)==GP_DECK_OK);
        for(unsigned i=0;i<60;i++)assert(gp_add_card(&s,r->cards[i]));
        assert(gp_find_dismantle_mask(&s,r->cards)==0);
        assert(gp_build_auto_deck(&s,recipe,0,0)==GP_DECK_OK);
        assert(gp_total_cards(&s)==60);
        assert(gp_find_dismantle_mask(&s,r->cards)==1);
        assert(gp_save_recipe(&s,59,&s.built[0])==GP_DECK_OK);
    }
    gp_init(&s,seed);
    GpDeck bad=gp_preset_decks[5];
    memset(bad.cards,1,60);
    assert(gp_validate_deck(bad.cards)==GP_DECK_NO_BASIC);
    bad=gp_preset_decks[5];
    for(unsigned i=0;i<5;i++)bad.cards[i]=0x30; /* Charmander. */
    assert(gp_validate_deck(bad.cards)==GP_DECK_TOO_MANY_SAME_NAME);
    assert(gp_set_deck(&s,0,&gp_preset_decks[5])==GP_DECK_NOT_OWNED);
}
static void card_pop(void)
{
    gp_init(&s,seed);
    uint8_t own[16]={0x31},peer[16]={0x2c},card;
    assert(gp_card_pop(&s,own,peer,&card));
    assert(card==0xa1); /* hash difference5 means hidden Mew, preserving parity bug. */
    assert(!gp_card_pop(&s,own,peer,&card));
    for(unsigned i=0;i<16;i++) {
        peer[1]=(uint8_t)(i+1);
        assert(gp_card_pop(&s,own,peer,&card));
        assert(card>=1 && card<=228);
    }
    peer[1]=0; /* 16-name ring evicts first peer. */
    assert(gp_card_pop(&s,own,peer,&card));
}
static void mail_and_challenge(void)
{
    gp_init(&s,seed);
    uint8_t cards[20],name[16]="PLAYER";
    assert(s.pc_packs[0]==0x81 && !gp_give_mail(&s,1));
    assert(gp_open_mail(&s,0,cards)==10);
    assert(!gp_open_mail(&s,0,cards));
    assert(gp_give_mail(&s,10));
    assert(gp_open_mail(&s,1,cards)==20);
    for(unsigned id=2;id<=15;id++)if(id!=10)assert(gp_give_mail(&s,id));
    for(unsigned slot=2;slot<15;slot++)assert(gp_open_mail(&s,slot,cards)>=10);
    for(unsigned trial=0;trial<256;trial++) {
        uint8_t rng[3]={(uint8_t)trial,7,23},deck,prizes;
        gp_init(&s,rng);gp_challenge_begin(&s);
        GpChallenge before=s.challenge;gp_challenge_begin(&s);
        assert(!memcmp(&before,&s.challenge,sizeof(before)));
        assert(s.challenge.opponents[0]<24 && s.challenge.opponents[1]<24 && s.challenge.opponents[2]<24);
        assert(s.challenge.opponents[0]!=s.challenge.opponents[1]);
        assert(s.challenge.opponents[1]!=s.challenge.opponents[2]);
        assert(s.challenge.opponents[0]!=s.challenge.opponents[2]);
        assert(s.challenge.opponents[3]>=24 && s.challenge.opponents[3]<32);
        assert(s.challenge.opponents[4]>=32 && s.challenge.opponents[4]<40);
        for(unsigned i=0;i<5;i++) {
            assert(gp_challenge_opponent(&s,&deck,&prizes));
            assert(deck<53 && prizes==(i<3 ? 4:6));
            assert(gp_challenge_result(&s,true,name)==(i<4 ? 1u:2u));
        }
        assert(s.challenge.record==5 && s.challenge.total_wins==1 && !s.challenge.active);
        gp_challenge_begin(&s);assert(s.challenge.streak==5);
        assert(gp_challenge_result(&s,true,name)==1);
        assert(!gp_challenge_result(&s,false,name));
        assert(!s.challenge.streak && !s.challenge.streak_backup && s.challenge.record==6);
    }
}
static void saves(void)
{
    static uint8_t blob[GP_SAVE_SIZE],copy[GP_SAVE_SIZE];
    static GpState restored,unchanged;
    gp_init(&s,seed);assert(gp_give_starter(&s,0));gp_challenge_begin(&s);
    assert(gp_save_encode(&s,blob,sizeof(blob))==sizeof(blob));
    assert(!gp_save_encode(&s,blob,sizeof(blob)-1));
    assert(gp_save_decode(&restored,blob,sizeof(blob)));
    assert(gp_save_encode(&restored,copy,sizeof(copy))==sizeof(copy));
    assert(!memcmp(blob,copy,sizeof(blob)));
    unchanged=restored;
    for(unsigned i=0;i<sizeof(blob);i+=97) {
        blob[i]^=0x80;
        assert(!gp_save_decode(&restored,blob,sizeof(blob)));
        assert(!memcmp(&restored,&unchanged,sizeof(restored)));
        blob[i]^=0x80;
    }
    for(unsigned n=0;n<sizeof(blob);n++)assert(!gp_save_decode(&restored,blob,n));
    /* Recompute CRC after mutating a card ID, to test semantic validation too. */
    blob[4+256+26]=255;
    uint32_t crc=0xffffffffu;
    for(unsigned i=0;i<sizeof(blob)-4;i++) {
        crc^=blob[i];
        for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    }
    crc=~crc;
    for(unsigned i=0;i<4;i++)blob[sizeof(blob)-4+i]=(uint8_t)(crc>>(i*8));
    assert(!gp_save_decode(&restored,blob,sizeof(blob)));
    assert(!memcmp(&restored,&unchanged,sizeof(restored)));
}
static void link_transactions(void)
{
    static GpState peer,before_sender,before_peer;
    uint8_t names[2][16],cards[2],send[3]={2,2,2};unsigned received;
    gp_init(&s,seed);gp_init(&peer,seed);
    assert(gp_name_encode(&s,"MARK",names[0]));
    assert(gp_name_encode(&peer,"AMY",names[1]));
    const uint8_t mark[]={3,0x3c,3,0x30,3,0x41,3,0x3a};
    assert(!memcmp(names[0],mark,8));
    assert(gp_card_pop_pair(&s,names[0],&peer,names[1],cards));
    assert(gp_total_cards(&s)==1 && gp_total_cards(&peer)==1);
    before_sender=s;before_peer=peer;
    assert(!gp_card_pop_pair(&s,names[0],&peer,names[1],cards));
    assert(!memcmp(&s,&before_sender,sizeof(s)) && !memcmp(&peer,&before_peer,sizeof(peer)));
    assert(gp_transfer_cards(&s,&peer,send,3,&received)==GP_LINK_NOT_OWNED);
    assert(!memcmp(&s,&before_sender,sizeof(s)) && !memcmp(&peer,&before_peer,sizeof(peer)));
    gp_init(&s,seed);gp_init(&peer,seed);
    for(unsigned i=0;i<3;i++)assert(gp_add_card(&s,2));
    for(unsigned i=0;i<98;i++)assert(gp_add_card(&peer,2));
    assert(gp_transfer_cards(&s,&peer,send,3,&received)==GP_LINK_OK && received==1);
    assert(gp_card_count(&s,2)==0 && gp_card_count(&peer,2)==99);
    assert(gp_transfer_recipe(&s,0,&peer,0,false)==GP_LINK_OCCUPIED);
    assert(gp_transfer_recipe(&s,0,&peer,59,false)==GP_LINK_OK);
    assert(!memcmp(&s.saved[0],&peer.saved[59],sizeof(GpDeck)));
    assert(gp_total_cards(&s)==0 && gp_total_cards(&peer)==99);
    before_sender=s;
    assert(!gp_name_encode(&s,"TOO LONG",names[0]));
    assert(!memcmp(&s,&before_sender,sizeof(s)));
}
int main(void)
{
    inventory();boosters();decks();card_pop();mail_and_challenge();saves();link_transactions();
    printf("Gameplay OK:29 boosters x256 seeds,50 recipes,3 starters,inventory/album,decks,Card Pop,15 mail,challenge256 seeds,canonical save corruption/truncation; state%zu save%u bytes\n",sizeof(GpState),GP_SAVE_SIZE);
}
