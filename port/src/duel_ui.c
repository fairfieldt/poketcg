/*
 * UI shims (see duel_ui.h and PORTING.md).
 *
 * Every routine keeps the WRAM/HRAM side effects of the original routine that
 * later logic can observe -- most importantly the card data the original's
 * drawing code loads into wLoadedCard1/wLoadedCard2 and wAttachedEnergies, and
 * wDuelDisplayedScreen -- and reports to the frontend through the host.
 */
#include "engine.h"
#include "tcg.h"
#include "gen/cards.h"
#include "gen/symbols.h"

static const TcgHost *gHost;
u8 gUIDecider;
static u8 gMenuContext;             /* TcgMenu of the last InitializeMenuParameters */
const char *gDuelistNames[2] = { "PLAYER", "OPPONENT" };

void tcg_set_host(const TcgHost *host)
{
	gHost = host;
}

void host_save_duel(const u8 *data, u16 size)
{
	if (gHost && gHost->save)
		gHost->save(gHost->ctx, data, size);
}

/* ---------------------------------------------------------------- frontend plumbing */

static void event(u8 type, u8 a0, u8 a1, u8 a2, u8 a3, u16 text, const char *str)
{
	if (!gHost || !gHost->event)
		return;
	TcgEvent ev = { type, { a0, a1, a2, a3 }, text, str };
	gHost->event(gHost->ctx, &ev);
}

void tcg_ui_event(u8 type, u8 a0, u8 a1, u8 a2, u8 a3, u16 text)
{
	if (type == TCG_EV_TEXT && text) {
		char formatted[512];
		tcg_format_text(text, formatted, sizeof(formatted));
		event(type, a0, a1, a2, a3, text, formatted);
		return;
	}
	event(type, a0, a1, a2, a3, text, NULL);
}

void tcg_ui_screen(u8 screen, u8 a2, u8 a3)
{
	event(TCG_EV_SCREEN, screen, hWhoseTurn, a2, a3, 0, NULL);
}

static u8 decider(void)
{
	return gUIDecider ? gUIDecider : (wWhoseTurn ? wWhoseTurn : hWhoseTurn);
}

static int request(TcgRequest *r)
{
	r->duelist = decider();
	r->shown = hWhoseTurn;
	if (!gHost || !gHost->request)
		return r->type == TCG_REQ_ACK ? 0 : TCG_CANCEL;
	return gHost->request(gHost->ctx, r);
}

void ClearJoypad(void)
{
	hDPadRepeat = 0;
	hKeysReleased = 0;
	hDPadHeld = 0;
	hKeysHeld = 0;
	hKeysPressed = 0;
}

static void set_keys(u8 keys)
{
	hKeysPressed = keys;
	hKeysHeld = keys;
	hDPadHeld = keys & 0xf0;
	hKeysReleased = 0;
}

/* ---------------------------------------------------------------- text */

static int append(char *out, int n, int pos, const char *s)
{
	while (*s && pos < n - 1)
		out[pos++] = *s++;
	out[pos] = 0;
	return pos;
}

