#ifndef TEST_PROFILES_LIBDRAGON_H
#define TEST_PROFILES_LIBDRAGON_H
#include <stddef.h>
#include <stdbool.h>
typedef enum {JOYPAD_PORT_1=0,JOYPAD_PORT_2=1} joypad_port_t;
bool joypad_is_connected(joypad_port_t);
void dma_wait(void);
void dma_read_async(void *,unsigned long,unsigned long);
void dma_write_raw_async(const void *,unsigned long,unsigned long);
void data_cache_hit_writeback(const void *,unsigned long);
void data_cache_hit_invalidate(void *,unsigned long);
void rspq_wait(void);
#endif
