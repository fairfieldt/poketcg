#ifndef WEB_MEMPAK_H
#define WEB_MEMPAK_H
#include <stdint.h>
typedef struct {
  uint32_t vendor;
  uint16_t game_id;
  uint8_t region, blocks, valid;
  char name[32];
} entry_structure_t;
int validate_mempak(int port);
int get_mempak_free_space(int port);
int get_mempak_entry(int port, int index, entry_structure_t *entry);
int write_mempak_entry_data(int port, entry_structure_t *entry, uint8_t *data);
#endif