static int format_number(char *buf, unsigned v)
{
	char tmp[8];
	int n = 0;
	do {
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	for (int i = 0; i < n; i++)
		buf[i] = tmp[n - 1 - i];
	buf[n] = 0;
	return n;
}

__attribute__((weak)) const char *tcg_text_override(uint16_t id)
{
	(void)id;
	return NULL;
}

const char *tcg_text(uint16_t id)
{
	if (id >= NUM_TEXTS)
		return "";
	const char *replacement = tcg_text_override(id);
	if (replacement)
		return replacement;
	return gTexts[id];
}

/* wDefaultText holds UTF-8 text in the port (the original stores GB-encoded text there;
 * it is only ever printed, never read by the duel logic) */
static char *default_text(void)
{
	wDefaultText[SIZEOF_wDefaultText - 1] = 0;
	return (char *)wDefaultText;
}

static void set_default_text(const char *s)
{
	int i = 0;
	while (s[i] && i < SIZEOF_wDefaultText - 1) {
		wDefaultText[i] = (u8)s[i];
		i++;
	}
	wDefaultText[i] = 0;
}

static int format_text(u16 id, char *out, int n, int pos, u8 *which2, u8 *which3, int depth)
{
	/* text id 0 prints wDefaultText */
	const char *s = id ? tcg_text(id) : default_text();
	for (; *s && pos < n - 1; s++) {
		char c = *s;
		if (c == TX_RAM1) {
			pos = append(out, n, pos, gDuelistNames[hWhoseTurn == OPPONENT_TURN]);
		} else if (c == TX_RAM2) {
			u16 t = rd16(&wTxRam2[(u8)(*which2 * 2)]);
			(*which2)++;
			if (t == 0)
				pos = append(out, n, pos, default_text());
			else if (depth < 4)
				pos = format_text(t, out, n, pos, which2, which3, depth + 1);
		} else if (c == TX_RAM3) {
			char num[8];
			format_number(num, rd16(&wTxRam3[(u8)(*which3 * 2)]));
			(*which3)++;
			pos = append(out, n, pos, num);
		} else if ((u8)c >= 0x20 || c == '\n') {
			out[pos++] = c;
			out[pos] = 0;
		}
	}
	return pos;
}

int tcg_format_text(uint16_t id, char *out, int n)
{
	if (n <= 0)
		return 0;
	out[0] = 0;
	u8 w2 = 0, w3 = 0;
	return format_text(id, out, n, 0, &w2, &w3, 0);
}

static void print_text(u16 text, u8 waits)
{
	char buf[512];
	tcg_format_text(text, buf, sizeof(buf));
	event(TCG_EV_TEXT, waits, 0, 0, 0, text, buf);
}

void print_ui_text(u16 text)
{
	print_text(text, 0);
}

void tcg_ui_practice_checklist(u16 label,const u16 *steps,unsigned count)
{
	char summary[1024],line[512];
	int used=0;
	summary[0]=0;
	if(label) {
		tcg_format_text(label,line,sizeof(line));
		used=append(summary,sizeof(summary),used,line);
	}
	for(unsigned i=0;i<count;i++) {
		tcg_format_text(steps[i],line,sizeof(line));
		if(!line[0])continue;
		used=append(summary,sizeof(summary),used,"\n\n");
		used=append(summary,sizeof(summary),used,line);
	}
	event(TCG_EV_TEXT,0,TCG_TEXT_PRACTICE_CHECKLIST,(u8)count,0,label,summary);
}

void tcg_ui_practice_speech(u16 text)
{
	char message[512];
	tcg_format_text(text,message,sizeof(message));
	event(TCG_EV_TEXT,1,TCG_TEXT_PRACTICE_SPEECH,0,0,text,message);
	WaitForWideTextBoxInput();
}

void LoadTxRam2(u16 text)
{
	wr16(wTxRam2, text);
}

void LoadTxRam3(u16 number)
{
	wr16(wTxRam3, number);
}

void DrawWideTextBox(void) {}
void DrawNarrowTextBox(void) {}

void DrawWideTextBox_PrintText(u16 text) { print_text(text, 0); }
void DrawWideTextBox_PrintTextNoDelay(u16 text) { print_text(text, 0); }
void DrawNarrowTextBox_PrintText(u16 text) { print_text(text, 0); }
void DrawNarrowTextBox_PrintTextNoDelay(u16 text) { print_text(text, 0); }

void WaitForWideTextBoxInput(void)
{
	/* InitializeMenuParameters(0, WideTextBoxMenuParameters) */
	wCurMenuItem = 0;
	hCurMenuItem = 0;
	wMenuCursorXOffset = 18;
	wMenuCursorYOffset = 17;
	wMenuYSeparation = 1;
	wNumMenuItems = 1;
	wMenuVisibleCursorTile = SYM_CURSOR_D;
	wMenuInvisibleCursorTile = SYM_BOX_BOTTOM;
	wMenuUpdateFunc[0] = 0;
	wMenuUpdateFunc[1] = 0;
	wCursorBlinkCounter = 0;
	TcgRequest r = { 0 };
	r.type = TCG_REQ_ACK;
	request(&r);
	set_keys(PAD_A);
}

/* wait for A or B; returns carry if B was pressed */
bool WaitForButtonAorB(void)
{
	TcgRequest r = { 0 };
	r.type = TCG_REQ_CONFIRM;
	int c = request(&r);
	EraseCursor();
	if (c == TCG_CANCEL) {
		set_keys(PAD_B);
		return true;
	}
	set_keys(PAD_A);
	return false;
}

void DrawWideTextBox_WaitForInput(u16 text)
{
	print_text(text, 1);
	WaitForWideTextBoxInput();
}

void DrawNarrowTextBox_WaitForInput(u16 text)
{
	print_text(text, 1);
	WaitForWideTextBoxInput();
}

void DrawWholeScreenTextBox(u16 text)
{
	EmptyScreen();
	print_text(text, 1);
	WaitForWideTextBoxInput();
}

void PrintScrollableText_NoTextBoxLabel(u16 text)
{
	print_text(text, 1);
	WaitForWideTextBoxInput();
}

/* copy the name and level of the card at wLoadedCard1 to wDefaultText */
void CopyCardNameAndLevel(u8 max_len)
{
	char buf[64];
	int pos = 0;
	buf[0] = 0;
	pos = format_text(rd16(wLoadedCard1.Name), buf, sizeof(buf), 0, &(u8){0}, &(u8){0}, 3);
	if (wLoadedCard1.Type < TYPE_ENERGY && wLoadedCard1.Level) {
		char num[8];
		format_number(num, wLoadedCard1.Level);
		pos = append(buf, sizeof(buf), pos, " Lv");
		pos = append(buf, sizeof(buf), pos, num);
	}
	(void)max_len;
	set_default_text(buf);
}

void CopyPlayerNameToDefaultText(void)
{
	set_default_text(gDuelistNames[0]);
}

void CopyOpponentNameToDefaultText(void)
{
	set_default_text(gDuelistNames[1]);
}

/* copy the text with the given id to de (text id 0: the turn duelist's name);
 * returns the address of the terminator */
u8 *CopyText(u16 text, u8 *de)
{
	const char *s = text ? tcg_text(text) : gDuelistNames[hWhoseTurn == OPPONENT_TURN];
	while (*s && de < gWRAM + WRAM_SIZE - 1)
		*de++ = (u8)*s++;
	*de = 0;
	return de;
}

/* ---------------------------------------------------------------- generic menus */

void InitializeMenuParameters(u8 a, const struct MenuParams *p)
{
	wCurMenuItem = a;
	hCurMenuItem = a;
	wMenuCursorXOffset = p->cursor_x;
	wMenuCursorYOffset = p->cursor_y;
	wMenuYSeparation = p->y_separation;
	wNumMenuItems = p->num_items;
	wMenuVisibleCursorTile = p->visible_cursor_tile;
	wMenuInvisibleCursorTile = p->invisible_cursor_tile;
	wr16(wMenuUpdateFunc, p->update_func);
	wCursorBlinkCounter = 0;
	gMenuContext = p->menu;
}

/* returns with the carry flag set (always, once A or B is pressed)
 * a = hCurMenuItem (MENU_CANCEL if B was pressed), e = wCurMenuItem */
bool HandleMenuInput(void)
{
	TcgRequest r = { 0 };
	r.type = TCG_REQ_MENU;
	r.menu = gMenuContext;
	r.num_items = wNumMenuItems;
	r.cursor = wCurMenuItem;
	r.items = wDuelTempList;
	if (r.menu == TCG_MENU_PLAY_AREA)
		/* Included/Bench cursors start at rows 0/3. The exclude flag can
		 * belong to an earlier selector rather than this menu. */
		r.arg[0] = (u8)(wMenuCursorYOffset / 3);
	int c = request(&r);
	wRefreshMenuCursorSFX = 0;
	if (c < 0 || c >= wNumMenuItems) {
		hCurMenuItem = MENU_CANCEL;
		set_keys(PAD_B);
		PlaySFX(SFX_CANCEL);
		return true;
	}
	wCurMenuItem = (u8)c;
	hCurMenuItem = (u8)c;
	set_keys(PAD_A);
	PlaySFX(SFX_CONFIRM);
	return true;
}

/* HandleMenuInput for the color list of HandleColorChangeScreen: the frontend answers
 * with a color (TCG_REQ_COLOR), converted back to the menu item */
bool HandleColorMenuInput(const u8 *item_to_color)
{
	TcgRequest r = { 0 };
	r.type = TCG_REQ_COLOR;
	r.num_items = wNumMenuItems;
	r.cursor = item_to_color[wCurMenuItem];
	r.items = item_to_color;
	int c = request(&r);
	for (u8 i = 0; c >= 0 && i < wNumMenuItems; i++) {
		if (item_to_color[i] == c) {
			wCurMenuItem = i;
			hCurMenuItem = i;
			set_keys(PAD_A);
			PlaySFX(SFX_CONFIRM);
			return true;
		}
	}
	hCurMenuItem = MENU_CANCEL;
	set_keys(PAD_B);
	PlaySFX(SFX_CANCEL);
	return true;
}

void EraseCursor(void) {}
void DrawCursor2(void) {}
void RefreshMenuCursor(void) {}

void SetCursorParametersForTextBox(u8 d, u8 e, u8 b, u8 c)
{
	wMenuCursorXOffset = d;
	wMenuCursorYOffset = e;
	wMenuVisibleCursorTile = b;
	wMenuInvisibleCursorTile = c;
}

/* HandleYesOrNoMenu: wCurMenuItem holds the item *not* selected by default
 * (the original toggles it once before waiting) */
static bool HandleYesOrNoMenu(u16 text, u8 two_items)
{
	u8 cur = (u8)(wCurMenuItem ^ 1);
	wCurMenuItem = cur;
	wCursorBlinkCounter = 0;
	TcgRequest r = { 0 };
	r.num_items = 2;
	r.text = text;
	bool no;
	if (two_items) {
		/* TwoItemHorizontalMenu: the two items are in the text; 0 = left, 1 = right */
		r.type = TCG_REQ_MENU;
		r.menu = TCG_MENU_TWO_ITEMS;
		r.cursor = cur;
		no = request(&r) == 1;
	} else {
		r.type = TCG_REQ_YES_NO;
		r.cursor = cur == 0 ? 1 : 0; /* 1 = yes */
		no = request(&r) != 1;
	}
	wCurMenuItem = no ? 1 : 0;
	set_keys(PAD_A);
	hCurMenuItem = wCurMenuItem;
	wDefaultYesOrNo = 0;
	return no;
}

bool YesOrNoMenu(void)
{
	wLeftmostItemCursorX = 6;
	wCurMenuItem = wDefaultYesOrNo;
	return HandleYesOrNoMenu(0, 0);
}

bool YesOrNoMenuWithText(u16 text)
{
	DrawWideTextBox_PrintText(text);
	wLeftmostItemCursorX = 6;
	wCurMenuItem = wDefaultYesOrNo;
	return HandleYesOrNoMenu(text, 0);
}

bool YesOrNoMenuWithText_LeftAligned(u16 text)
{
	DrawNarrowTextBox_PrintTextNoDelay(text);
	wLeftmostItemCursorX = 2;
	wCurMenuItem = wDefaultYesOrNo;
	return HandleYesOrNoMenu(text, 0);
}

bool YesOrNoMenuWithText_SetCursorToYes(u16 text)
{
	wDefaultYesOrNo = 1;
	return YesOrNoMenuWithText(text);
}

/* display a two-item horizontal menu with custom text and handle input; carry if second item */
bool TwoItemHorizontalMenu(u16 text)
{
	DrawWideTextBox_PrintText(text);
	wLeftmostItemCursorX = 6;
	wCurMenuItem = 1;
	return HandleYesOrNoMenu(text, 1);
}

/* ---------------------------------------------------------------- frames, sound */

void DoFrame(void) {}

void WaitFrames(u8 n)
{
	if (n)
		event(TCG_EV_WAIT, n, 0, 0, 0, 0, NULL);
}

void PlaySFX(u8 sfx) { event(TCG_EV_SFX, sfx, 0, 0, 0, 0, NULL); }
void PlaySong(u8 song) { event(TCG_EV_MUSIC, song, 0, 0, 0, 0, NULL); }
void PlaySFX_InvalidChoice(void) { PlaySFX(SFX_DENIED); }
void PlayDefaultSong(void) {}

/* ---------------------------------------------------------------- duel screens */

void EmptyScreen(void)
{
	wDuelDisplayedScreen = 0;
	tcg_ui_screen(TCG_SCREEN_EMPTY, 0, 0);
}

void ZeroObjectPositionsAndToggleOAMCopy(void) {}

/* empty the screen, and setup text and graphics for a duel */
void SetupDuel(void)
{
	wTileMapFill = 0;
	EmptyScreen();
}

static void LoadPlayAreaCardGfx(u8 card)
{
	if (card == 0xff)
		return;
	LoadCardDataToBuffer1_FromDeckIndex(card);
}

/* PrintPlayAreaCardAttachedEnergies: only GetPlayAreaCardAttachedEnergies has side effects */
static void PrintPlayAreaCardAttachedEnergies(u8 e)
{
	GetPlayAreaCardAttachedEnergies(e);
}

static void DrawDuelHUD(u8 x, u8 y)
{
	wHUDEnergyAndHPBarsX = x;
	wHUDEnergyAndHPBarsY = y;
	u8 card = GetTurnDuelistVariable(DUELVARS_ARENA_CARD);
	if (card == 0xff)
		return;
	LoadCardDataToBuffer1_FromDeckIndex(card);
	CopyCardNameAndLevel(32);
	GetArenaCardColor();
	PrintPlayAreaCardAttachedEnergies(PLAY_AREA_ARENA);
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
}

static void DrawDuelHUDs_DrawHUD(void)
{
	DrawDuelHUD(11, 8);
	SwapTurn();
	DrawDuelHUD(3, 1);
	SwapTurn();
}

/* draws the main elements of the main duel interface, including HUDs, HPs, card names
 * and color symbols, attached cards, and other information, of both duelists. */
void DrawDuelHUDs(void)
{
	if (GetTurnDuelistVariable(DUELVARS_DUELIST_TYPE) == DUELIST_TYPE_PLAYER) {
		DrawDuelHUDs_DrawHUD();
	} else {
		u8 turn = hWhoseTurn;
		hWhoseTurn = PLAYER_TURN;
		DrawDuelHUDs_DrawHUD();
		hWhoseTurn = turn;
	}
	tcg_ui_event(TCG_EV_SCREEN, TCG_SCREEN_MAIN, hWhoseTurn, 1, 0, 0);
}

static void DrawDuelMainScene_Draw(void)
{
	/* first, load the graphics and draw the background scene */
	if (wDuelDisplayedScreen == DUEL_MAIN_SCENE)
		return;
	wDuelDisplayedScreen = DUEL_MAIN_SCENE;
	LoadPlayAreaCardGfx(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	SwapTurn();
	LoadPlayAreaCardGfx(GetTurnDuelistVariable(DUELVARS_ARENA_CARD));
	SwapTurn();
	DrawDuelHUDs();
	tcg_ui_screen(TCG_SCREEN_MAIN, 0, 0);
}

void DrawDuelMainScene(void)
{
	if (GetTurnDuelistVariable(DUELVARS_DUELIST_TYPE) == DUELIST_TYPE_PLAYER) {
		DrawDuelMainScene_Draw();
		return;
	}
	u8 turn = hWhoseTurn;
	hWhoseTurn = PLAYER_TURN;
	DrawDuelMainScene_Draw();
	hWhoseTurn = turn;
}

void DrawDuelBoxMessage(u8 boxmsg)
{
	tcg_ui_screen(TCG_SCREEN_BOX_MESSAGE, boxmsg, 0);
}

void DrawDuelistPortraitsAndNames(void) {}
void PrintDuelResultStats(void) {}
void PlaceDuelMenuItems(void) {}

void DisplayDuelistTurnScreen(void)
{
	EmptyScreen();
	DrawDuelBoxMessage(hWhoseTurn == PLAYER_TURN ? BOXMSG_PLAYERS_TURN : BOXMSG_OPPONENTS_TURN);
	DrawWideTextBox_WaitForInput(DuelistTurnText);
	ExchangeRNG();
}

/* draw a large picture of the card loaded in wLoadedCard1, including its image
 * and a header indicating the type of card (TRAINER, ENERGY, PoKéMoN) */
void DrawLargePictureOfCard(void)
{
	EmptyScreen();
	wDuelDisplayedScreen = LARGE_CARD_PICTURE;
	tcg_ui_screen(TCG_SCREEN_LARGE_CARD, wLoadedCard1.ID, 0);
}

/* display card detail when a card is drawn or played; loads the card to wLoadedCard1 */
void DisplayCardDetailScreen(u8 card, u16 text)
{
	LoadCardDataToBuffer1_FromDeckIndex(card);
	/* _DisplayCardDetailScreen */
	DrawLargePictureOfCard();
	event(TCG_EV_CARD, card, hWhoseTurn, wLoadedCard1.ID, 0, text, NULL);
	CopyCardNameAndLevel(18);
	LoadTxRam2(0);
	DrawWideTextBox_WaitForInput(text);
}

void DisplayPlayerDrawCardScreen(void)
{
	DisplayCardDetailScreen(hTempCardIndex_ff98, YouDrewText);
}

void DisplayCardListDetails(void)
{
	if (wDuelTempList[0] == 0xff)
		return;
	InitAndDrawCardListScreenLayout();
	tcg_ui_screen(TCG_SCREEN_CARD_LIST, CountCardsInDuelTempList(), 0);
	DrawWideTextBox_WaitForInput(YouReceivedTheseCardsText);
}

void DisplayUsePokemonPowerScreen(void)
{
	wCurPlayAreaSlot = hTempPlayAreaLocation_ff9d;
	wCurPlayAreaY = 0;
	EmptyScreen();
	PrintPlayAreaCardInformationAndLocation();
	tcg_ui_screen(TCG_SCREEN_OTHER, hTempPlayAreaLocation_ff9d, 0);
}

void DisplayUsedTrainerCardDetailScreen(void)
{
	DisplayCardDetailScreen(hTempCardIndex_ff9f, UsedText);
}

void PrintUsedTrainerCardDescription(void)
{
	EmptyScreen();
	DrawWideTextBox_WaitForInput(UsedText);
}

void DisplayOpponentUsedAttackScreen(void)
{
	EmptyScreen();
	LoadCardDataToBuffer1_FromCardID(wTempCardID_ccc2);
	wCardPageNumber = CARDPAGE_POKEMON_OVERVIEW;
}

/* display the animation of the turn duelist drawing number of cards.
 * if there isn't any card left in the deck, let the player know with a text message. */
void DisplayDrawNCardsScreen(u8 n)
{
	wNumCardsTryingToDraw = n;
	wNumCardsBeingDrawn = 0;
	u8 left = (u8)(DECK_SIZE - GetTurnDuelistVariable(DUELVARS_NUMBER_OF_CARDS_NOT_IN_DECK));
	if (left < wNumCardsTryingToDraw)
		wNumCardsTryingToDraw = left; /* trying to draw more cards than there are left in the deck */
	if (wDuelDisplayedScreen != DRAW_CARDS && wDuelDisplayedScreen != SHUFFLE_DECK) {
		EmptyScreen();
		DrawDuelistPortraitsAndNames();
	}
	wDuelDisplayedScreen = DRAW_CARDS;
	if (wNumCardsTryingToDraw == 0) {
		DrawWideTextBox_WaitForInput(CannotDrawCardBecauseNoCardsInDeckText);
		return;
	}
	LoadTxRam3(wNumCardsTryingToDraw);
	DrawWideTextBox_PrintText(DrawCardsFromTheDeckText);
	do {
		wNumCardsBeingDrawn++;
	} while (wNumCardsBeingDrawn < wNumCardsTryingToDraw);
	WaitFrames(30);
}

void DisplayDrawOneCardScreen(void)
{
	DisplayDrawNCardsScreen(1);
}

static void PlayShuffleAndDrawCardsAnimation(u16 shuffle_text, u16 draw_text)
{
	EmptyScreen();
	DrawDuelistPortraitsAndNames();
	wDuelDisplayedScreen = SHUFFLE_DECK;
	DrawWideTextBox_PrintText(shuffle_text);
	if (wDuelType == DUELTYPE_PRACTICE)
		WaitForWideTextBoxInput();
	else
		FinishQueuedAnimations();
	wNumCardsBeingDrawn = 0;
	ResetAnimationQueue();
	DrawWideTextBox_PrintText(draw_text);
	do {
		wNumCardsBeingDrawn++;
	} while (wNumCardsBeingDrawn < 7);
	WaitFrames(30);
	FinishQueuedAnimations();
}

void PlayShuffleAndDrawCardsAnimation_TurnDuelist(void)
{
	PlayShuffleAndDrawCardsAnimation(ShufflesTheDeckText, Drew7CardsText);
}

void PlayShuffleAndDrawCardsAnimation_BothDuelists(void)
{
	PlayShuffleAndDrawCardsAnimation(wDuelType == DUELTYPE_PRACTICE ?
		ThisIsJustPracticeDoNotShuffleText : EachPlayerShuffleOpponentsDeckText,
		EachPlayerDraw7CardsText);
}

void PlayDeckShuffleAnimation(void)
{
	if (wDuelDisplayedScreen != SHUFFLE_DECK) {
		EmptyScreen();
		DrawDuelistPortraitsAndNames();
	}
	wDuelDisplayedScreen = SHUFFLE_DECK;
	tcg_ui_event(TCG_EV_ANIM, DUEL_ANIM_PLAYER_SHUFFLE, 1, hWhoseTurn, 0, 0);
}

void PrintAttackOrCardDescription(void) {}

void OpenCardPage_FromHand(void)
{
	event(TCG_EV_CARD, 0xff, hWhoseTurn, wLoadedCard1.ID, 1, 0, NULL);
	WaitForWideTextBoxInput();
}

void OpenCardPage_FromCheckPlayArea(void)
{
	OpenCardPage_FromHand();
}

void OpenAttackPage(void) {}

void OpenDuelCheckMenu(void) {}

void RedrawTurnDuelistsDuelHUD(void)
{
	if (hWhoseTurn == wWhoseTurn) {
		DrawDuelHUDs();
		return;
	}
	SwapTurn();
	DrawDuelHUDs();
	SwapTurn();
}

void RedrawTurnDuelistsMainSceneOrDuelHUD(void)
{
	if (wDuelDisplayedScreen == DUEL_MAIN_SCENE) {
		RedrawTurnDuelistsDuelHUD();
		return;
	}
	if (hWhoseTurn == wWhoseTurn) {
		DrawDuelMainScene();
		return;
	}
	SwapTurn();
	DrawDuelMainScene();
	SwapTurn();
}

void PrintAttachedEnergyToPokemon(void)
{
	LoadCardNameToTxRam2_b(GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD)));
	LoadCardNameToTxRam2(hTempCardIndex_ff98);
	DrawWideTextBox_WaitForInput(AttachedEnergyToPokemonText);
}

