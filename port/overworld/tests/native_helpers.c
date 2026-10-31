/* Tests private native adapters at the boundary where story ASM uses RAM. */
#include "../../gameplay/gameplay.h"
#include "../overworld.c"
#include <assert.h>

static unsigned mail_ids[3], mail_count;
static bool mail(void *ctx, OwState *s, unsigned feature_id,
                 unsigned argument) {
  (void)ctx;
  (void)s;
  if (feature_id == OW_FEATURE_PC_PACK && mail_count < 3)
    mail_ids[mail_count++] = argument;
  return true;
}
int main(void) {
  OwState s = {0};
  Reg r = {0};
  for (unsigned i = 0; i < sizeof(native_fields) / sizeof(native_fields[0]);
       i++) {
    if (i == 2)
      continue; /* Console is a fixed host adapter. */
    for (unsigned n = 0; n < native_fields[i].width; n++) {
      r.hl = (uint16_t)(native_fields[i].address + n);
      mem_set(&s, &r, "[hli]", 42 + n);
      assert(r.hl == native_fields[i].address + n + 1);
      r.hl = (uint16_t)(native_fields[i].address + n);
      assert(mem_get(&s, &r, "[hld]") == 42 + n);
      assert(r.hl == native_fields[i].address + n - 1);
    }
  }
  memset(&s, 0, sizeof(s));
  ow_set_event(&s, EVENT_RECEIVED_LEGENDARY_CARDS, 1);
  s.native_vars[9] = 16;
  assert(native_run(&s, NULL, ow_script_address("Preload_Tech5"), &r));
  assert(r.carry && s.native_vars[9] == 18); /* Original inc[hl] twice. */
  s.challenge_history[0] = s.challenge_history[1] = s.challenge_history[2] =
      s.challenge_history[3] = 255;
  assert(native_run(&s, NULL, ow_script_address("Func_f5db"), &r));
  assert(!s.challenge_history[0] && !s.challenge_history[1] &&
         !s.challenge_history[2] && !s.challenge_history[3]);
  assert(native_run(&s, NULL, ow_script_address("Func_f5d4"), &r));
  assert(s.challenge_history[0] == 128); /* Cup opponent history is writable. */
  OwHost host = {0};
  host.feature = mail;
  const unsigned medals[] = {
      EVENT_BEAT_NIKKI, EVENT_BEAT_RICK,   EVENT_BEAT_KEN,  EVENT_BEAT_AMY,
      EVENT_BEAT_ISAAC, EVENT_BEAT_MURRAY, EVENT_BEAT_GENE, EVENT_BEAT_MITCH};
  for (unsigned i = 0; i < 8; i++)
    ow_set_event(&s, medals[i], 1);
  medal_packs(&s, &host);
  assert(ow_event(&s, EVENT_MEDAL_COUNT) == 8 && mail_count == 3);
  assert(mail_ids[0] == 12 && mail_ids[1] == 11 && mail_ids[2] == 10);
  GpState gp;
  for (unsigned seed = 0; seed < 256; seed++) {
    uint8_t rng[3] = {(uint8_t)seed, (uint8_t)(seed * 7), 93};
    gp_init(&gp, rng);
    s.rng = rng[0] | (uint32_t)rng[1] << 8 | (uint32_t)rng[2] << 16;
    for (unsigned bound = 1; bound < 256; bound++) {
      assert(random_value(&s, bound) == gp_random(&gp, (uint8_t)bound));
      assert(s.rng == (gp.rng[0] | (uint32_t)gp.rng[1] << 8 |
                       (uint32_t)gp.rng[2] << 16));
    }
  }
  puts("Native helpers OK: allRAM "
       "aliases/postinc,Tech5preload,Cuphistory,medalmailorder,65K exactRNG "
       "draws");
}
