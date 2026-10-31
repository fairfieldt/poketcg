#ifndef TCGN64_DUEL_INPUT_H
#define TCGN64_DUEL_INPUT_H
#include <stdbool.h>
#include <stdint.h>

enum { INPUT_A=1, INPUT_B=2, INPUT_Z=4, INPUT_START=8, INPUT_L=16, INPUT_R=32,
       INPUT_UP=64, INPUT_DOWN=128, INPUT_LEFT=256, INPUT_RIGHT=512 };
/* VI samples and render frames have separate lifetimes. An edge remains queued
 * after release until the UI consumes it, even through a slow redraw. */
typedef struct {
 uint16_t held[2],pending[2];
 bool connected[2];
} DuelInputCapture;

static inline void duel_input_sample(volatile DuelInputCapture *capture,unsigned port,
 uint16_t held,bool connected)
{
 if(!connected) {
  capture->held[port]=capture->pending[port]=0;capture->connected[port]=false;return;
 }
 capture->pending[port]|=held&~capture->held[port];
 capture->held[port]=held;capture->connected[port]=true;
}
static inline uint16_t duel_input_take(volatile DuelInputCapture *capture,unsigned port)
{
 uint16_t pending=capture->pending[port];capture->pending[port]=0;return pending;
}

/* Important new content needs a press after it has actually appeared. The
 * first completed presentation consumes old edges; a held confirm must be
 * released before a later edge can act. There is no fixed time delay. */
typedef struct {bool presented,armed;} DuelFreshConfirm;
static inline bool duel_fresh_confirm_step(DuelFreshConfirm *confirm,bool held)
{
 if(!confirm->presented){confirm->presented=true;confirm->armed=!held;return false;}
 if(!confirm->armed){if(!held)confirm->armed=true;return false;}
 return true;
}
#endif