/* print the PokemonEvolvedIntoPokemonText, given the Pokemon card to evolve in wPreEvolutionPokemonCard,
 * and the evolved Pokemon card in hTempCardIndex_ff98. also play a sound effect. */
void PrintPokemonEvolvedIntoPokemon(void)
{
	PlaySFX(SFX_POKEMON_EVOLUTION);
	LoadCardNameToTxRam2(wPreEvolutionPokemonCard);
	LoadCardNameToTxRam2_b(hTempCardIndex_ff98);
	DrawWideTextBox_WaitForInput(PokemonEvolvedIntoPokemonText);
}

void DisplayNoBasicPokemonInHandScreen(void)
{
	EmptyScreen();
	CreateHandCardList(NULL);
	tcg_ui_screen(TCG_SCREEN_CARD_LIST, CountCardsInDuelTempList(), 0);
	WaitForWideTextBoxInput();
}

void DrawPlayAreaToPlacePrizeCards(void)
{
	EmptyScreen();
	wCheckMenuPlayAreaWhichLayout = hWhoseTurn;
	wCheckMenuPlayAreaWhichDuelist = hWhoseTurn;
}

void HandleDuelSetup_PlacePrizes(void)
{
	WaitFrames((u8)(20 * wDuelInitialPrizes));
}

