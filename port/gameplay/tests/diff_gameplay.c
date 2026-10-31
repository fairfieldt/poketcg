/* Differential validation against original assembled SM83 routines. The GB CPU
   is a test oracle only; gameplay/ contains no emulator or ROM dependency. */
#include "gameplay.h"
#include "../../harness/gb/gb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {unsigned bank,addr;char name[128];} Symbol;
static Symbol symbols[30000];static unsigned symbol_count;
static GB *gb;
static Symbol symbol(const char *name)
{
    for(unsigned i=0;i<symbol_count;i++)if(!strcmp(symbols[i].name,name))return symbols[i];
    fprintf(stderr,"Unknown symbol: %s\n",name);exit(1);
}
static void call(const char *name)
{
    Symbol s=symbol(name);
    gb->ime=gb->ie=0;gb_write(gb,0xff40,0);
    if(s.bank){gb_write(gb,0x2000,(uint8_t)s.bank);gb->hram[0]=(uint8_t)s.bank;}
    gb->sp=0xdfee;
    gb_write(gb,gb->sp,0xfe);gb_write(gb,gb->sp+1,0x3f);
    gb->pc=(uint16_t)s.addr;gb->halted=0;gb->bp_skip_n=0;
    int result=gb_run(gb,50000000,0);
    if(result!=GB_STOP_BREAKPOINT || gb->pc!=0x3ffe) {
        fprintf(stderr,"%s did not return: result%d pc%04x\n",name,result,gb->pc);exit(1);
    }
}
int main(int argc,char **argv)
{
    if(argc!=3){fprintf(stderr,"usage: %s ROM SYM\n",argv[0]);return 2;}
    FILE *file=fopen(argv[1],"rb");if(!file)return 2;
    fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);
    uint8_t *rom=malloc((size_t)size);if(!rom)return 2;
    if(fread(rom,1,(size_t)size,file)!=(size_t)size)return 2;fclose(file);
    gb=gb_new(rom,(size_t)size);free(rom);if(!gb)return 2;
    file=fopen(argv[2],"r");if(!file)return 2;
    char line[256];
    while(fgets(line,sizeof(line),file) && symbol_count<30000) {
        Symbol *s=&symbols[symbol_count];
        if(sscanf(line,"%x:%x %127s",&s->bank,&s->addr,s->name)==3)symbol_count++;
    }
    fclose(file);gb_add_breakpoint(gb,-1,0x3ffe);
    Symbol rng=symbol("wRNG1"),drawn=symbol("wBoosterCardsDrawn");
    static GpState state;
    for(unsigned pack=0;pack<29;pack++)for(unsigned trial=0;trial<32;trial++) {
        uint8_t seed[3]={(uint8_t)(trial*7),(uint8_t)(trial*49+7),(uint8_t)(trial+77)},cards[10];
        gp_init(&state,seed);
        memset(gb->wram,0,sizeof(gb->wram));memset(gb->sram,0,sizeof(gb->sram));
        memset(gb->sram+0x100,128,256);
        gb_write(gb,0x4000,0);
        for(unsigned i=0;i<3;i++)gb_write(gb,(uint16_t)(rng.addr+i),seed[i]);
        gb->a=(uint8_t)pack;call("GenerateBoosterPack");
        if(!gp_open_booster(&state,pack,cards))return 1;
        for(unsigned i=0;i<10;i++)if(cards[i]!=gb_read(gb,(uint16_t)(drawn.addr+i))) {
            fprintf(stderr,"Booster mismatch pack%u trial%u index%u C%u GB%u\n",pack,trial,i,cards[i],gb_read(gb,(uint16_t)(drawn.addr+i)));return 1;
        }
        for(unsigned i=0;i<3;i++)if(state.rng[i]!=gb_read(gb,(uint16_t)(rng.addr+i))) {
            fprintf(stderr,"RNG mismatch pack%u trial%u byte%u\n",pack,trial,i);return 1;
        }
        if(memcmp(state.collection,gb->sram+0x100,256)) {
            fprintf(stderr,"Collection mismatch pack%u trial%u\n",pack,trial);return 1;
        }
    }
    Symbol opponents=symbol("sChallengeMachineOpponents");
    for(unsigned trial=0;trial<256;trial++) {
        uint8_t seed[3]={(uint8_t)trial,(uint8_t)(trial*13),87};gp_init(&state,seed);
        memset(gb->wram,0,sizeof(gb->wram));memset(gb->sram,0,sizeof(gb->sram));gb_write(gb,0x4000,0);
        for(unsigned i=0;i<3;i++)gb_write(gb,(uint16_t)(rng.addr+i),seed[i]);
        call("ChallengeMachine_PickOpponentSequence");gp_challenge_begin(&state);
        gb_write(gb,0,0x0a);
        for(unsigned i=0;i<5;i++)if(state.challenge.opponents[i]!=gb_read(gb,(uint16_t)(opponents.addr+i))) {
            fprintf(stderr,"Challenge mismatch trial%u index%u C%u GB%u\n",trial,i,state.challenge.opponents[i],gb_read(gb,(uint16_t)(opponents.addr+i)));return 1;
        }
        for(unsigned i=0;i<3;i++)if(state.rng[i]!=gb_read(gb,(uint16_t)(rng.addr+i)))return 1;
    }
    Symbol player=symbol("sPlayerName"),peer=symbol("wNameBuffer");
    for(unsigned trial=0;trial<256;trial++) {
        uint8_t seed[3]={(uint8_t)trial,(uint8_t)(trial*3),51},own_name[16],peer_name[16],card;
        gp_init(&state,seed);gp_name_encode(&state,"MARK",own_name);gp_name_encode(&state,"AMY",peer_name);
        memset(gb->wram,0,sizeof(gb->wram));memset(gb->sram,0,sizeof(gb->sram));gb_write(gb,0x4000,0);gb_write(gb,0,0x0a);
        for(unsigned i=0;i<16;i++){gb_write(gb,(uint16_t)(player.addr+i),own_name[i]);gb_write(gb,(uint16_t)(peer.addr+i),peer_name[i]);}
        call("DecideCardToReceiveFromCardPop");
        if(!gp_card_pop(&state,own_name,peer_name,&card) || card!=gb->a) {
            fprintf(stderr,"CardPop mismatch trial%u C%u GB%u\n",trial,card,gb->a);return 1;
        }
        for(unsigned i=0;i<3;i++)if(state.rng[i]!=gb_read(gb,(uint16_t)(rng.addr+i)))return 1;
    }
    puts("Differential OK:29 boosters x32seeds (cards,RNG,collection),256 ChallengeMachine sequences,256 CardPop selections; identical to original SM83 routines");
    gb_free(gb);
}
