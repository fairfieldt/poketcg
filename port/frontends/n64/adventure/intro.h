#ifndef ADVENTURE_INTRO_H
#define ADVENTURE_INTRO_H
#include <stdbool.h>

/* Play the original opening, then wait at its title screen. A/Start skips the
   opening to the title; a second press opens the diary selector. */
void adventure_intro_play(bool opening);

/* Render a completed native frame for diagnostics. Scenes 0..2 are the three
   booster introductions; scene 3 is the title. Time is in original 60 Hz ticks. */
struct surface_s *adventure_intro_preview(unsigned scene, unsigned source_frame);
bool adventure_intro_check(void);
#endif
