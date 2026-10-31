#ifndef ADVENTURE_NAMING_H
#define ADVENTURE_NAMING_H
#include <stdbool.h>

/* Fresh player naming: six characters, or MARK when the name is left empty. */
void adventure_name_player(char *name,unsigned capacity);
/* Native rendering and input-rule checks for the diagnostic ROM. */
struct surface_s *adventure_name_player_preview(void);
bool adventure_naming_check(void);
#endif
