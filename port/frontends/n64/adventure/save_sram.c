/* Two 16 KiB journal slots on a 32 KiB cartridge SRAM. Header is committed
 * last; either the preceding save or the complete new save survives a reset. */
#include <libdragon.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../common/save_store.h"
#include "../save_store_n64.h"

#define SLOT 16384u
#define MAX_FILES 8u
#define HEADER (32u+MAX_FILES*24u)
typedef struct { char name[16]; uint32_t size; uint8_t *data; } SaveFile;
static SaveFile files[MAX_FILES];
static uint32_t generation,writes;
static int active=-1;
static bool opened;
static uint8_t image[SLOT] __attribute__((aligned(16)));
static uint8_t verify[SLOT] __attribute__((aligned(16)));

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void put32(uint8_t *p,uint32_t n) { p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n; }
static uint32_t crc32(const uint8_t *p,unsigned n)
{
 uint32_t crc=~0u;
 while(n--) { crc^=*p++;for(unsigned k=0;k<8;k++)crc=(crc>>1)^((0u-(crc&1))&0xedb88320u); }
 return ~crc;
}
static void sram_timing(void)
{
#ifndef ADVENTURE_SRAM_TEST
 dma_wait();
 *(volatile uint32_t *)0xa4600024=5;
 *(volatile uint32_t *)0xa4600028=12;
 *(volatile uint32_t *)0xa460002c=13;
 *(volatile uint32_t *)0xa4600030=2;
#endif
}
static void read_slot(unsigned slot,uint8_t *out)
{
 sram_timing();data_cache_hit_invalidate(out,SLOT);
 dma_read_async(out,0x08000000u+slot*SLOT,SLOT);dma_wait();
}
static void write_part(unsigned slot,unsigned offset,const uint8_t *data,unsigned length)
{
 sram_timing();data_cache_hit_writeback(data,length);
 dma_write_raw_async(data,0x08000000u+slot*SLOT+offset,length);dma_wait();writes++;
}
static bool valid(const uint8_t *data)
{
 if(memcmp(data,"TCGADV01",8)||get32(data+12)>SLOT-HEADER||get32(data+24)!=crc32(data,24))return false;
 return get32(data+16)==crc32(data+HEADER,get32(data+12)) &&
        get32(data+20)==crc32(data+32,HEADER-32);
}
bool save_store_open(const char *where)
{
 (void)where;
 if(opened)return true;
 read_slot(0,image);read_slot(1,verify);
 bool a=valid(image),b=valid(verify);
 if(b && (!a || (int32_t)(get32(verify+8)-get32(image+8))>0)) {memcpy(image,verify,SLOT);active=1;}
 else if(a)active=0;
 if(active>=0) {
  generation=get32(image+8);
  for(unsigned i=0;i<MAX_FILES;i++) {
   const uint8_t *entry=image+32+i*24;
   unsigned offset=get32(entry+16),size=get32(entry+20);
   if(!entry[0])continue;
   if(!memchr(entry,0,16)||offset<HEADER||offset>SLOT||size>SLOT-offset)continue;
   SaveFile *f=&files[i];memcpy(f->name,entry,16);f->size=size;
   f->data=malloc(size?size:1);if(!f->data)return false;memcpy(f->data,image+offset,size);
  }
 }
 opened=true;return true;
}
static int find(const char *name)
{for(unsigned i=0;i<MAX_FILES;i++)if(!strcmp(files[i].name,name))return i;return -1;}
int save_store_read(const char *name,uint8_t *data,uint32_t size)
{
 int index=find(name);if(index<0)return -1;
 SaveFile *f=&files[index];if(size>f->size)size=f->size;memcpy(data,f->data,size);return size;
}
static bool commit(void)
{
 memset(image,0,SLOT);unsigned cursor=HEADER;
 for(unsigned i=0;i<MAX_FILES;i++) {
  SaveFile *f=&files[i];if(!f->name[0])continue;
  if(f->size>SLOT-cursor)return false;
  uint8_t *entry=image+32+i*24;memcpy(entry,f->name,16);
  put32(entry+16,cursor);put32(entry+20,f->size);memcpy(image+cursor,f->data,f->size);cursor+=f->size;
 }
 memcpy(image,"TCGADV01",8);put32(image+8,generation+1);put32(image+12,cursor-HEADER);
 put32(image+16,crc32(image+HEADER,cursor-HEADER));put32(image+20,crc32(image+32,HEADER-32));
 put32(image+24,crc32(image,24));
 unsigned target=active==0?1:0;
 static const uint8_t zero[32] __attribute__((aligned(16)))={0};
 write_part(target,0,zero,32);
 write_part(target,32,image+32,SLOT-32);
 write_part(target,0,image,32);
 read_slot(target,verify);
 if(!valid(verify)||memcmp(image,verify,SLOT))return false;
 active=target;generation++;return true;
}
bool save_store_write(const char *name,const uint8_t *data,uint32_t size)
{
 if(!name||!name[0]||strlen(name)>=16||size>SLOT-HEADER)return false;
 int index=find(name);
 if(index<0)for(unsigned i=0;i<MAX_FILES;i++)if(!files[i].name[0]){index=i;break;}
 if(index<0)return false;
 unsigned total=size;
 for(unsigned i=0;i<MAX_FILES;i++)if(i!=(unsigned)index)total+=files[i].size;
 if(total>SLOT-HEADER)return false;
 SaveFile *f=&files[index];if(f->size==size&&f->data&&!memcmp(f->data,data,size))return true;
 uint8_t *copy=malloc(size?size:1);if(!copy)return false;memcpy(copy,data,size);
 SaveFile old=*f;memset(f,0,sizeof(*f));strcpy(f->name,name);f->size=size;f->data=copy;
 if(!commit()){*f=old;free(copy);return false;}
 free(old.data);return true;
}
bool save_store_remove(const char *name)
{
 int index=find(name);if(index<0)return true;
 SaveFile old=files[index];memset(&files[index],0,sizeof(files[index]));
 if(!commit()){files[index]=old;return false;}free(old.data);return true;
}
bool adventure_save_pair(const char *first,const uint8_t *a,unsigned an,
                         const char *second,const uint8_t *b,unsigned bn)
{
 if(!first||!second||!first[0]||!second[0]||strlen(first)>=16||strlen(second)>=16||!strcmp(first,second))return false;
 const char *names[2]={first,second};const uint8_t *data[2]={a,b};unsigned sizes[2]={an,bn};
 int indexes[2]={find(first),find(second)};SaveFile old[2];
 for(unsigned k=0;k<2;k++)if(indexes[k]<0)for(unsigned i=0;i<MAX_FILES;i++)if(!files[i].name[0]&&(k==0||(int)i!=indexes[0])){indexes[k]=i;break;}
 if(indexes[0]<0||indexes[1]<0)return false;
 unsigned total=an+bn;
 for(unsigned i=0;i<MAX_FILES;i++)if((int)i!=indexes[0]&&(int)i!=indexes[1])total+=files[i].size;
 if(total>SLOT-HEADER)return false;
 uint8_t *copies[2]={malloc(an?an:1),malloc(bn?bn:1)};
 if(!copies[0]||!copies[1]){free(copies[0]);free(copies[1]);return false;}
 for(unsigned k=0;k<2;k++) {
  memcpy(copies[k],data[k],sizes[k]);old[k]=files[indexes[k]];
  SaveFile *file=&files[indexes[k]];memset(file,0,sizeof(*file));strcpy(file->name,names[k]);file->size=sizes[k];file->data=copies[k];
 }
 bool ok=commit();
 for(unsigned k=0;k<2;k++)if(ok)free(old[k].data);else{files[indexes[k]]=old[k];free(copies[k]);}
 return ok;
}
void save_store_poll(void) {}
void save_store_flush(void) {}
int save_store_pending(void) {return 0;}
uint32_t save_store_blocks_written(void) {return writes;}
#if ADVENTURE_DIAGNOSTIC
void adventure_save_reopen(void)
{
 for(unsigned i=0;i<MAX_FILES;i++)free(files[i].data);
 memset(files,0,sizeof(files));opened=false;active=-1;generation=0;
 save_store_open(NULL);
}
#endif