void DrawYourOrOppPlayAreaScreen_Bank0(u8 l)
{
	wCheckMenuPlayAreaWhichDuelist = hWhoseTurn;
	wCheckMenuPlayAreaWhichLayout = l;
	EmptyScreen();
}

void Func_82b6(void) {}

/* GetFirstSetPrizeCard: first set prize at or after index a (wrapping), or 0 */
static u8 GetFirstSetPrizeCard(u8 c)
{
	u8 d = GetTurnDuelistVariable(DUELVARS_PRIZES);
	u8 e = PRIZES_6;
	for (;;) {
		if ((1 << c) & d)
			return c; /* prize is set */
		if (--e == 0)
			return 0;
		c++;
		if (c == PRIZES_6)
			c = 0;
	}
}

/* a = number of prize cards for player to select to take */
void SelectPrizeCards(u8 n)
{
	wNumberOfPrizeCardsToSelect = n;
	wYourOrOppPlayAreaCurPosition = GetFirstSetPrizeCard(0);
	wr16(wSelectedPrizeCardListPtr, ADDR_hTempPlayAreaLocation_ffa1);
	for (;;) {
		if (wNumberOfPrizeCardsToSelect == 0 || GetTurnDuelistVariable(DUELVARS_PRIZES) == 0) {
			/* done_selection */
			hTemp_ffa0 = GetTurnDuelistVariable(DUELVARS_PRIZES);
			*gbptr(rd16(wSelectedPrizeCardListPtr)) = 0xff;
			return;
		}
		DrawYourOrOppPlayAreaScreen_Bank0(hWhoseTurn);
		u8 c, b;
		for (;;) {
			TcgRequest r = { 0 };
			r.type = TCG_REQ_PRIZE;
			r.num_items = 6;
			r.cursor = wYourOrOppPlayAreaCurPosition;
			r.text = PleaseChooseAPrizeText;
			r.arg[0] = GetTurnDuelistVariable(DUELVARS_PRIZES);
			int k = request(&r);
			if (k < 0 || k >= 6)
				continue;
			wYourOrOppPlayAreaCurPosition = (u8)k;
			set_keys(PAD_A);
			c = wYourOrOppPlayAreaCurPosition;
			b = (u8)(1 << c);
			/* if cursor prize is not set, then return to input loop */
			if (GetTurnDuelistVariable(DUELVARS_PRIZES) & b)
				break;
		}
		/* remove prize */
		TurnDuelistVars()[DUELVARS_PRIZES] -= b;
		/* get its deck index */
		u8 card = GetTurnDuelistVariable((u8)(c + DUELVARS_PRIZE_CARDS));
		u16 p = rd16(wSelectedPrizeCardListPtr);
		*gbptr(p) = card; /* store deck index */
		wr16(wSelectedPrizeCardListPtr, (u16)(p + 1));
		/* add prize card to hand */
		AddCardToHand(card);
		LoadCardDataToBuffer1_FromDeckIndex(card);
		OpenCardPage_FromHand();
		wNumberOfPrizeCardsToSelect--;
		wYourOrOppPlayAreaCurPosition = GetFirstSetPrizeCard(wYourOrOppPlayAreaCurPosition);
	}
}

/* ---------------------------------------------------------------- card lists */

void InitAndDrawCardListScreenLayout(void)
{
	wSelectedDuelSubMenuItem = 0;
	wSelectedDuelSubMenuScrollOffset = 0;
	wSortCardListByID = 0;
	wPrintSortNumberInCardListPtr[0] = 0;
	wPrintSortNumberInCardListPtr[1] = 0;
	wCardListItemSelectionMenuType = 0;
	wNoItemSelectionMenuKeys = PAD_START;
	wr16(wCardListInfoBoxText, PleaseSelectHandText);
	wr16(wCardListHeaderText, DuelistHandText);
	DrawCardListScreenLayout();
}

