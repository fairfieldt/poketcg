/*
 * endian_check: runs a set of deterministic duels with the engine and prints a
 * digest of everything observable (every event and request with its strings,
 * every answer, the final WRAM/HRAM). Built natively and for big-endian MIPS
 * (run under qemu-mips), the two outputs must be identical if the engine has
 * no byte order dependency (make endiancheck).
 *
 * The duelists answer requests with a seeded random policy (like tcgcli's
 * "random" duelist); duels run human-vs-human, human-vs-AI and AI-vs-AI.
 * An engine fatal error (e.g. an effect that is not ported yet) ends that duel
 * and is part of the digest.
 *
 * With -DENDIAN_CHECK_FREESTANDING this file brings its own tiny runtime
 * (Linux o32 syscalls, mem and str functions), so that it links without a
 * MIPS C library.
 */
#include <stddef.h>
#include <stdint.h>

#include "tcg.h"
#include "common.h"

#ifdef ENDIAN_CHECK_FREESTANDING
/* ---------------------------------------------------------------- freestanding runtime (MIPS o32 Linux) */
static long sys3(long n, long a, long b, long c)
{
	register long v0 __asm__("$2") = n;
	register long a0 __asm__("$4") = a;
	register long a1 __asm__("$5") = b;
	register long a2 __asm__("$6") = c;
	register long a3 __asm__("$7");
	__asm__ volatile("syscall"
	                 : "+r"(v0), "=r"(a3)
	                 : "r"(a0), "r"(a1), "r"(a2)
	                 : "memory", "$1", "$3", "$8", "$9", "$10", "$11", "$12", "$13", "$14", "$15", "$24", "$25",
	                   "hi", "lo");
	return v0;
}
static void sys_write(const void *p, size_t n) { sys3(4004, 1, (long)p, (long)n); }
static void sys_exit(int code)
{
	for (;;)
		sys3(4001, code, 0, 0);
}
void *memcpy(void *d, const void *s, size_t n)
{
	unsigned char *a = d;
	const unsigned char *b = s;
	while (n--)
		*a++ = *b++;
	return d;
}
void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *a = d;
	const unsigned char *b = s;
	if (a < b)
		while (n--)
			*a++ = *b++;
	else
		while (n--)
			a[n] = b[n];
	return d;
}
void *memset(void *d, int c, size_t n)
{
	unsigned char *a = d;
	while (n--)
		*a++ = (unsigned char)c;
	return d;
}
int memcmp(const void *x, const void *y, size_t n)
{
	const unsigned char *a = x, *b = y;
	for (; n--; a++, b++)
		if (*a != *b)
			return *a - *b;
	return 0;
}
size_t strlen(const char *s)
{
	size_t n = 0;
	while (s[n])
		n++;
	return n;
}
int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b)
			return (unsigned char)*a - (unsigned char)*b;
		if (!*a)
			return 0;
	}
	return 0;
}
int main(void);
void __start(void) { sys_exit(main()); }
#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void sys_write(const void *p, size_t n) { fwrite(p, 1, n, stdout); }
#endif

/* ---------------------------------------------------------------- output */

static void out(const char *s) { sys_write(s, strlen(s)); }

