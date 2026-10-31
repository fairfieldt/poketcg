#include <libdragon.h>
#include "audio.h"
#include "../../../audio/generated/audio_assets.h"
static wav64_t music,sfx;
static bool ready,music_open,sfx_open,pause_valid;
static unsigned music_id,sfx_priority=255,pause_music_id;
static double pause_position;
extern void game_draw(void);
extern void game_tick(void);

#if ADVENTURE_DIAGNOSTIC == 14
static struct {
 uint32_t last_poll,max_gap,max_mix;
 unsigned polls,buffers,underruns;
 bool was_active,starved;
} review;
void adventure_audio_review_reset(void)
{review=(typeof(review)){0};}
void adventure_audio_review_report(const char *phase)
{
 debugf("AUDIO REVIEW phase=%s polls=%u buffers=%u underruns=%u max_poll_us=%u max_mix_us=%u\n",
  phase,review.polls,review.buffers,review.underruns,
  (unsigned)TICKS_TO_US(review.max_gap),(unsigned)TICKS_TO_US(review.max_mix));
}
#endif

void adventure_audio_init(void)
{
 audio_init(32000,8);mixer_init(8);
 mixer_ch_set_vol(0,.65f,.65f);mixer_ch_set_vol(1,.65f,.65f);
 mixer_ch_set_vol(2,.8f,.8f);mixer_ch_set_vol(3,.8f,.8f);ready=true;
}
void adventure_audio_poll(void)
{
 game_tick();
 if(!ready)return;
#if ADVENTURE_DIAGNOSTIC == 14
 uint32_t now=TICKS_READ();
 if(review.last_poll&&now-review.last_poll>review.max_gap)review.max_gap=now-review.last_poll;
 review.last_poll=now;review.polls++;
 bool active=(music_open&&mixer_ch_playing(0))||(sfx_open&&mixer_ch_playing(2));
 /* AI busy is actual hardware queue state, not the count of writable ring
    slots. Ignore startup/intentional silence and count each drained spell once. */
 bool starved=active&&review.was_active&&
  !(*(volatile uint32_t *)0xa450000c&(1u<<30));
 if(starved&&!review.starved)review.underruns++;
 review.was_active=active;review.starved=starved;
#endif
 while(audio_can_write()) {
  int16_t *buffer=audio_write_begin();
#if ADVENTURE_DIAGNOSTIC == 14
  uint32_t mixed=TICKS_READ();
#endif
  mixer_poll(buffer,audio_get_buffer_length());audio_write_end();
#if ADVENTURE_DIAGNOSTIC == 14
  mixed=TICKS_READ()-mixed;if(mixed>review.max_mix)review.max_mix=mixed;
  review.buffers++;
#endif
 }
}
static void stop_music(void)
{
 mixer_ch_stop(0);mixer_ch_stop(1);
 if(music_open){wav64_close(&music);music_open=false;}
 music_id=0;
}
static void play_music(unsigned id)
{
 if(id==music_id && music_open && mixer_ch_playing(0))return;
 stop_music();if(!id||id>=31)return;
 wav64_open(&music,tcg_music_paths[id]);music_open=true;music_id=id;wav64_play(&music,0);
}
static void play_sfx(unsigned id)
{
 if(id>=96)return;
 unsigned priority=tcg_sfx_priorities[id];
 if(id && mixer_ch_playing(2)&&priority>sfx_priority)return;
 mixer_ch_stop(2);mixer_ch_stop(3);
 if(sfx_open){wav64_close(&sfx);sfx_open=false;}
 if(!id)return;
 wav64_open(&sfx,tcg_sfx_paths[id]);sfx_open=true;sfx_priority=priority;wav64_play(&sfx,2);
}
void adventure_audio_event(unsigned kind,unsigned id)
{
 if(!ready)return;
 switch(kind) {
 case 0:case 0x48:play_music(id);break;
 case 1:case 0x49:play_sfx(id);break;
 case 0x4a:
  if(music_open&&!pause_valid){pause_position=mixer_ch_get_pos(0);pause_music_id=music_id;mixer_ch_stop(0);mixer_ch_stop(1);pause_valid=true;}
  break;
 case 0x4b:
  if(pause_valid){play_music(pause_music_id);mixer_ch_set_pos(0,pause_position);pause_valid=false;}
  break;
 case 0x4d:
  if(music_open&&!music.wave.loop_len)while(mixer_ch_playing(0))game_draw();
  break;
 }
}