void InitAndDrawCardListScreenLayout_WithSelectCheckMenu(void)
{
	InitAndDrawCardListScreenLayout();
	wCardListItemSelectionMenuType = SELECT_CHECK;
}

/* returns carry if wDuelTempList is empty */
bool DrawCardListScreenLayout(void)
{
	EmptyScreen();
	PrintSortNumberInCardList_CallFromPointer();
	return wDuelTempList[0] == 0xff;
}

void SetCardListHeaderText(u16 hl_info, u16 de_header)
{
	wr16(wCardListHeaderText, de_header);
	SetCardListInfoBoxText(hl_info);
}

void SetCardListInfoBoxText(u16 hl_info)
{
	wr16(wCardListInfoBoxText, hl_info);
}

void SetDiscardPileScreenTexts(void)
{
	SetCardListHeaderText(ChooseTheCardYouWishToExamineText,
		hWhoseTurn == PLAYER_TURN ? YourDiscardPileText : OpponentsDiscardPileText);
}

/* the sort number printers are pure drawing; wPrintSortNumberInCardListPtr keeps the
 * original's GB address of PrintSortNumberInCardList for RAM fidelity */
void PrintSortNumberInCardList_SetPointer(void)
{
	wr16(wPrintSortNumberInCardListPtr, GBADDR_PrintSortNumberInCardList);
	wSortCardListByID = TRUE;
}

void PrintSortNumberInCardList_CallFromPointer(void)
{
	/* Snapshot the original ordering glyphs without changing their RAM. */
	event(TCG_EV_CARD_ORDER,hWhoseTurn,0xff,0,0,0,NULL);
	if(rd16(wPrintSortNumberInCardListPtr)!=GBADDR_PrintSortNumberInCardList)return;
	for(u8 i=0;i<5&&wDuelTempList[i]!=0xff;i++)
		if(wDuelTempList[10+i])event(TCG_EV_CARD_ORDER,hWhoseTurn,
			wDuelTempList[i],wDuelTempList[10+i],0,0,NULL);
}

/* LoadSelectedCardGfx: loads the card at the cursor (hCurMenuItem) of wDuelTempList */
static void LoadSelectedCardGfx(void)
{
	u8 id;
	GetCardInDuelTempList(hCurMenuItem, &id);
	LoadCardDataToBuffer1_FromCardID(id);
}

/* display the SELECT|CHECK or PLAY|CHECK menu when a card of a list is selected
 * and handle input. return carry if b is pressed (or CHECK selected). */
bool CardListItemSelectionMenu(void)
{
	if (wCardListItemSelectionMenuType == 0)
		return false;
	u16 text = SelectCheckText;
	if (wCardListItemSelectionMenuType == PLAY_CHECK) {
		LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff98);
		text = wLoadedCard1.Type == TYPE_TRAINER ? PlayCheck1Text : PlayCheck2Text;
	}
	DrawNarrowTextBox_PrintTextNoDelay(text);
	static const struct MenuParams ItemSelectionMenuParameters = {
		1, 14, 2, 2, SYM_CURSOR_R, SYM_SPACE, 0, TCG_MENU_TWO_ITEMS,
	};
	InitializeMenuParameters(0, &ItemSelectionMenuParameters);
	TcgRequest r = { 0 };
	r.type = TCG_REQ_CARD_ITEM_MENU;
	r.num_items = 2;
	r.text = text;
	r.arg[0] = hTempCardIndex_ff98;
	int c = request(&r);
	if (c < 0 || c > 1) {
		hCurMenuItem = MENU_CANCEL;
		set_keys(PAD_B);
		return true;
	}
	wCurMenuItem = (u8)c;
	hCurMenuItem = (u8)c;
	set_keys(PAD_A);
	if (c == 0)
		return false;
	/* CHECK option selected: open the card page */
	LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff98);
	OpenCardPage_FromHand();
	DrawCardListScreenLayout();
	return true;
}

/* displays a list of cards and handles input in order to navigate through the list,
 * select a card, open a card page, etc. returns carry if B is pressed to exit the
 * card list screen. otherwise returns the selected card at hTempCardIndex_ff98 (and *card). */
bool DisplayCardList(u8 *card)
{
	for (;;) {
		u8 n = CountCardsInDuelTempList();
		hCurMenuItem = (u8)(wSelectedDuelSubMenuItem + wSelectedDuelSubMenuScrollOffset);
		if (n)
			LoadSelectedCardGfx();
		tcg_ui_screen(TCG_SCREEN_CARD_LIST, n, 0);
		TcgRequest r = { 0 };
		r.type = TCG_REQ_CARD_LIST;
		r.num_items = n;
		r.cursor = hCurMenuItem;
		r.items = wDuelTempList;
		r.text = rd16(wCardListHeaderText);
		r.text2 = rd16(wCardListInfoBoxText);
		r.arg[0] = (wNoItemSelectionMenuKeys & PAD_A) ? 1 : 0; /* view only: A opens the card page */
		r.arg[1] = wCardListItemSelectionMenuType;
		int c = request(&r);
		if (c < 0 || c >= n) {
			/* b_pressed (HandleMenuInput sets hCurMenuItem to $ff on B) */
			hCurMenuItem = MENU_CANCEL;
			set_keys(PAD_B);
			if (card)
				*card = hCurMenuItem;
			return true;
		}
		hCurMenuItem = (u8)c;
		if (c != (u8)(wSelectedDuelSubMenuItem + wSelectedDuelSubMenuScrollOffset))
			LoadSelectedCardGfx(); /* the original reloads the card image when the cursor moves */
		/* refresh the position of the last checked card of the list */
		if (c < 5) {
			wSelectedDuelSubMenuItem = (u8)c;
			wSelectedDuelSubMenuScrollOffset = 0;
		} else {
			wSelectedDuelSubMenuItem = 4;
			wSelectedDuelSubMenuScrollOffset = (u8)(c - 4);
		}
		set_keys(PAD_A);
		if (wNoItemSelectionMenuKeys & PAD_A) {
			/* open the card page directly, without an item selection menu */
			u8 id;
			GetCardInDuelTempList(hCurMenuItem, &id);
			LoadCardDataToBuffer1_FromDeckIndex(hTempCardIndex_ff98);
			OpenCardPage_FromHand();
			DrawCardListScreenLayout();
			continue;
		}
		/* display the item selection menu (PLAY|CHECK or SELECT|CHECK) for the selected card */
		GetCardInDuelTempList_OnlyDeckIndex(hCurMenuItem);
		if (CardListItemSelectionMenu())
			continue; /* jump back if B pressed to exit the item selection menu */
		if (card)
			*card = hTempCardIndex_ff98;
		return false;
	}
}

bool OpenDiscardPileScreen(void)
{
	if (CreateDiscardPileCardList()) {
		DrawWideTextBox_WaitForInput(TheDiscardPileHasNoCardsText);
		return true;
	}
	InitAndDrawCardListScreenLayout();
	SetDiscardPileScreenTexts();
	wNoItemSelectionMenuKeys = PAD_START + PAD_A;
	DisplayCardList(NULL);
	return false;
}

/* ---------------------------------------------------------------- play area screens */

static void PrintPlayAreaCardHeader(void)
{
	LoadCardDataToBuffer1_FromDeckIndex(GetTurnDuelistVariable((u8)(wCurPlayAreaSlot + DUELVARS_ARENA_CARD)));
	GetPlayAreaCardColor(wCurPlayAreaSlot);
}

static void PrintPlayAreaCardInformation(void)
{
	PrintPlayAreaCardHeader();
	PrintPlayAreaCardAttachedEnergies(wCurPlayAreaSlot);
}

void PrintPlayAreaCardInformationAndLocation(void)
{
	if (GetTurnDuelistVariable((u8)(wCurPlayAreaSlot + DUELVARS_ARENA_CARD)) == 0xff)
		return;
	PrintPlayAreaCardInformation();
}

void InitAndPrintPlayAreaCardInformationAndLocation(void)
{
	wCurPlayAreaSlot = hTempPlayAreaLocation_ff9d;
	wCurPlayAreaY = (u8)(hTempPlayAreaLocation_ff9d * 3);
	PrintPlayAreaCardInformationAndLocation();
}

