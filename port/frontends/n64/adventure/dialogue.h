#ifndef ADVENTURE_DIALOGUE_H
#define ADVENTURE_DIALOGUE_H
#include <stdbool.h>

void adventure_story_message(const char *speaker,const char *text,unsigned speed);
int adventure_story_choice(const char *question,const char *const *items,
 unsigned count,int initial,bool cancel);
void adventure_duel_transition(bool entering);
/* Native snapshots and text/pagination checks for diagnostic builds. */
struct surface_s *adventure_story_message_preview(const char *speaker,const char *text);
bool adventure_dialogue_check(void);
#endif
