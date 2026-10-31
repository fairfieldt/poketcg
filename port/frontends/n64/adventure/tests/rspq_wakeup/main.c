#include <libdragon.h>
#include <stdint.h>

static unsigned callbacks;
static void completed(void *ctx)
{unsigned expected=(unsigned)(uintptr_t)ctx;assertf(++callbacks==expected,"Queue callback order changed");}

int main(void)
{
 debug_init_isviewer();debug_init_usblog();timer_init();rspq_init();
 debugf("RSPQ WAKE STRESS BEGIN: normal/highpri wraps, idle wakes, ordered callbacks\n");
 for(unsigned i=0;i<4096;i++){
  /* Cross the normal512-word and highpri128-word buffer boundaries at varied
     offsets. Each callback is a real queued syncpoint, checked in order. */
  unsigned words=(i*137+73)%900;
  for(unsigned j=0;j<words;j++)rspq_noop();
  for(unsigned batch=0;batch<1+(i&3);batch++){
   rspq_highpri_begin();
   for(unsigned j=0;j<3+(i*17+batch*41)%180;j++)rspq_noop();
   rspq_highpri_end();
  }
  rspq_syncpoint_new_cb(completed,(void *)(uintptr_t)(i+1));
  if(!(i&7)){rspq_highpri_sync();rspq_wait();}
  if(!(i&31)){
   /* Let the RSP reach WaitNewInput, then issue and wait for new lowpri work. */
   rspq_wait();wait_ticks(400+(i%19)*29);rspq_noop();rspq_wait();
  }
  if((i&511)==511)debugf("RSPQ WAKE STRESS progress=%u callbacks=%u\n",i+1,callbacks);
 }
 rspq_highpri_sync();rspq_wait();
 assertf(callbacks==4096,"Missing queue callbacks: %u",callbacks);
 debugf("RSPQ WAKE STRESS PASS callbacks=%u\n",callbacks);
 for(;;){}
}