/* returns carry if B was pressed (SetCursorParametersForTextBox_Default falls
 * through to WaitForButtonAorB) */
bool InitAndPrintPlayAreaCardInformationAndLocation_WithTextBox(void)
{
	InitAndPrintPlayAreaCardInformationAndLocation();
	SetCursorParametersForTextBox(0, wCurPlayAreaY, SYM_CURSOR_R, SYM_SPACE);
	return WaitForButtonAorB();
}

void SetupPlayAreaScreen(void)
{
	wExcludeArenaPokemon = 0;
	if (wDuelDisplayedScreen == PLAY_AREA_CARD_LIST)
		return;
	EmptyScreen();
}

u8 PrintPlayAreaCardList(void)
{
	wDuelDisplayedScreen = PLAY_AREA_CARD_LIST;
	wr16(wListPointer, ADDR_wDuelTempList);
	u8 c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 b = 0;
	/* bug (original): with no Pokemon in play area c = 0 would loop 256 times */
	do {
		wCurPlayAreaSlot = b;
		wCurPlayAreaY = (u8)(b * 3);
		u16 p = rd16(wListPointer);
		*gbptr(p) = GetTurnDuelistVariable((u8)(b + DUELVARS_ARENA_CARD));
		wr16(wListPointer, (u16)(p + 1));
		PrintPlayAreaCardInformationAndLocation();
		b++;
	} while (--c);
	for (u8 i = b; i != MAX_PLAY_AREA_POKEMON; i++) {
		wCurPlayAreaSlot = i;
		wCurPlayAreaY = (u8)(i * 3);
	}
	wNumPlayAreaItems = b;
	if (wExcludeArenaPokemon) {
		b--;
		wNumPlayAreaItems = b;
		for (u8 i = 0; i < b; i++)
			wDuelTempList[i] = wDuelTempList[(u8)(i + 1)];
	}
	tcg_ui_screen(TCG_SCREEN_PLAY_AREA, wExcludeArenaPokemon, 0);
	return wNumPlayAreaItems;
}

u8 PrintPlayAreaCardList_EnableLCD(void)
{
	wDuelDisplayedScreen = PLAY_AREA_CARD_LIST;
	PrintPlayAreaCardList();
	return wNumPlayAreaItems;
}

static bool display_play_area_screen(u8 keys,int preferred_cursor);

bool OpenPlayAreaScreenForViewing(void)
{
	return DisplayPlayAreaScreen(PAD_START + PAD_A);
}

bool OpenPlayAreaScreenForSelection(void)
{
	return DisplayPlayAreaScreen(PAD_START);
}

bool OpenPlayAreaScreenForEvolutionSelection(u8 cursor)
{
	return display_play_area_screen(PAD_START,cursor);
}

bool DisplayPlayAreaScreen(u8 keys)
{
	return display_play_area_screen(keys,-1);
}

static bool display_play_area_screen(u8 keys,int preferred_cursor)
{
	wNoItemSelectionMenuKeys = keys;
	u8 saved_ff98 = hTempCardIndex_ff98;
	bool reload = false;
	if (!wPlayAreaScreenLoaded) {
		wSelectedDuelSubMenuItem = 0;
		wPlayAreaScreenLoaded = 1;
		reload = true;
	}
	for (;;) {
		if (reload) {
			EmptyScreen();
			PrintPlayAreaCardList();
			reload = false;
		}
		/* skip_ahead */
		static const struct MenuParams included = { 0, 0, 3, 6, SYM_CURSOR_R, SYM_SPACE, GBADDR_PlayAreaScreenMenuFunction, TCG_MENU_PLAY_AREA };
		static const struct MenuParams excluded = { 0, 3, 3, 6, SYM_CURSOR_R, SYM_SPACE, GBADDR_PlayAreaScreenMenuFunction, TCG_MENU_PLAY_AREA };
		InitializeMenuParameters(wSelectedDuelSubMenuItem, wExcludeArenaPokemon ? &excluded : &included);
		wNumMenuItems = wNumPlayAreaItems;
		TcgRequest r = { 0 };
		r.type = TCG_REQ_PLAY_AREA;
		r.num_items = wNumPlayAreaItems;
		r.cursor = (u8)(wSelectedDuelSubMenuItem + wExcludeArenaPokemon);
		if (preferred_cursor >= wExcludeArenaPokemon &&
		    preferred_cursor < wExcludeArenaPokemon + wNumPlayAreaItems)
			r.cursor = (u8)preferred_cursor;
		r.arg[0] = wExcludeArenaPokemon;
		r.arg[1] = (keys & PAD_A) ? 1 : 0; /* view only */
		r.items = wDuelTempList;
		int loc = request(&r);
		int item = loc - wExcludeArenaPokemon;
		if (loc < 0 || item < 0 || item >= wNumPlayAreaItems) {
			/* B pressed */
			hCurMenuItem = MENU_CANCEL;
			set_keys(PAD_B);
			wSelectedDuelSubMenuItem = wCurMenuItem;
			wCurPlayAreaSlot = (u8)(wExcludeArenaPokemon + wCurMenuItem);
			hTempPlayAreaLocation_ff9d = (u8)(MENU_CANCEL + wExcludeArenaPokemon);
			hTempCardIndex_ff98 = saved_ff98;
			hCurMenuItem = hTempPlayAreaLocation_ff9d;
			return true;
		}
		wCurMenuItem = (u8)item;
		hCurMenuItem = (u8)item;
		set_keys(PAD_A);
		wSelectedDuelSubMenuItem = (u8)item;
		wCurPlayAreaSlot = (u8)(wExcludeArenaPokemon + item);
		if (keys & PAD_A) {
			/* viewing: open the card page */
			u8 card = GetTurnDuelistVariable((u8)(wCurPlayAreaSlot + DUELVARS_ARENA_CARD));
			if (card != 0xff) {
				LoadCardDataToBuffer1_FromCardID(GetCardIDFromDeckIndex(card));
				OpenCardPage_FromCheckPlayArea();
			}
			reload = true;
			continue;
		}
		hTempPlayAreaLocation_ff9d = (u8)(hCurMenuItem + wExcludeArenaPokemon);
		if (GetTurnDuelistVariable((u8)(hTempPlayAreaLocation_ff9d + DUELVARS_ARENA_CARD_HP)) != 0) {
			hTempCardIndex_ff98 = saved_ff98;
			hCurMenuItem = hTempPlayAreaLocation_ff9d;
			return false;
		}
	}
}

/* the duel menu's Pkmn Power screen: lists (deck index, attack 1 category) pairs in wDuelTempList */
void DisplayPlayAreaScreenToUsePkmnPower_DrawScreen(void)
{
	EmptyScreen();
	wr16(wListPointer, ADDR_wDuelTempList);
	u8 c = GetTurnDuelistVariable(DUELVARS_NUMBER_OF_POKEMON_IN_PLAY_AREA);
	u8 b = 0;
	do {
		wHUDEnergyAndHPBarsX = b;
		wCurPlayAreaY = (u8)(b * 3);
		u16 p = rd16(wListPointer);
		*gbptr(p) = GetTurnDuelistVariable((u8)(b + DUELVARS_ARENA_CARD));
		PrintPlayAreaCardHeader();
		*gbptr((u16)(p + 1)) = wLoadedCard1.Atk1.Category;
		wr16(wListPointer, (u16)(p + 2));
		b++;
	} while (--c);
	wNumPlayAreaItems = b;
	tcg_ui_screen(TCG_SCREEN_PLAY_AREA, 0, TCG_PLAY_AREA_POWER);
}

/* ---------------------------------------------------------------- energy discard menu */

void DisplayEnergyDiscardScreen(u8 loc)
{
	wEnergyDiscardPlayAreaLocation = loc;
	EmptyScreen();
	wCurPlayAreaSlot = loc;
	wCurPlayAreaY = 0;
	PrintPlayAreaCardInformation();
	wEnergyDiscardMenuNumerator = 0;
	wEnergyDiscardMenuDenominator = 1;
	DisplayEnergyDiscardMenu();
}

