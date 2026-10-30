/*
 * Routines (and constants) that effects/part4.c needs but that are not
 * declared in any shared header yet. To be provided by the lead.
 */
#ifndef TCG_EFFECTS_PART4_NEEDS_H
#define TCG_EFFECTS_PART4_NEEDS_H

#include "../common.h"

/*
 * WaitForButtonAorB (home/menus.asm), reached by fallthrough from
 * SetCursorParametersForTextBox_Default.
 * input:  the text box cursor set up by SetCursorParametersForTextBox
 *         (wCurMenuItem, wMenuCursorXOffset/YOffset, wMenuYSeparation,
 *         wNumMenuItems, wMenuVisibleCursorTile/InvisibleCursorTile).
 * Blocks (DoFrame + RefreshMenuCursor each frame, which updates
 * wCursorBlinkCounter) until A or B is pressed, then calls EraseCursor.
 * output: carry (true) if B was pressed, nc (false) if A was pressed
 *         (the asm comment above it says the opposite; the code returns
 *         carry on B). hKeysPressed holds the key.
 * Needed because core.asm's InitAndPrintPlayAreaCardInformationAndLocation_WithTextBox
 * returns this carry and DevolutionSpray_PlayerSelection uses it ("jr c" = B ends the
 * devolution selection), while its duel_ui.h shim returns void. part4.c therefore runs
 * that routine's body inline: InitAndPrintPlayAreaCardInformationAndLocation(),
 * SetCursorParametersForTextBox(0, wCurPlayAreaY, SYM_CURSOR_R, SYM_SPACE), WaitForButtonAorB().
 * (Alternative: make the duel_ui.h shim of ..._WithTextBox return this bool.)
 */
bool WaitForButtonAorB(void);

/* constants missing from gen/constants.h */
#ifndef SYM_SPACE
#define SYM_SPACE 0x00     /* src/constants/charmaps.asm, TX_SYMBOL table */
#endif
#ifndef SYM_CURSOR_R
#define SYM_CURSOR_R 0x0f
#endif
#ifndef PAD_A
#define PAD_A (1 << 0)     /* src/constants/hardware.inc */
#endif
#ifndef PAD_START
#define PAD_START (1 << 3)
#endif

#endif
