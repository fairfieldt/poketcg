/* Native UI fixture using actual source texts and checklist/speech events.
   Engine ordering/state is checked separately by harness/practice_check.c. */
#define TCGN64_ADVENTURE
#define TCGN64_CARD_ART
#define ADVENTURE_DIAGNOSTIC 9
#include "../../frontends/n64/tcgn64.c"
#include "../../frontends/n64/adventure/video.c"
#include "../../src/duel_ui.h"
void adventure_audio_poll(void) {}
void adventure_audio_event(unsigned kind,unsigned id) {(void)kind;(void)id;}
static int expected_mode,bridge_cases;
int __wrap_tcg_run_duel(const TcgDuelSetup *setup)
{
 (void)setup;assertf(g_mode==expected_mode,"Adventure selected the wrong duel presentation mode");
 bridge_cases++;return 0;
}

static void verify_bridge_modes(void)
{
 TcgDuelSetup setup={0};setup.duelist[0].name="Mark";setup.duelist[1].name="Sam";
 setup.duelist[1].kind=TCG_AI;setup.duelist[1].deck_id=TCG_SAMS_PRACTICE_DECK_ID;
 TcgDuelSetup unchanged=setup;expected_mode=MODE_PRACTICE;n64_duel_play(&setup,NULL);
 assertf(!memcmp(&setup,&unchanged,sizeof(setup)),"Adventure changed practice setup");
 setup.duelist[1].deck_id=SAMS_NORMAL_DECK_ID;expected_mode=MODE_VS_AI;n64_duel_play(&setup,NULL);
 setup.duelist[1].kind=TCG_HUMAN;expected_mode=MODE_HOTSEAT;n64_duel_play(&setup,NULL);
 assertf(bridge_cases==3,"Adventure mode cases did not run");
 debugf("PRACTICE BRIDGE MODE PASS cases=3\n");
}

static void capture(const char *label)
{
 surface_t *frame=g_last_ui_frame;const uint16_t *pixels=UncachedAddr(frame->buffer);
 debugf("FRAMEBUFFER %s-%u %u %u\n",label,frame->width,frame->width,frame->height);
 for(unsigned y=0;y<frame->height;y++)for(unsigned x=0;x<frame->width;x+=32) {
  char text[129];unsigned count=frame->width-x;if(count>32)count=32;
  for(unsigned i=0;i<count;i++)snprintf(text+i*4,5,"%04x",pixels[y*(frame->stride/2)+x+i]);
  debugf("PIXELS %u %u %s\n",x,y,text);
 }
 debugf("FRAMEBUFFER END\n");
}
static void draw(void)
{begin_frame();draw_practice_reading(0);draw_practice_continue(0);end_frame();}

int main(void)
{
 debug_init_isviewer();debug_init_usblog();timer_init();dfs_init(DFS_DEFAULT_LOCATION);rdpq_init();joypad_init();
 static const u16 steps[]={Turn1Instr1PracticeDuelText,Turn1Instr2PracticeDuelText,Turn1Instr3PracticeDuelText};
 static const u16 speech[]={Turn1DrMason1PracticeDuelText,Turn1DrMason3PracticeDuelText,LetsPlayTheGamePracticeDuelText};
 static const unsigned counts[]={0,2,3};static const char *const labels[]={"mason-long","mason-finally","mason-recap"};
 TcgHost host={on_event,NULL,NULL,NULL};tcg_set_host(&host);
 for(unsigned high=0;high<2;high++) {
  display_init(n64_video_init(high),DEPTH_16_BPP,3,GAMMA_NONE,FILTERS_RESAMPLE);
  g_raster_width=n64_video_width;g_ui_canvas=n64_video_surface();
  if(!high)verify_bridge_modes();
  for(unsigned i=0;i<NUM_COLORS;i++)g_col[i]=graphics_make_color(kRGB[i][0],kRGB[i][1],kRGB[i][2],255);
  copy_str(g_name[0],sizeof(g_name[0]),"Mark");copy_str(g_name[1],sizeof(g_name[1]),"Sam");
  gDuelistNames[0]="Mark";gDuelistNames[1]="Sam";
  g_kind[0]=PK_HUMAN;g_kind[1]=PK_AI;g_mode=MODE_PRACTICE;
  g_turn_holder=TCG_PLAYER;g_result=-1;g_screen=TCG_SCREEN_EMPTY;
  hWhoseTurn=PLAYER_TURN;wWhoseTurn=PLAYER_TURN;wDuelTurns=0;
  LoadTxRam3(1);wr16(wTextBoxLabel,DrMasonText);log_clear();
  tcg_set_host(&host);
  for(unsigned stage=0;stage<3;stage++) {
   debugf("PRACTICE READING %s-%u BEGIN\n",labels[stage],n64_video_width);
   tcg_ui_practice_checklist(PlayersTurnPracticeDuelText,steps,counts[stage]);
   tcg_ui_practice_speech(speech[stage]);
   assertf(practice_reading_visible(),"Practice reading mode was not selected");
   for(unsigned frame=0;frame<24;frame++)draw();
   capture(labels[stage]);
  }
  rspq_wait();surface_free(&g_ui_canvas);display_close();
 }
 debugf("PRACTICE READING REVIEW PASS\n");for(;;)wait_ms(10);
}
