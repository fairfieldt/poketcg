#include <libdragon.h>
#include "audio.h"
#include "../../../audio/generated/audio_assets.h"
typedef struct {wav64_t wave;unsigned id;bool open;} MusicSlot;
/* Keep a paused compressed decoder and its mixer samplebuffer intact. Source
   fanfares use the other stereo pair; VADPCM cannot seek to an arbitrary
   saved sample after closing/reopening a song. Pair 2/3 belongs to SFX. */
static MusicSlot music[2];
static unsigned active_music;
static int paused_music=-1;
static wav64_t sfx;
static bool ready,sfx_open;
static unsigned sfx_priority=255;
static unsigned music_channel(unsigned slot){return slot?4:0;}
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
 mixer_ch_set_vol(0,.65f,.65f);mixer_ch_set_vol(4,.65f,.65f);
 mixer_ch_set_vol(2,.8f,.8f);ready=true;
}
void adventure_audio_poll(void)
{
 game_tick();
 if(!ready)return;
#if ADVENTURE_DIAGNOSTIC == 14
 uint32_t now=TICKS_READ();
 if(review.last_poll&&now-review.last_poll>review.max_gap)review.max_gap=now-review.last_poll;
 review.last_poll=now;review.polls++;
 bool active=(music[active_music].open&&mixer_ch_playing(music_channel(active_music)))||
  (sfx_open&&mixer_ch_playing(2));
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
static void stop_music(unsigned slot)
{
 MusicSlot *m=&music[slot];unsigned channel=music_channel(slot);
 mixer_ch_stop(channel);mixer_ch_stop(channel+1);
 if(m->open){wav64_close(&m->wave);m->open=false;}
 m->id=0;
}
static void play_music(unsigned id)
{
 MusicSlot *m=&music[active_music];unsigned channel=music_channel(active_music);
 if(id==m->id&&m->open&&mixer_ch_playing(channel))return;
 stop_music(active_music);if(!id||id>=31)return;
 wav64_open(&m->wave,tcg_music_paths[id]);m->open=true;m->id=id;
 mixer_ch_set_vol(channel,.65f,.65f);wav64_play(&m->wave,channel);
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
  if(music[active_music].open&&paused_music<0){
   unsigned channel=music_channel(active_music);
   /* A zero playback rate holds the exact fractional sample and decoder
      cache. Muting the master also mutes its stereo secondary channel. */
   mixer_ch_set_vol(channel,0,0);mixer_ch_set_freq(channel,0);
   paused_music=(int)active_music;active_music^=1;
  }
  break;
 case 0x4b:
  if(paused_music>=0){
   stop_music(active_music);active_music=(unsigned)paused_music;paused_music=-1;
   unsigned channel=music_channel(active_music);
   mixer_ch_set_freq(channel,music[active_music].wave.wave.frequency);
   mixer_ch_set_vol(channel,.65f,.65f);
  }
  break;
 case 0x4d:
  if(music[active_music].open&&!music[active_music].wave.wave.loop_len)
   while(mixer_ch_playing(music_channel(active_music)))game_draw();
  break;
 }
}

