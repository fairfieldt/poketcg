#ifndef TEST_LIBDRAGON_H
#define TEST_LIBDRAGON_H
#include <stddef.h>
void dma_wait(void);
void dma_read_async(void *,unsigned long,unsigned long);
void dma_write_raw_async(const void *,unsigned long,unsigned long);
void data_cache_hit_writeback(const void *,unsigned long);
void data_cache_hit_invalidate(void *,unsigned long);
#endif
