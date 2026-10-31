/* Exercise the actual frontend latch/consume path with a physical snapshot
 * source, independently of display timing and the duel/RNG implementation. */
#define TCGN64_HOST_TEST
#define main tcgn64_frontend_main
#include "../tcgn64.c"
#undef main

static joypad_inputs_t physical[2];
static bool connected[2]={true,true};
void joypad_poll(void) {}
bool joypad_is_connected(joypad_port_t port) {return connected[port];}
joypad_inputs_t joypad_get_inputs(joypad_port_t port) {return physical[port];}

int main(void)
{
 input_reset();
 /* A complete tap between two renders survives both release and a second
    passive redraw (the event-text -> ACK transition that used to eat it). */
 physical[0].btn.a=true;input_sample();
 physical[0].btn.a=false;input_sample();
 input_poll();input_poll();assert(keys(1).a);
 input_poll();assert(!keys(1).a);
 /* One press cannot acknowledge the next message while still held. */
 physical[0].btn.a=true;input_poll();assert(keys(1).a);
 for(unsigned i=0;i<20;i++){input_poll();assert(!keys(1).a);}
 physical[0].btn.a=false;input_poll();assert(!keys(1).a);
 physical[0].btn.a=true;input_poll();assert(keys(1).a);
 /* Only the answering controller acts; an off-turn input cannot be saved
    until the other player's future turn. Start is read exactly once. */
 physical[0]=(joypad_inputs_t){0};physical[1].btn.a=true;input_poll();
 assert(!keys(1).a);assert(!keys(2).a);
 physical[1].btn.a=false;input_poll();keys(3);
 physical[1].btn.start=true;input_poll();assert(keys(2).start);assert(!keys(2).start);
 /* The initial D-pad edge works immediately; a short tap is not required
    to last into the SDK's second held snapshot. */
 physical[0].btn.d_down=true;input_sample();physical[0].btn.d_down=false;input_sample();
 input_poll();assert(keys(1).down);input_poll();assert(!keys(1).down);
 physical[1].btn.b=true;input_sample();connected[1]=false;input_poll();
 assert(!keys(2).b);
 /* A VI may arrive after the rendering poll but before consumption. */
 connected[1]=true;memset(physical,0,sizeof(physical));input_reset();input_poll();
 physical[1].btn.a=true;input_sample();assert(!keys(1).a);
 input_poll();assert(!keys(2).a);
 physical[0].btn.a=true;input_sample();assert(keys(1).a);
 input_poll();assert(!keys(1).a);
 physical[0].btn.d_down=true;input_sample();assert(keys(1).down);
 input_poll();assert(!keys(1).down);
 /* A complete tap queued before the first card presentation cannot dismiss
    it. A later fresh press can; the captured edge is still consumed once. */
 memset(physical,0,sizeof(physical));input_reset();DuelFreshConfirm card={0};
 physical[0].btn.a=true;input_sample();physical[0].btn.a=false;input_sample();input_poll();
 Keys before=keys(1);assert(before.a);
 assert(!duel_fresh_confirm_step(&card,confirm_buttons_held(1)));
 input_poll();assert(!keys(1).a);assert(duel_fresh_confirm_step(&card,confirm_buttons_held(1)));
 physical[0].btn.a=true;input_poll();
 assert(duel_fresh_confirm_step(&card,confirm_buttons_held(1))&&keys(1).a);assert(!keys(1).a);
 /* A confirm held through presentation must first release. This does not
    change the independent held-B animation fast-forward policy. */
 card=(DuelFreshConfirm){0};input_reset();input_poll();keys(1);
 assert(!duel_fresh_confirm_step(&card,confirm_buttons_held(1)));
 for(unsigned i=0;i<5;i++){input_poll();keys(1);assert(!duel_fresh_confirm_step(&card,confirm_buttons_held(1)));}
 physical[0].btn.a=false;input_poll();keys(1);assert(!duel_fresh_confirm_step(&card,confirm_buttons_held(1)));
 physical[0].btn.a=true;input_poll();assert(duel_fresh_confirm_step(&card,confirm_buttons_held(1))&&keys(1).a);
 /* A queued placement confirmation must not answer the following actions. */
 DuelFreshConfirm actions={0};assert(!duel_fresh_confirm_step(&actions,confirm_buttons_held(1)));
 physical[0].btn.a=false;input_poll();keys(1);assert(!duel_fresh_confirm_step(&actions,confirm_buttons_held(1)));
 physical[0].btn.a=true;input_poll();assert(duel_fresh_confirm_step(&actions,confirm_buttons_held(1))&&keys(1).a);
 puts("FRONTEND INPUT PASS: tap survives slow/passive redraws; one press/one action; held ACK safe; ports/start/dpad/unplug");
 return 0;
}
