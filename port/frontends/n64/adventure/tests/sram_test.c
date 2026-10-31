#define ADVENTURE_SRAM_TEST 1
#include <assert.h>
#include <stdio.h>
#include "../save_sram.c"
static uint8_t cartridge[32768];
static int cut_after=-1,operation;
void dma_wait(void) {}
void data_cache_hit_writeback(const void *p,unsigned long n){(void)p;(void)n;}
void data_cache_hit_invalidate(void *p,unsigned long n){(void)p;(void)n;}
void dma_read_async(void *p,unsigned long addr,unsigned long n)
{assert(addr>=0x08000000u&&addr+n<=0x08008000u);memcpy(p,cartridge+addr-0x08000000u,n);}
void dma_write_raw_async(const void *p,unsigned long addr,unsigned long n)
{assert(addr>=0x08000000u&&addr+n<=0x08008000u);if(cut_after<0||operation++<cut_after)memcpy(cartridge+addr-0x08000000u,p,n);}
static void reboot(void)
{
 for(unsigned i=0;i<MAX_FILES;i++)free(files[i].data);
 memset(files,0,sizeof(files));active=-1;generation=0;opened=false;cut_after=-1;operation=0;
 assert(save_store_open(NULL));
}
int main(void)
{
 memset(cartridge,255,sizeof(cartridge));reboot();
 uint8_t world[7000],duel[1660],readback[7000];memset(world,42,sizeof(world));memset(duel,71,sizeof(duel));
 assert(save_store_write("adventure",world,sizeof(world)));
 assert(save_store_write("duel",duel,sizeof(duel)));reboot();
 assert(save_store_read("adventure",readback,sizeof(readback))==7000&&!memcmp(readback,world,7000));
 assert(save_store_read("duel",readback,sizeof(readback))==1660&&!memcmp(readback,duel,1660));
 /* Interrupt before invalidation, after invalidation, and after payload write.
    Each interrupted write must recover the previous complete game on reboot. */
 for(int cut=0;cut<3;cut++) {
  cut_after=cut;operation=0;memset(world,43,7000);
  assert(!save_store_write("adventure",world,7000));reboot();
  assert(save_store_read("adventure",readback,7000)==7000&&readback[0]==42);
 }
 assert(save_store_write("adventure",world,7000));reboot();
 assert(save_store_read("adventure",readback,7000)==7000&&readback[0]==43);
 assert(save_store_remove("duel"));reboot();assert(save_store_read("duel",readback,7000)==-1);
 /* Corrupt newest slot's generation/header: journal rolls back, not forward
    to an otherwise valid but corrupted-header save. */
 cartridge[active*SLOT+8]^=0x80;reboot();
 assert(save_store_read("adventure",readback,7000)==7000&&readback[0]==43);
 assert(save_store_read("duel",readback,7000)==1660);
 puts("SRAM PASS: save/reboot, full diary+duel, interrupted writes, remove, corrupt-header rollback");
}
