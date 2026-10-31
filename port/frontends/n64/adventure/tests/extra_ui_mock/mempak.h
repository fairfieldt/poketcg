#ifndef EXTRA_UI_MEMPAK_H
#define EXTRA_UI_MEMPAK_H
#include <stdint.h>
typedef struct {uint32_t vendor;uint16_t game_id,inode;uint8_t region,blocks,valid,entry_id;char name[62];} entry_structure_t;
int validate_mempak(int);
int get_mempak_free_space(int);
int get_mempak_entry(int,int,entry_structure_t *);
int write_mempak_entry_data(int,entry_structure_t *,uint8_t *);
#endif