void DisplayEnergyDiscardMenu(void)
{
	DrawWideTextBox_PrintTextNoDelay(ChooseEnergyCardToDiscardText);
	wCardListIndicatorYPosition = 4;
	tcg_ui_screen(TCG_SCREEN_ENERGY_DISCARD, wEnergyDiscardPlayAreaLocation, 0);
}

bool HandleEnergyDiscardMenuInput(void)
{
	TcgRequest r = { 0 };
	r.type = TCG_REQ_ENERGY_DISCARD;
	r.num_items = CountCardsInDuelTempList();
	r.items = wDuelTempList;
	r.text = ChooseEnergyCardToDiscardText;
	r.arg[0] = wEnergyDiscardMenuNumerator;
	r.arg[1] = wEnergyDiscardMenuDenominator;
	r.arg[2] = wEnergyDiscardPlayAreaLocation;
	int c = request(&r);
	if (c < 0 || c >= r.num_items) {
		hCurMenuItem = MENU_CANCEL;
		set_keys(PAD_B);
		return true;
	}
	hCurMenuItem = (u8)c;
	wCurMenuItem = (u8)c;
	set_keys(PAD_A);
	GetCardInDuelTempList_OnlyDeckIndex((u8)c);
	return false;
}

/* ---------------------------------------------------------------- animations */

void ResetAnimationQueue(void) {}
void FinishQueuedAnimations(void) {}
bool CheckAnyAnimationPlaying(void) { return false; }

void PlayDuelAnimation(u8 anim)
{
	event(TCG_EV_ANIM, anim, 1, hWhoseTurn, 0, 0, NULL);
}

void ResetAttackAnimationIsPlaying(void)
{
	wAttackAnimationIsPlaying = FALSE;
}

/* play attack animation
 * - [wLoadedAttackAnimation]: animation to play
 * - de: damage dealt by the attack (to display the animation with the number)
 * - b: PLAY_AREA_* location, if applicable
 * - c: a wDamageEffectiveness constant (to print WEAK or RESIST if necessary)
 * - h: which duelist side to play animation */
void PlayAttackAnimation(u16 de, u8 b, u8 c, u8 h)
{
	u8 turn = hWhoseTurn;
	hWhoseTurn = wWhoseTurn;
	wDamageAnimEffectiveness = c;
	if (hWhoseTurn != h)
		b |= 0x80; /* on the non-turn duelist's side */
	wDamageAnimPlayAreaLocation = b;
	wDamageAnimPlayAreaSide = wWhoseTurn;
	wDamageAnimCardID = wTempNonTurnDuelistCardID;
	wr16(wDamageAnimAmount, de);
	/* if damage >= 70, ATK_ANIM_HIT becomes ATK_ANIM_BIG_HIT */
	if (wLoadedAttack.Animation == ATK_ANIM_HIT && (u8)de >= 70)
		wLoadedAttack.Animation = ATK_ANIM_BIG_HIT;
	event(TCG_EV_ANIM, wLoadedAttack.Animation, 0, h, b, 0, NULL);
	if (de)
		event(TCG_EV_DAMAGE, h, b & 0x7f, (u8)de, (u8)(de >> 8), 0, NULL);
	hWhoseTurn = turn;
}

void WaitAttackAnimation(void) {}

/* plays all animations that are queued in wStatusConditionQueue */
void PlayStatusConditionQueueAnimations(void)
{
	u8 n = wStatusConditionQueueIndex;
	if (n == 0)
		return;
	wStatusConditionQueue[n] = 0;
	for (u8 *hl = wStatusConditionQueue; *hl; hl += 3) {
		u8 d = hl[0], a = hl[2], e;
		if (a == ASLEEP)
			e = ATK_ANIM_SLEEP;
		else if (a == PARALYZED)
			e = ATK_ANIM_PARALYSIS;
		else if (a == POISONED || a == DOUBLE_POISONED)
			e = ATK_ANIM_POISON;
		else if (a == CONFUSED)
			e = hWhoseTurn == d ? ATK_ANIM_OWN_CONFUSION : ATK_ANIM_CONFUSION;
		else
			continue;
		wLoadedAttack.Animation = e;
		wDuelAnimLocationParam = 0;
		event(TCG_EV_ANIM, e, 0, d, 0, 0, NULL);
	}
}

/* this is a simple version of PlayAttackAnimation_DealAttackDamage that doesn't
 * take into account status conditions, damage modifiers, etc, for damage calculation.
 * used for confusion damage to self and for damage to benched Pokemon, for example */
void PlayAttackAnimation_DealAttackDamageSimple(u8 *hl, u16 de, u8 b, u8 c)
{
	PlayAttackAnimation(de, b, c, (u8)(gbaddr(hl) >> 8));
	WaitAttackAnimation();
	SubtractHP(hl, de);
	if (wDuelDisplayedScreen != DUEL_MAIN_SCENE)
		return;
	DrawDuelHUDs();
}

void PlayBetweenTurnsAnimation(u8 anim)
{
	if (wDuelType == 0 && wWhoseTurn != PLAYER_TURN) {
		SwapTurn();
		wDuelAnimDuelistSide = hWhoseTurn;
		SwapTurn();
	} else {
		wDuelAnimDuelistSide = hWhoseTurn;
	}
	wDuelAnimLocationParam = 0;
	wDuelAnimationScreen = DUEL_ANIM_SCREEN_MAIN_SCENE;
	PlayDuelAnimation(anim);
	RedrawTurnDuelistsDuelHUD();
}

/* ---------------------------------------------------------------- coin toss */

static u8 GetCoinTossDuelistType(void)
{
	return GetTurnDuelistVariable(DUELVARS_DUELIST_TYPE);
}

/* function that executes one or more consecutive coin tosses during a duel (a = number of coin tosses),
 * returns the number of heads in wCoinTossNumHeads (and as return value) */
static u8 _TossCoin(u8 a)
{
	wCoinTossTotalNum = a;
	if (wDuelDisplayedScreen != COIN_TOSS) {
		wCoinTossNumTossed = 0;
		EmptyScreen();
	}
	/* no need to print text if this is not the first coin toss */
	u16 text = rd16(wCoinTossScreenTextID);
	if (wCoinTossNumTossed == 0) {
		wDuelDisplayedScreen = COIN_TOSS;
		tcg_ui_screen(TCG_SCREEN_COIN_TOSS, wCoinTossTotalNum, 0);
		print_text(text, 0);
	}
	wCoinTossScreenTextID[0] = 0;
	wCoinTossScreenTextID[1] = 0;
	/* store duelist type and reset number of heads */
	wCoinTossDuelistType = GetCoinTossDuelistType();
	ExchangeRNG();
	wCoinTossNumHeads = 0;
	u8 saved_decider = gUIDecider;
	gUIDecider = hWhoseTurn;
	do {
		ResetAnimationQueue();
		PlayDuelAnimation(DUEL_ANIM_COIN_SPIN);
		if (wCoinTossDuelistType == DUELIST_TYPE_PLAYER)
			WaitForWideTextBoxInput(); /* wait for input when player is ready to toss */
		else
			WaitFrames(30);
		ResetAnimationQueue();
		u8 e = TAILS;
		if (!(UpdateRNGSources() & 1))
			e = HEADS;
		if (e)
			wCoinTossNumHeads++;
		event(TCG_EV_COIN, e, (u8)(wCoinTossNumTossed + 1), wCoinTossTotalNum, hWhoseTurn, text, NULL);
		PlaySFX((wCoinTossDuelistType == 0 ? e : (u8)(e ^ 1)) ? SFX_COIN_TOSS_HEADS : SFX_COIN_TOSS_TAILS);
		wCoinTossNumTossed++;
		if (wCoinTossDuelistType != DUELIST_TYPE_PLAYER) {
			/* wait for input if we are finished */
			if (wCoinTossNumTossed == wCoinTossTotalNum)
				WaitForWideTextBoxInput();
			WaitFrames(30);
			if (!(wCoinTossTotalNum | wCoinTossNumHeads))
				WaitForWideTextBoxInput();
		} else {
			WaitForWideTextBoxInput();
		}
		FinishQueuedAnimations();
		/* we're finished if wCoinTossNumTossed == wCoinTossTotalNum */
	} while (wCoinTossNumTossed < wCoinTossTotalNum);
	gUIDecider = saved_decider;
	ExchangeRNG();
	FinishQueuedAnimations();
	ResetAnimationQueue();
	return wCoinTossNumHeads;
}

