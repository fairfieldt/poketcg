/* Port of the practice duel with Sam (engine/duel/core.asm DoPracticeDuelAction table).
 * Not ported yet: the port refuses to start a practice duel (wIsPracticeDuel != 0). */
#include "engine.h"

bool PracticeDuelAction(u8 action)
{
	(void)action;
	tcg_fatal("the practice duel is not supported yet");
	return false;
}
