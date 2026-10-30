/* Weak fallbacks for the AI entry points the core calls, so the engine links
 * (and human-vs-human duels run) before/without the AI port. */
#include "engine.h"

#define WEAK __attribute__((weak))

WEAK void LoadOpponentDeck(void) { tcg_fatal("AI not ported: LoadOpponentDeck"); }
WEAK u8 AIDoAction_Turn(void) { tcg_fatal("AI not ported: AIDoAction_Turn"); return 0; }
WEAK u8 AIDoAction_StartDuel(void) { tcg_fatal("AI not ported: AIDoAction_StartDuel"); return 0; }
WEAK u8 AIDoAction_ForcedSwitch(void) { tcg_fatal("AI not ported: AIDoAction_ForcedSwitch"); return 0; }
WEAK u8 AIDoAction_KOSwitch(void) { tcg_fatal("AI not ported: AIDoAction_KOSwitch"); return 0; }
WEAK u8 AIDoAction_TakePrize(void) { tcg_fatal("AI not ported: AIDoAction_TakePrize"); return 0; }
WEAK u8 AIDoAction(u8 action) { (void)action; tcg_fatal("AI not ported: AIDoAction"); return 0; }
