#ifndef ADVENTURE_FPS_SWEEP_H
#define ADVENTURE_FPS_SWEEP_H
#include <libdragon.h>
/* Use a production frame function with persistent assets. A callback may
   present/poll audio itself, or return a fenced still-owned display surface;
   diagnostic linker wrappers detect presentation and fill only missing work. */
typedef surface_t *(*FpsSweepDraw)(void *ctx,unsigned frame);
void fps_sweep_case(const char *name,FpsSweepDraw draw,void *ctx);
/* Optional nested, non-overlapping diagnostic substage within a frame. */
void fps_sweep_note(const char *name,uint64_t ticks);
void adventure_fps_survey(void);
/* Implemented beside the corresponding private production draw functions. */
void adventure_intro_fps_survey(void);
void adventure_naming_fps_survey(void);
void adventure_dialogue_fps_survey(const char *prefix);
#endif
