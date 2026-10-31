#ifndef TCG_FRONTEND_CONTROL_TEXT_H
#define TCG_FRONTEND_CONTROL_TEXT_H
#include <stddef.h>
#include <stdint.h>
#include "gen/text.h"

enum TcgControlLayout { TCG_CONTROLS_N64, TCG_CONTROLS_WEB };

/* Exact control instructions only. Preserve ordinary uses of "select", card
 * rules, story text and the generated source strings. */
static inline const char *tcg_control_text(enum TcgControlLayout layout,uint16_t id)
{
	if (layout == TCG_CONTROLS_N64) {
		switch (id) {
		case SamTurn4Instr2PracticeDuelText:
			return "2. Press Z to check Pokemon data.";
		case SamTurn4DrMason2PracticeDuelText:
			return "Press Z to check Pokemon data.\n"
			       "Check your cards and the status\n"
			       "of your Pokemon before choosing.";
		case UsefulButtonsBookText:
			return "During a duel, press Z to check\n"
			       "play areas and discard piles.\n"
			       "Use L/R to switch views.\n"
			       "Press A to view a card, then\n"
			       "press B to go back.\n\n"
			       "START shows the highlighted card.\n"
			       "From the action menu, use Up to\n"
			       "browse Pokemon and A to inspect.";
		default: return NULL;
		}
	}
	if (layout == TCG_CONTROLS_WEB) {
		switch (id) {
		case SamTurn4Instr2PracticeDuelText:
			return "2. Press C to view Pokemon data.";
		case SamTurn4DrMason2PracticeDuelText:
			return "Press C to view Pokemon data.\n"
			       "Check your cards and the status\n"
			       "of your Pokemon before choosing.";
		case UsefulButtonsBookText:
			return "During a duel, choose Check to\n"
			       "view play areas and discard piles.\n"
			       "On the duel board, click a Pokemon\n"
			       "to view its card.\n"
			       "You can also press C to view the\n"
			       "highlighted card.\n"
			       "Press X or Escape to go back.";
		default: return NULL;
		}
	}
	return NULL;
}
#endif
