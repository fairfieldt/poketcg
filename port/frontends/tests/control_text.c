#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "engine.h"
#include "../control_text.h"

static int layout = -1;
static char shown[1536];
static unsigned shown_mode,answers;

const char *tcg_text_override(uint16_t id)
{ return layout < 0 ? NULL : tcg_control_text((enum TcgControlLayout)layout,id); }

static void event(void *ctx,const TcgEvent *ev)
{
	(void)ctx;
	if(ev->type != TCG_EV_TEXT) return;
	snprintf(shown,sizeof(shown),"%s",ev->str ? ev->str : "");
	shown_mode=ev->arg[1];
}
static int request(void *ctx,const TcgRequest *req)
{
	(void)ctx;
	assert(req->type == TCG_REQ_ACK);
	answers++;
	return 0;
}
static void expect_control(const char *text)
{
	if(layout < 0) assert(strstr(text,"SELECT"));
	else {
		assert(!strstr(text,"SELECT"));
		assert(strstr(text,layout == TCG_CONTROLS_N64 ? "Press Z" : "Press C"));
	}
}
int main(void)
{
	TcgHost host={.event=event,.request=request};tcg_set_host(&host);
	uint8_t source_wram[WRAM_SIZE],source_hram[HRAM_SIZE];
	assert(strstr(gTexts[SamTurn4Instr2PracticeDuelText],"SELECT"));
	assert(strstr(gTexts[UsefulButtonsBookText],"B + Down"));
	for(layout=-1;layout<=TCG_CONTROLS_WEB;layout++) {
		memset(gWRAM,0,sizeof(gWRAM));memset(gHRAM,0,sizeof(gHRAM));
		for(unsigned id=0;id<NUM_TEXTS;id++) {
			bool replaced=layout>=0 && (id==SamTurn4Instr2PracticeDuelText ||
				id==SamTurn4DrMason2PracticeDuelText || id==UsefulButtonsBookText);
			assert((tcg_text(id)!=gTexts[id])==replaced);
		}
		assert(!*tcg_text(NUM_TEXTS) && !*tcg_text(UINT16_MAX));
		char formatted[1536];
		tcg_format_text(SamTurn4DrMason2PracticeDuelText,formatted,sizeof(formatted));
		expect_control(formatted);
		tcg_ui_practice_speech(SamTurn4DrMason2PracticeDuelText);
		assert(shown_mode==TCG_TEXT_PRACTICE_SPEECH);expect_control(shown);
		const u16 steps[]={SamTurn4Instr1PracticeDuelText,SamTurn4Instr2PracticeDuelText};
		tcg_ui_practice_checklist(ReplaceDueToKnockoutPracticeDuelText,steps,2);
		assert(shown_mode==TCG_TEXT_PRACTICE_CHECKLIST);
		assert(strstr(shown,"Staryu"));expect_control(shown);
		tcg_format_text(UsefulButtonsBookText,formatted,sizeof(formatted));
		if(layout<0) assert(strstr(formatted,"B + Down"));
		else {
			assert(!strstr(formatted,"B + Down"));
			assert(!strstr(formatted,"SELECT"));
			assert(strstr(formatted,layout==TCG_CONTROLS_N64 ? "press Z" : "choose Check"));
			if(layout==TCG_CONTROLS_WEB) assert(!strstr(formatted,"START") && !strstr(formatted,"press Z"));
		}
		/* "Select" as an ordinary menu verb and real N64 Start/A/B instructions
		 * retain their source wording. */
		assert(tcg_text(SelectStaryuPracticeDuelText)==gTexts[SelectStaryuPracticeDuelText]);
		assert(tcg_text(Tech5DiaryAndEmailExplanationText)==gTexts[Tech5DiaryAndEmailExplanationText]);
		if(layout<0) {
			memcpy(source_wram,gWRAM,sizeof(source_wram));
			memcpy(source_hram,gHRAM,sizeof(source_hram));
		} else {
			assert(!memcmp(source_wram,gWRAM,sizeof(source_wram)));
			assert(!memcmp(source_hram,gHRAM,sizeof(source_hram)));
		}
	}
	assert(answers==3);
	puts("CONTROL TEXT PASS: source/N64/web lookup, all text IDs, composed lesson/checklist/book, unchanged engine state");
	return 0;
}