#if ADVENTURE_DIAGNOSTIC == 17
/* Exercise the real mixer/RSP decoder, without waiting for AI playback time. */
static int16_t regression_pcm[2048*2] __attribute__((aligned(16)));
static void regression_mix(unsigned frames)
{
 while(frames){unsigned count=frames>2048?2048:frames;
  mixer_poll(regression_pcm,count);frames-=count;}
}
static void regression_reward_point(void)
{
 unsigned ch=music_channel(active_music);
 assertf(music[active_music].id==9&&paused_music<0,"Reward did not start music09");
 while(mixer_ch_get_pos(ch)<652536)regression_mix(1024);
 /* Rewind inside the most recently decoded block to reproduce the exact
    sample in the reported crash; this requires no compressed-stream seek. */
 mixer_ch_set_pos(ch,652536);
 debugf("AUDIO RESUME REPRO pause=%u\n",(unsigned)mixer_ch_get_pos(ch));
}
static double regression_position(unsigned slot)
{
 double pos=mixer_ch_get_pos(music_channel(slot));waveform_t *w=&music[slot].wave.wave;
 /* The mixer may normalize its absolute loop coordinates while frozen. */
 while(w->loop_len&&pos>=w->len)pos-=w->loop_len;
 return pos;
}
static void regression_assets(void)
{
 stop_music(0);stop_music(1);play_sfx(0);
 for(unsigned kind=0;kind<2;kind++)for(unsigned id=0;id<(kind?96u:31u);id++){
  wav64_t wave;const char *path=kind?tcg_sfx_paths[id]:tcg_music_paths[id];
  wav64_open(&wave,path);wav64_play(&wave,6);
  /* Include the first decoder read after each legal loop seek. Finite clips
     also pass their real end, exercising the zeroed Huffman lookahead tail. */
  uint64_t samples=(uint64_t)(wave.wave.len+1024)*audio_get_frequency();
  unsigned frames=(unsigned)((samples+wave.wave.frequency-1)/wave.wave.frequency);
  regression_mix((frames+1)&~1u);
  /* RSP clamps a finite voice at its end; the next CPU poll releases it. */
  regression_mix(2);
  assertf(wave.wave.loop_len||!mixer_ch_playing(6),
   "Finite WAV64 did not finish: %s pos%u len%u frames%u frequency%u DAC%u",
   path,(unsigned)mixer_ch_get_pos(6),wave.wave.len,frames,(unsigned)wave.wave.frequency,audio_get_frequency());
  mixer_ch_stop(6);mixer_ch_stop(7);wav64_close(&wave);
  debugf("AUDIO ASSET DECODE PASS kind=%u id=%u\n",kind,id);
 }
 debugf("AUDIO ALL ASSETS REGRESSION PASS: 31 songs, 96 SFX, real RSP decoder, finite tails and loop starts\n");
}
void adventure_audio_regression(void)
{
 extern bool adventure_audio_reward_diagnostic(void (*)(void));
 extern unsigned adventure_audio_memory_diagnostic(void);
 unsigned warm_heap=0;
 regression_assets();
 adventure_audio_event(0,9);
 regression_reward_point();
 wav64_t *retained=&music[active_music].wave;
 uint32_t uuid=retained->wave.__uuid;
 adventure_audio_event(0x4a,0);adventure_audio_event(0,28);
 regression_mix(32000*8);
 assertf(mixer_ch_get_pos(music_channel((unsigned)paused_music))==652536,
  "Paused decoder advanced during fanfare");
 adventure_audio_event(0x4b,0);
 assertf(&music[active_music].wave==retained&&retained->wave.__uuid==uuid&&
  mixer_ch_get_pos(music_channel(active_music))==652536,"Resume replaced or sought decoder");
 regression_mix(2048);
 debugf("AUDIO EXACT SEEK REGRESSION PASS: resumed sample652536 without reopening\n");
 /* Decode through the original end and its legal loop skip point. */
 regression_mix(32000*90);
 assertf(mixer_ch_playing(music_channel(active_music)),"Music09 did not survive loop");
 debugf("AUDIO LOOP REGRESSION PASS: music09 90 seconds past resume\n");
 for(unsigned i=0;i<24;i++){
  unsigned held=active_music;double pos=regression_position(held);
  adventure_audio_event(0x4a,0);adventure_audio_event(0x4a,0);
  adventure_audio_event(1,1);adventure_audio_event(0,28);
  adventure_audio_event(0,28);regression_mix(32000*2);
  assertf(paused_music==(int)held&&regression_position(held)==pos,"Nested pause lost background");
  adventure_audio_event(0,0);adventure_audio_event(0,9);regression_mix(1024);
  adventure_audio_event(0x4b,0);adventure_audio_event(0x4b,0);
  assertf(active_music==held&&paused_music<0&&!music[held^1].open&&
   regression_position(held)==pos,"Repeated resume leaked or replaced music");
  regression_mix(2048);
  heap_stats_t heap;sys_get_heap_stats(&heap);
  if(!i)warm_heap=heap.used;
  else assertf(heap.used<=warm_heap+256,"Repeated fanfares leaked heap: %d vs %u",heap.used,warm_heap);
 }
 debugf("AUDIO HEAP STABILITY PASS: warm_used=%u across24 fanfares\n",warm_heap);
 adventure_audio_event(1,0);adventure_audio_event(0,28);regression_mix(32000*2);
 assertf(!mixer_ch_playing(music_channel(active_music)),"Finite song did not finish");
 adventure_audio_event(0x4a,0);adventure_audio_event(0,9);regression_mix(1024);
 adventure_audio_event(0x4b,0);
 assertf(!mixer_ch_playing(music_channel(active_music)),"Resume restarted an ended song");
 adventure_audio_event(0,0);adventure_audio_event(0x4a,0);adventure_audio_event(0x4b,0);
 assertf(paused_music<0&&!music[0].open&&!music[1].open,"Silence retained music handles");
 debugf("AUDIO OWNERSHIP REGRESSION PASS: 24 fanfares, nested pause, SFX, replacement, silence, finite end\n");
 assertf(adventure_audio_reward_diagnostic(regression_reward_point),"Source reward failed");
 assertf(paused_music<0&&music[active_music].id==9&&!music[active_music^1].open,
  "Source reward left a paused song/fanfare");
 regression_mix(32000*90);
 adventure_audio_event(0x4a,0);adventure_audio_event(0,9);adventure_audio_event(1,1);
 assertf(adventure_audio_memory_diagnostic()>128*1024,"Audio retained state exhausted scene headroom");
 adventure_audio_event(0x4b,0);adventure_audio_event(1,0);
 debugf("AUDIO RESUME REGRESSION PASS\n");
}
#endif
