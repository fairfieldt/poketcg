#ifndef N64_DUEL_BRIDGE_H
#define N64_DUEL_BRIDGE_H
#include "tcg.h"
#include <stdbool.h>
void n64_ui_init(void);
int n64_duel_play(const TcgDuelSetup *setup, const uint8_t *saved);
void n64_message(const char *title,const char *text);
void n64_ui_sanitize_text(const char *text,char *out,unsigned capacity);
int n64_ui_wrap_text(const char *text,char lines[][37],unsigned capacity);
int n64_ui_wrap_columns(const char *text,unsigned columns,char lines[][37],unsigned capacity);
int n64_menu(const char *title,const char *const *items,int count,int initial,bool cancel);
void n64_card_detail(unsigned id);
void n64_card_name(unsigned id,char *out,int capacity);
void n64_duel_rng(uint8_t out[3]);
void n64_duel_automated(bool enabled);
struct surface_s *n64_ui_frame(void);
void n64_map_label(struct surface_s *frame,const char *name);
void n64_ui_text_speed(unsigned speed);
/* Static CPU-rendered phases in the DIAGNOSTIC=2 video test ROM only. */
enum { N64_VIDEO_DIALOGUE, N64_VIDEO_DUEL, N64_VIDEO_CARD };
void n64_ui_video_probe(unsigned phase,unsigned frames);
#endif