static void out_num(const char *label, uint32_t v, int hex)
{
	char buf[16];
	int n = 0;
	do {
		uint32_t d = hex ? v % 16 : v % 10;
		buf[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
		v = hex ? v / 16 : v / 10;
	} while (v || (hex && n < 8));
	out(label);
	while (n)
		sys_write(&buf[--n], 1);
}

/* ---------------------------------------------------------------- digest */

static uint32_t g_hash, g_events, g_requests, g_rng;

static void mix(const void *p, size_t n)
{
	const uint8_t *b = p;
	while (n--)
		g_hash = (g_hash ^ *b++) * 16777619u;
}

static void mix8(uint8_t v) { mix(&v, 1); }

static void mix16(uint16_t v)
{
	mix8((uint8_t)v);
	mix8((uint8_t)(v >> 8));
}

static uint32_t hash_bytes(const uint8_t *p, size_t n)
{
	uint32_t h = 2166136261u;
	while (n--)
		h = (h ^ *p++) * 16777619u;
	return h;
}

static uint32_t rnd(uint32_t n)
{
	g_rng ^= g_rng << 13;
	g_rng ^= g_rng >> 17;
	g_rng ^= g_rng << 5;
	return n ? g_rng % n : 0;
}

/* ---------------------------------------------------------------- host callbacks */

static void *g_jmp[5];
static const char *g_fatal;

void tcg_fatal(const char *msg)
{
	g_fatal = msg;
	__builtin_longjmp(g_jmp, 1);
}

static void on_event(void *ctx, const TcgEvent *ev)
{
	(void)ctx;
	g_events++;
	mix8(ev->type);
	mix(ev->arg, 4);
	mix16(ev->text);
	if (ev->str)
		mix(ev->str, strlen(ev->str) + 1);
}

static int answer(const TcgRequest *r)
{
	switch (r->type) {
	case TCG_REQ_ACK:
		return 0;
	case TCG_REQ_DUEL_MENU: {
		static const int weights[6] = { 40, 30, 1, 8, 6, 15 };
		int t = (int)rnd(100);
		for (int i = 0; i < 6; i++) {
			if (t < weights[i])
				return i;
			t -= weights[i];
		}
		return 5;
	}
	case TCG_REQ_YES_NO:
		return rnd(4) != 0;
	case TCG_REQ_CARD_LIST:
		if (r->arg[0] || !r->num_items || rnd(8) == 0)
			return TCG_CANCEL;
		return (int)rnd(r->num_items);
	case TCG_REQ_CARD_ITEM_MENU:
		return rnd(10) == 0 ? 1 : 0;
	case TCG_REQ_PLAY_AREA:
		if (r->arg[1] || rnd(10) == 0)
			return TCG_CANCEL;
		return r->arg[0] + (int)rnd(r->num_items ? r->num_items : 1);
	case TCG_REQ_PRIZE:
		return (int)rnd(6);
	default:
		if (!r->num_items || rnd(10) == 0)
			return TCG_CANCEL;
		return (int)rnd(r->num_items);
	}
}

static int on_request(void *ctx, const TcgRequest *r)
{
	(void)ctx;
	g_requests++;
	if (g_requests > 60000) {
		g_fatal = "request limit";
		__builtin_longjmp(g_jmp, 1);
	}
	mix8(r->type);
	mix8(r->duelist);
	mix8(r->shown);
	mix8(r->menu);
	mix8(r->num_items);
	mix8(r->cursor);
	mix16(r->text);
	mix16(r->text2);
	mix(r->arg, 4);
	if (r->str)
		mix(r->str, strlen(r->str) + 1);
	if (r->items)
		mix(r->items, r->num_items * (r->type == TCG_REQ_MENU && r->menu == TCG_MENU_ATTACKS ? 2 : 1));
	int a = answer(r);
	mix16((uint16_t)a);
	return a;
}

/* ---------------------------------------------------------------- main */

static const uint8_t kDecks[] = {
	CHARMANDER_AND_FRIENDS_DECK_ID, SQUIRTLE_AND_FRIENDS_DECK_ID, BULBASAUR_AND_FRIENDS_DECK_ID,
	LIGHTNING_AND_FIRE_DECK_ID, WATER_AND_FIGHTING_DECK_ID, GRASS_AND_PSYCHIC_DECK_ID,
	FIRST_STRIKE_DECK_ID, ROCK_CRUSHER_DECK_ID, GO_GO_RAIN_DANCE_DECK_ID, ZAPPING_SELFDESTRUCT_DECK_ID,
	FLOWER_POWER_DECK_ID, STRANGE_PSYSHOCK_DECK_ID, WONDERS_OF_SCIENCE_DECK_ID, FIRE_CHARGE_DECK_ID,
	IM_RONALD_DECK_ID, LEGENDARY_MOLTRES_DECK_ID, IMAKUNI_DECK_ID, GHOST_DECK_ID,
};

int main(void)
{
	static const char *const modes[3] = { "human-human", "human-ai", "ai-ai" };
	TcgHost host = { on_event, on_request, NULL, NULL };
	tcg_set_host(&host);
	uint32_t total = 2166136261u;
	int duels = 0;
	for (int mode = 0; mode < 3; mode++) {
		for (uint32_t seed = 1; seed <= 12; seed++) {
			TcgDuelSetup s;
			memset(&s, 0, sizeof(s));
			s.prizes = 6;
			s.rng[0] = (uint8_t)(seed * 37);
			s.rng[1] = (uint8_t)(seed * 101 + mode);
			s.rng[2] = (uint8_t)seed;
			s.duelist[0].kind = mode == 2 ? TCG_AI : TCG_HUMAN;
			s.duelist[1].kind = mode == 0 ? TCG_HUMAN : TCG_AI;
			s.duelist[0].deck_id = kDecks[(seed * 7) % sizeof(kDecks)];
			s.duelist[1].deck_id = kDecks[(seed * 11 + 3) % sizeof(kDecks)];
			s.duelist[0].name = "P1";
			s.duelist[1].name = "P2";
			g_hash = 2166136261u;
			g_events = g_requests = 0;
			g_rng = seed * 2654435761u + (uint32_t)mode;
			g_fatal = NULL;
			int result = -1;
			if (__builtin_setjmp(g_jmp) == 0)
				result = tcg_run_duel(&s);
			const uint8_t *w = tcg_wram();
			out(modes[mode]);
			out_num(" seed=", seed, 0);
			out_num(" result=", (uint32_t)result, 0);
			out_num(" turns=", w[ADDR_wDuelTurns - 0xc000], 0);
			out_num(" events=", g_events, 0);
			out_num(" requests=", g_requests, 0);
			out_num(" stream=", g_hash, 1);
			out_num(" wram=", hash_bytes(tcg_wram(), 0x2000), 1);
			out_num(" hram=", hash_bytes(tcg_hram(), 0x80), 1);
			if (g_fatal) {
				out(" stopped: ");
				out(g_fatal);
			}
			out("\n");
			total = (total ^ g_hash) * 16777619u;
			duels++;
		}
	}
	out_num("duels=", (uint32_t)duels, 0);
	out_num(" digest=", total, 1);
	out("\n");
	return 0;
}
