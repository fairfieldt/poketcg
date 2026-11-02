#include "transfer_import.h"
#include <string.h>

static TiResult identify(const uint8_t *h)
{
 uint8_t check=0;for(unsigned i=0x34;i<=0x4c;i++)check=(uint8_t)(check-h[i]-1);
 if(check!=h[0x4d])return TI_BAD_HEADER;
 /* pret/poketcg2 specifies the distinct Japanese sequel header BP7J. */
 if(!memcmp(h+0x34,"POKEMON-CG2",11)&&!memcmp(h+0x3f,"BP7J",4))return TI_UNSUPPORTED;
 if(memcmp(h+0x34,"POKECARD",8))return TI_WRONG_GAME;
 if(h[0x3c]||h[0x3d]||h[0x3e]||memcmp(h+0x3f,"AXQE",4)||h[0x4a]!=1||h[0x47]!=0x1b||h[0x49]!=3)return TI_UNSUPPORTED;
 return TI_OK;
}
/* The game restores the bank-2 backup on Continue. Decks have no checksum. */
TiResult ti_parse(const uint8_t *h,size_t hn,const uint8_t *g,size_t gn,
                  const uint8_t *d,size_t dn,GpDeck out[4],uint8_t valid[4])
{
 if(!h || !g || !d || !out || !valid || hn<80 || gn<192 || dn<336)return TI_BAD_SAVE;
 TiResult identity=identify(h);if(identity!=TI_OK)return identity;
 unsigned count=(unsigned)g[2]|(unsigned)g[3]<<8;
 unsigned expected=(unsigned)g[4]|(unsigned)g[5]<<8, sum=0;
 if(g[0]!=8||g[1]!=0||count!=0xb3||g[0x2c]>14)return TI_BAD_SAVE;
 for(unsigned i=0;i<count;i++)sum=(sum+g[8+i])&0xffff;
 if(sum!=expected)return TI_BAD_SAVE;
 memset(out,0,4*sizeof(*out));memset(valid,0,4);
 unsigned found=0;
 for(unsigned i=0;i<4;i++) {
  const uint8_t *p=d+i*84;
  if(!p[24])continue;
  if(gp_validate_deck(p+24)!=GP_DECK_OK)return TI_BAD_SAVE;
  GpDeck *deck=&out[i];memcpy(deck->cards,p+24,60);
  unsigned j=0;for(;j<23 && p[j];j++) {
   unsigned ch=p[j];deck->name[j]=(ch>=0x20&&ch<=0x7a&&ch!=0x40&&ch!=0x60)?(char)ch:'?';
  }
  deck->name[j]=0;valid[i]=1;found++;
 }
 return found?TI_OK:TI_NO_DECKS;
}

/* Only Transfer Pak registers and MBC5 ROM-space control registers are written. */
static int value(const TiBus *b,uint16_t addr,uint8_t v)
{uint8_t block[32];memset(block,v,sizeof(block));return b->write(b->ctx,addr,block);}
static int status(const TiBus *b)
{uint8_t p[32];if(b->read(b->ctx,0xb000,p))return TI_IO;if(p[0]&0x40)return TI_NO_CART;if(!(p[0]&1))return TI_IO;return TI_OK;}
static TiResult blocks(const TiBus *b,uint8_t bank,uint16_t addr,uint8_t *out,unsigned n,uint64_t start)
{
 if(value(b,0xa000,bank))return TI_IO;
 for(unsigned i=0;i<n;i+=32) {
  if(b->millis(b->ctx)-start>15000)return TI_TIMEOUT;
  int s=status(b);if(s)return (TiResult)s;
  if(b->read(b->ctx,addr+i,out+i))return TI_IO;
 }
 return TI_OK;
}
TiResult ti_import_read(const TiBus *b,GpDeck decks[4],uint8_t valid[4])
{
 if(!b||!decks||!valid||!b->connected||!b->accessory||!b->read||!b->write||!b->millis)return TI_IO;
 if(!b->connected(b->ctx))return TI_NO_CONTROLLER;
 int accessory=b->accessory(b->ctx);
 if(accessory==2)return TI_CONTROLLER_PAK;
 if(accessory!=1)return TI_NO_PAK;
 uint64_t start=b->millis(b->ctx);TiResult result=TI_IO;int powered=0,enabled=0;
 uint8_t h[96],h2[96],g[192],g2[192],d[352],d2[352];
 if(b->progress)b->progress(b->ctx,1);
 start=b->millis(b->ctx);
 if(value(b,0x8000,0x84))goto done;
 powered=1;
 if(value(b,0xb000,1))goto done;
 result=(TiResult)status(b);if(result)goto done;
 result=blocks(b,0,0xc100,h,sizeof(h),start);if(result)goto done;
 result=blocks(b,0,0xc100,h2,sizeof(h2),start);if(result)goto done;
 if(memcmp(h,h2,sizeof(h))){result=TI_IO;goto done;}
 result=identify(h);if(result)goto done;
 if(b->progress)b->progress(b->ctx,2);
 start=b->millis(b->ctx);
 /* GB $0000 enables SRAM; $4000 selects MBC5 SRAM bank 2. */
 if(value(b,0xa000,0)||value(b,0xc000,0x0a)){result=TI_IO;goto done;}
 enabled=1;
 if(value(b,0xa000,1)||value(b,0xc000,2)){result=TI_IO;goto done;}
 result=blocks(b,2,0xe800,g,sizeof(g),start);if(result)goto done;
 result=blocks(b,2,0xe200,d,sizeof(d),start);if(result)goto done;
 result=blocks(b,2,0xe800,g2,sizeof(g2),start);if(result)goto done;
 result=blocks(b,2,0xe200,d2,sizeof(d2),start);if(result)goto done;
 if(memcmp(g,g2,sizeof(g))||memcmp(d,d2,sizeof(d))){result=TI_IO;goto done;}
 result=ti_parse(h,sizeof(h),g,sizeof(g),d,sizeof(d),decks,valid);
done:
 if(enabled){if(!value(b,0xa000,0))value(b,0xc000,0);}
 if(powered)value(b,0x8000,0xfe);
 return result;
}
