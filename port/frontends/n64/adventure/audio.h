#ifndef ADVENTURE_AUDIO_H
#define ADVENTURE_AUDIO_H
#include <stdbool.h>
void adventure_audio_init(void);
void adventure_audio_poll(void);
bool adventure_audio_music_playing(void);
/* Diagnostic 23 covers world music paused by PC music paused by a booster. */
void adventure_audio_nested_regression(void);
void adventure_audio_event(unsigned kind,unsigned id);
/* Diagnostic 17 exercises compressed music across source-script fanfares. */
void adventure_audio_regression(void);
/* Diagnostic 14 exercises the production intro without controller input. */
void adventure_audio_review(void);
#if ADVENTURE_DIAGNOSTIC == 14
void adventure_audio_review_reset(void);
void adventure_audio_review_report(const char *phase);
#endif
#endif
