#include "../src/gen/constants.h"
#include "../src/gen/text.h"
#include "gen/data.h"
#include "overworld.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
  unsigned text_count, duels, features, gifts, starter;
  int outcome;
  unsigned collection;
  unsigned starter_choice, starter_choices, confirmations, reward_messages;
  bool reselect;
  uint8_t saved[2048];
  size_t saved_size;
} Test;
static const uint16_t starter_texts[] = {CharmanderAndFriendsDeckChoiceText,
    SquirtleAndFriendsDeckChoiceText, BulbasaurAndFriendsDeckChoiceText};
static void text(void *ctx, OwState *s, uint16_t id, uint8_t npc) {
  (void)s;
  (void)npc;
  assert(id);
  Test *t=ctx;t->text_count++;
  if(id==Text05f4||id==Text05f5){
    assert(s->starter<3&&s->txram[0]==starter_texts[s->starter]);
    if(id==Text05f5)t->reward_messages++;
  }
}
static int choice(void *ctx, OwState *s, unsigned kind, uint16_t id,
                  unsigned count, unsigned initial) {
  Test *t=ctx;
  (void)count;
  (void)initial;
  if(kind==OW_CHOICE_STARTER)
    return t->reselect&&t->starter_choices++ ? 2 : (int)t->starter_choice;
  if(kind==OW_CHOICE_YES_NO&&id==Text05f3){
    assert(s->starter<3&&s->txram[0]==starter_texts[s->starter]);
    return !t->reselect||t->confirmations++>0;
  }
  return kind == OW_CHOICE_YES_NO      ? 1
         : kind == OW_CHOICE_SAM       ? SAM_MENU_NOTHING
         : kind == OW_CHOICE_SAM_RULES ? SAM_MENU_NOTHING_TO_ASK
                                       : 0;
}
static int duel(void *ctx, OwState *s, const OwDuel *d) {
  Test *t = ctx;
  t->duels++;
  assert(d->prizes >= 2 && d->prizes <= 6);
  assert(s->duel_pending);
  t->saved_size = ow_save_size();
  assert(t->saved_size <= sizeof(t->saved));
  assert(ow_save_encode(s, t->saved, sizeof(t->saved)));
  return t->outcome;
}
static bool feature(void *ctx, OwState *s, unsigned f, unsigned a) {
  (void)s;
  (void)f;
  (void)a;
  ((Test *)ctx)->features++;
  return true;
}
static void gift(void *ctx, unsigned c, int delta) {
  (void)c;
  Test *t = ctx;
  if (delta > 0)
    t->gifts += (unsigned)delta;
}
static unsigned card_count(void *ctx, unsigned c, bool decks) {
  (void)c;
  (void)decks;
  return ((Test *)ctx)->collection;
}
static unsigned total(void *ctx) {
  (void)ctx;
  return 500;
}
static void starter(void *ctx, unsigned choice) {
  assert(choice < 3);
  ((Test *)ctx)->starter++;
}
static void booster(void *ctx, unsigned id) {
  assert(id < 29);
  ((Test *)ctx)->gifts += 10;
}
static OwHost host(Test *t) {
  OwHost h = {.ctx = t,
              .text = text,
              .choice = choice,
              .duel = duel,
              .feature = feature,
              .card_count = card_count,
              .card_change = gift,
              .total_cards = total,
              .starter = starter,
              .booster = booster};
  return h;
}
int main(void) {
  Test t = {.outcome = 1, .collection = 4};
  OwHost h = host(&t);
  OwState s;
  for(unsigned selected=0;selected<4;selected++){
    Test names={.outcome=1,.collection=4,.starter_choice=selected%3,
                .reselect=selected==3};
    OwHost names_host=host(&names);OwState named;
    ow_new_game(&named,&names_host,123);
    assert(!named.error[0]&&names.starter==1&&names.reward_messages==1);
    assert(named.starter==(selected==3?2:selected));
    if(selected==3)assert(names.confirmations==2&&names.starter_choices==2);
  }
  puts("Starter text PASS: all three complete deck names, confirmation/reward and reselection");
  ow_new_game(&s, &h, 123);
  assert(!s.error[0]);
  assert(s.map == MASON_LABORATORY);
  assert(ow_event(&s, EVENT_MASON_LAB_STATE) ==
         MASON_LAB_RECEIVED_STARTER_DECK);
  assert(t.duels == 1 && t.starter == 1);
  OwState restored;
  assert(ow_save_decode(&restored, t.saved, t.saved_size));
  assert(restored.duel_pending);
  ow_finish_duel(&restored, &h, 1);
  assert(!restored.error[0]);
  assert(ow_event(&restored, EVENT_MASON_LAB_STATE) ==
         MASON_LAB_RECEIVED_STARTER_DECK);
  assert(!restored.duel_pending);
  unsigned failures = 0, encounters = 0;
  for (unsigned stage = 0; stage < 3; stage++)
    for (unsigned map = 1; map < 34; map++) {
      OwState base;
      memset(&base, 0, sizeof(base));
      base.version = OW_SAVE_VERSION;
      base.rng = 0x3157;
      base.overworld_selection = 1;
      ow_set_event(&base, EVENT_MASON_LAB_STATE, 3);
      if (stage >= 1) {
        for (unsigned e = EVENT_BEAT_NIKKI; e <= EVENT_BEAT_MITCH; e++)
          ow_set_event(&base, e, 1);
        ow_set_event(&base, EVENT_MEDAL_COUNT, 8);
        ow_set_event(&base, EVENT_BEAT_JENNIFER, 1);
        ow_set_event(&base, EVENT_BEAT_NICHOLAS, 1);
        ow_set_event(&base, EVENT_BEAT_BRANDON, 1);
        ow_set_event(&base, EVENT_PUPIL_MICHAEL_STATE, PUPIL_DEFEATED);
        ow_set_event(&base, EVENT_PUPIL_CHRIS_STATE, PUPIL_DEFEATED);
        ow_set_event(&base, EVENT_PUPIL_JESSICA_STATE, PUPIL_DEFEATED);
        ow_set_event(&base, EVENT_NIKKI_STATE, NIKKI_IN_GRASS_CLUB);
      }
      if (stage == 2)
        ow_set_event(&base, EVENT_RECEIVED_LEGENDARY_CARDS, 1);
      ow_load_map(&base, &h, map, 8, 8, NORTH);
      if (base.error[0]) {
        printf("FAIL load stage%u map%u: %s\n", stage, map, base.error);
        failures++;
        continue;
      }
      for (unsigned i = 0; i < base.npc_count; i++)
        for (unsigned outcome = 0; outcome < 2; outcome++) {
          s = base;
          t.outcome = (int)outcome;
          unsigned id = s.npcs[i].id;
          ow_run(&s, &h, ow_script_address("Script_Clerk13"),
                 id); /* Guard simple text callback before the actual header
                         entry. */
          ow_run(&s, &h, ow_npc_headers[id].script, id);
          ow_resume(&s, &h);
          encounters++;
          if (s.error[0]) {
            printf("FAIL npc stage%u map%u id%u outcome%u: %s\n", stage, map,
                   id, outcome, s.error);
            failures++;
          }
        }
    }
  printf("Story: intro/practice/starter/save-resume; %u NPC-state encounters; "
         "%u failures\n",
         encounters, failures);
  return failures ? 1 : 0;
}