/* function that executes a single coin toss during a duel.
 * returns carry (true) if heads */
bool TossCoin(u16 text)
{
	TRACE_FN();
	wr16(wCoinTossScreenTextID, text);
	u8 a = _TossCoin(1);
	wDuelDisplayedScreen = 0;
	return a != 0;
}

u8 TossCoinATimes(u8 n, u16 text)
{
	TRACE_FN();
	wr16(wCoinTossScreenTextID, text);
	return _TossCoin(n);
}

/* ---------------------------------------------------------------- the duel menu */

/* handle input for the 2-row 3-column duel menu; returns the selected item */
u8 HandleDuelMenuInput(u8 initial)
{
	wCurMenuItem = initial;
	hCurMenuItem = initial;
	wCursorBlinkCounter = 0;
	for (;;) {
		TcgRequest r = { 0 };
		r.type = TCG_REQ_DUEL_MENU;
		r.num_items = 6;
		r.cursor = initial;
		int c = request(&r);
		if (c < 0 || c > 5)
			continue; /* B does nothing in the duel menu */
		wCurMenuItem = (u8)c;
		hCurMenuItem = (u8)c;
		set_keys(PAD_A);
		PlaySFX(SFX_CONFIRM);
		return (u8)c;
	}
}

/* ---------------------------------------------------------------- numbers (core.asm) */

/* convert two-byte number in hl to TX_SYMBOL format and write it to wStringBuffer
 * replace leading zeros with SYM_SPACE */
void TwoByteNumberToTxSymbol_PadSpace_Bank1(u16 hl)
{
	static const u16 pow10[5] = { 10000, 1000, 100, 10, 1 };
	for (int i = 0; i < 5; i++) {
		wStringBuffer[i] = (u8)(SYM_0 + hl / pow10[i]);
		hl %= pow10[i];
	}
	wStringBuffer[5] = TX_END;
	for (int i = 0; i < 4 && wStringBuffer[i] == SYM_0; i++)
		wStringBuffer[i] = SYM_SPACE; /* trim leading zero */
}

/* convert one-byte number in a to TX_SYMBOL format, and write it to wStringBuffer + 2
 * and BGMap0 address at bc */
void WriteOneByteNumberInTxSymbol_PadSpace(u8 a, u8 b, u8 c)
{
	TwoByteNumberToTxSymbol_PadSpace_Bank1(a);
}

/* convert one-byte two-digit number in a (0-99) to TX_SYMBOL format,
 * and write it to wStringBuffer + 3 and BGMap0 address at bc */
void WriteTwoDigitNumberInTxSymbol_PadSpace(u8 a, u8 b, u8 c)
{
	TwoByteNumberToTxSymbol_PadSpace_Bank1(a);
}

/* ---------------------------------------------------------------- peek (engine/menus/duel.asm) */

static void DrawYourOrOppPlayAreaScreen(u8 h, u8 l)
{
	wCheckMenuPlayAreaWhichDuelist = h;
	wCheckMenuPlayAreaWhichLayout = l;
	EmptyScreen();
	tcg_ui_screen(TCG_SCREEN_PLAY_AREA, 0, TCG_PLAY_AREA_PEEK);
}

/* the player's Peek selection (Mankey's Peek): pick a prize card, the opponent's hand
 * (a random card of it) or the top card of a deck, of either play area.
 * returns wce5c: $40 + prize index, $7f for the deck, or the hand card's deck index,
 * with bit 7 set if the opponent's play area was chosen (turn swapped) */
u8 HandlePeekSelection(void)
{
	wce5c = 0;
	wIsSwapTurnPending = 0;
	DrawYourOrOppPlayAreaScreen(hWhoseTurn, hWhoseTurn);
	for (;;) {
		/* check_swap */
		if (wIsSwapTurnPending) {
			SwapTurn();
			wIsSwapTurnPending = 0;
		}
		static const struct MenuParams PlayAreaMenuParameters = {
			1, 14, 2, 2, SYM_CURSOR_R, SYM_SPACE, 0, TCG_MENU_PEEK_AREA,
		};
		InitializeMenuParameters(0, &PlayAreaMenuParameters);
		do
			HandleMenuInput();
		while (hCurMenuItem == MENU_CANCEL); /* can't use B btn */
		bool other = hCurMenuItem != 0;
		if (!other) {
			if (wCheckMenuPlayAreaWhichDuelist != hWhoseTurn) {
				DrawYourOrOppPlayAreaScreen(hWhoseTurn, hWhoseTurn);
				wIsSwapTurnPending = 0;
			}
		} else {
			/* PrepareYourPlayAreaSelection */
			if (wCheckMenuPlayAreaWhichDuelist == hWhoseTurn)
				DrawYourOrOppPlayAreaScreen(hWhoseTurn == PLAYER_TURN ? OPPONENT_TURN : PLAYER_TURN, hWhoseTurn);
			SwapTurn();
			wIsSwapTurnPending = TRUE; /* mark pending to swap turn */
		}
		wYourOrOppPlayAreaCurPosition = 0;
		for (;;) {
			/* loop_input_2: 0-5 prize cards, 6 hand (opponent's play area only), 7 deck */
			TcgRequest r = { 0 };
			r.type = TCG_REQ_MENU;
			r.menu = TCG_MENU_PEEK_TARGET;
			r.num_items = 8;
			r.cursor = wYourOrOppPlayAreaCurPosition;
			r.arg[0] = GetTurnDuelistVariable(DUELVARS_PRIZES);
			r.arg[1] = other;
			r.text = WhichCardWouldYouLikeToSeeText;
			int c = request(&r);
			if (c < 0 || c > 7)
				break; /* selection_cancelled: back to the play area menu */
			if (c == 6 && !other)
				continue; /* not reachable with PeekYourPlayAreaTransitionTable */
			set_keys(PAD_A);
			wYourOrOppPlayAreaCurPosition = (u8)c;
			u8 card;
			if (c < 6) {
				/* SelectedPrize: return if prize card taken */
				if (!(GetTurnDuelistVariable(DUELVARS_PRIZES) & (1 << c)))
					continue;
				wce5c = (u8)(c + 0x40);
				card = GetTurnDuelistVariable((u8)(c + DUELVARS_PRIZE_CARDS));
			} else if (c == 6) {
				/* SelectedOppsHand */
				u8 n;
				if (CreateHandCardList(&n))
					continue;
				ShuffleCards(wDuelTempList, n);
				card = wDuelTempList[0];
			} else {
				/* SelectedDeck */
				if (CreateDeckCardList(NULL))
					continue;
				wce5c = 0x7f;
				card = wDuelTempList[0];
			}
			/* ShowSelectedCard */
			if (wce5c == 0)
				wce5c = card;
			LoadCardDataToBuffer1_FromDeckIndex(card);
			OpenCardPage_FromHand();
			if (wIsSwapTurnPending) {
				SwapTurn();
				return (u8)(wce5c | 0x80);
			}
			return wce5c;
		}
	}
}

/* show the card the AI chose with Peek (b = AI_PEEK_TARGET_* value) */
void DrawAIPeekScreen(u8 b)
{
	wIsSwapTurnPending = 0;
	u8 l = hWhoseTurn;
	u8 a = hWhoseTurn;
	if (b & (1 << AI_PEEK_TARGET_HAND_F)) {
		/* AI chose the hand */
		SwapTurn();
		wIsSwapTurnPending = TRUE;
		a = hWhoseTurn;
	}
	DrawYourOrOppPlayAreaScreen(a, l);
	u8 t = b & 0x7f;
	if (t == 0x7f)
		wYourOrOppPlayAreaCurPosition = 7; /* cursor on the deck */
	else if (t & (1 << AI_PEEK_TARGET_PRIZE_F))
		wYourOrOppPlayAreaCurPosition = t & 0x3f;
	else
		wYourOrOppPlayAreaCurPosition = 6;
	if (wIsSwapTurnPending)
		SwapTurn();
}
