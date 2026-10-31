#ifndef ADVENTURE_AUDIO_H
#define ADVENTURE_AUDIO_H
void adventure_audio_init(void);
void adventure_audio_poll(void);
void adventure_audio_event(unsigned kind,unsigned id);
/* Diagnostic 14 exercises the production intro without controller input. */
void adventure_audio_review(void);
#if ADVENTURE_DIAGNOSTIC == 14
void adventure_audio_review_reset(void);
void adventure_audio_review_report(const char *phase);
#endif
#endif
