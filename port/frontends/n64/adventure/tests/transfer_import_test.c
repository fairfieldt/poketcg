#include "../transfer_import.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef N64
#include <libdragon.h>
#endif

typedef struct {uint8_t header[96],general[192],decks[352];int connected,accessory,removed,fail,reads,writes,bank,ram_bank,enabled,power;uint64_t clock,step;} Mock;
static int connected(void *p){return ((Mock *)p)->connected;}
static int accessory(void *p){return ((Mock *)p)->accessory;}
static int write_block(void *p,uint16_t a,const uint8_t *d)
{
 Mock *m=p;m->writes++;
 for(int i=1;i<32;i++)assert(d[i]==d[0]);
 if(a==0x8000){m->power=d[0]==0x84;return 0;}
 if(a==0xb000)return 0;
 if(a==0xa000){m->bank=d[0];return 0;}
 assert(a==0xc000); /* Never write to GB SRAM or any other cartridge address. */
 if(m->bank==0)m->enabled=d[0]==10;
 else {assert(m->bank==1);m->ram_bank=d[0];}
 return 0;
}
static int read_block(void *p,uint16_t a,uint8_t *d)
{
 Mock *m=p;if(m->fail && ++m->reads==m->fail)return -1;
 if(a==0xb000){memset(d,0,32);d[0]=m->removed?0x40:1;return 0;}
 if(m->bank==0 && a>=0xc100 && a<0xc160){memcpy(d,m->header+a-0xc100,32);return 0;}
 assert(m->bank==2 && m->enabled && m->ram_bank==2);
 if(a>=0xe200 && a<0xe360){memcpy(d,m->decks+a-0xe200,32);return 0;}
 if(a>=0xe800 && a<0xe8c0){memcpy(d,m->general+a-0xe800,32);return 0;}
 assert(0);return -1;
}
static uint64_t millis(void *p){Mock *m=p;m->clock+=m->step;return m->clock;}
static void header_checksum(Mock *m)
{uint8_t n=0;for(unsigned i=0x34;i<=0x4c;i++)n=(uint8_t)(n-m->header[i]-1);m->header[0x4d]=n;}
static Mock fixture(void)
{
 Mock m={0};m.connected=m.accessory=1;memcpy(m.header+0x34,"POKECARD",8);memcpy(m.header+0x3f,"AXQE",4);
 m.header[0x47]=0x1b;m.header[0x49]=3;m.header[0x4a]=1;header_checksum(&m);
 m.general[0]=8;m.general[2]=0xb3;
 unsigned basic=0,energy=0;
 for(unsigned i=1;i<=GP_NUM_CARDS;i++) {if(!basic&&gp_cards[i].type<8&&gp_cards[i].stage==0)basic=i;if(!energy&&gp_cards[i].type>=8&&gp_cards[i].type<=13)energy=i;}
 assert(basic&&energy);memcpy(m.decks,"TEST",4);m.decks[24]=basic;memset(m.decks+25,energy,59);
 return m;
}
static TiResult run(Mock *m,GpDeck out[4],uint8_t valid[4])
{TiBus bus={m,connected,accessory,read_block,write_block,millis,NULL};return ti_import_read(&bus,out,valid);}
int main(void)
{
#ifdef N64
 debug_init_isviewer();
#endif
 GpDeck out[4];uint8_t valid[4];Mock m=fixture();
 assert(run(&m,out,valid)==TI_OK && valid[0]&&!valid[1] && !strcmp(out[0].name,"TEST"));
 assert(!m.enabled&&!m.power && m.ram_bank==2);
 m=fixture();m.connected=0;assert(run(&m,out,valid)==TI_NO_CONTROLLER && !m.writes);
 m=fixture();m.accessory=0;assert(run(&m,out,valid)==TI_NO_PAK && !m.writes);
 m=fixture();m.accessory=2;assert(run(&m,out,valid)==TI_CONTROLLER_PAK && !m.writes);
 m=fixture();m.removed=1;assert(run(&m,out,valid)==TI_NO_CART && !m.power);
 m=fixture();m.fail=20;assert(run(&m,out,valid)==TI_IO && !m.power && !m.enabled);
 m=fixture();m.step=16000;assert(run(&m,out,valid)==TI_TIMEOUT && !m.power);
 m=fixture();m.header[0x34]++;assert(run(&m,out,valid)==TI_BAD_HEADER && !m.enabled);
 m=fixture();m.header[0x34]='X';header_checksum(&m);assert(run(&m,out,valid)==TI_WRONG_GAME && !m.enabled);
 m=fixture();m.header[0x42]='J';header_checksum(&m);assert(run(&m,out,valid)==TI_UNSUPPORTED && !m.enabled);
 m=fixture();memcpy(m.header+0x34,"POKEMON-CG2",11);memcpy(m.header+0x3f,"BP7J",4);header_checksum(&m);assert(run(&m,out,valid)==TI_UNSUPPORTED && !m.enabled);
 m=fixture();m.general[8]++;assert(run(&m,out,valid)==TI_BAD_SAVE);
 m=fixture();m.general[0x2c]=15;m.general[4]=15;assert(run(&m,out,valid)==TI_BAD_SAVE);
 m=fixture();m.decks[24]=255;assert(run(&m,out,valid)==TI_BAD_SAVE);
 m=fixture();m.decks[25]=0;assert(run(&m,out,valid)==TI_BAD_SAVE);
 m=fixture();m.decks[24]=0;assert(run(&m,out,valid)==TI_NO_DECKS);
 m=fixture();assert(ti_parse(m.header,79,m.general,192,m.decks,352,out,valid)==TI_BAD_SAVE);
 puts("Transfer Pak parser and transport tests passed");
#ifdef N64
 debugf("TRANSFER IMPORT TEST PASS\n");
 for(;;){}
#endif
 return 0;
}
