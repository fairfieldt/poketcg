/*
 * gbtest.c - test driver for the gb core.
 *
 *   gbtest selftest                        targeted unit checks (timer, serial, LCD, call stack, ...)
 *   gbtest blargg  ROM [max_frames]        run a Blargg test ROM, print serial output;
 *                                          exit 0 iff it printed "Passed"
 *   gbtest bench   ROM [frames] [render]   measure emulated frames per second
 *   gbtest shot    ROM frames out.png      run with no input, save a screenshot
 *   gbtest poketcg ROM SYM OUTDIR          boot checks for Pokemon TCG (see below)
 */
#define _POSIX_C_SOURCE 199309L
#include "gb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint8_t *load_file(const char *path, size_t *size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror(path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *buf = malloc(n > 0 ? (size_t)n : 1);
    if (!buf || fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        fprintf(stderr, "%s: read error\n", path);
        fclose(fp);
        free(buf);
        return NULL;
    }
    fclose(fp);
    *size = (size_t)n;
    return buf;
}

static GB *open_rom(const char *path)
{
    size_t size;
    uint8_t *rom = load_file(path, &size);
    if (!rom)
        return NULL;
    GB *gb = gb_new(rom, size);
    free(rom);
    if (!gb)
        fprintf(stderr, "%s: gb_new failed\n", path);
    return gb;
}

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------ */
/* blargg                                                             */

typedef struct { char buf[8192]; size_t len; } SerialLog;

static void serial_capture(GB *gb, uint8_t byte, void *user)
{
    (void)gb;
    SerialLog *log = user;
    if (log->len + 1 < sizeof log->buf) {
        log->buf[log->len++] = (char)byte;
        log->buf[log->len] = 0;
    }
}

static int cmd_blargg(const char *path, long max_frames)
{
    GB *gb = open_rom(path);
    if (!gb)
        return 2;
    gb->render = 0;
    SerialLog log = { .len = 0 };
    log.buf[0] = 0;
    gb->serial_cb = serial_capture;
    gb->serial_user = &log;
    int result = 1;
    for (long f = 0; f < max_frames; f++) {
        int r = gb_run(gb, 70224 * 2, 1);
        if (r == GB_STOP_ERROR) {
            printf("%s\nERROR: %s\n", log.buf, gb_error_string(gb));
            result = 2;
            break;
        }
        if (strstr(log.buf, "Passed")) {
            result = 0;
            break;
        }
        if (strstr(log.buf, "Failed"))
            break;
    }
    printf("%s: %s\n---\n%s\n---\n", path, result == 0 ? "PASS" : "FAIL", log.buf);
    gb_free(gb);
    return result;
}

/* ------------------------------------------------------------------ */
/* bench                                                              */

static int cmd_bench(const char *path, long frames, int render)
{
    GB *gb = open_rom(path);
    if (!gb)
        return 2;
    gb->render = render;
    double t0 = now_sec();
    uint64_t c0 = gb->cycles;
    for (long f = 0; f < frames; f++) {
        int r = gb_run(gb, 70224 * 4, 1);
        if (r == GB_STOP_ERROR) {
            printf("ERROR: %s\n", gb_error_string(gb));
            break;
        }
    }
    double dt = now_sec() - t0;
    double cyc = (double)(gb->cycles - c0);
    printf("render=%d: %llu frames (%.0f cycles) in %.3fs: %.0f frames/s, %.1fx real time\n",
           render, (unsigned long long)gb->frames, cyc, dt, gb->frames / dt,
           cyc / dt / 4194304.0);
    gb_free(gb);
    return 0;
}

static int cmd_shot(const char *path, long frames, const char *out)
{
    GB *gb = open_rom(path);
    if (!gb)
        return 2;
    for (long f = 0; f < frames; f++)
        if (gb_run(gb, 70224 * 4, 1) == GB_STOP_ERROR) {
            printf("ERROR: %s\n", gb_error_string(gb));
            break;
        }
    int r = gb_write_png(gb, out);
    printf("wrote %s (%d)\n", out, r);
    gb_free(gb);
    return r ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* poketcg                                                            */

typedef struct { int bank; uint16_t addr; char name[128]; } Sym;
static Sym *syms;
static int nsyms;

static int load_syms(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror(path);
        return -1;
    }
    char line[256];
    int cap = 0;
    while (fgets(line, sizeof line, fp)) {
        unsigned bank, addr;
        char name[128];
        if (sscanf(line, "%x:%x %127s", &bank, &addr, name) != 3)
            continue;
        if (nsyms == cap) {
            cap = cap ? cap * 2 : 4096;
            syms = realloc(syms, sizeof *syms * (size_t)cap);
        }
        syms[nsyms].bank = (int)bank;
        syms[nsyms].addr = (uint16_t)addr;
        snprintf(syms[nsyms].name, sizeof syms[nsyms].name, "%s", name);
        nsyms++;
    }
    fclose(fp);
    return 0;
}

static const Sym *find_sym(const char *name)
{
    for (int i = 0; i < nsyms; i++)
        if (!strcmp(syms[i].name, name))
            return &syms[i];
    return NULL;
}

/* nearest symbol at or below (bank, addr) */
static const char *sym_name(int bank, uint16_t addr, char *buf, size_t n)
{
    const Sym *best = NULL;
    for (int i = 0; i < nsyms; i++) {
        const Sym *s = &syms[i];
        if (addr >= 0x4000 && addr < 0x8000 && s->bank != bank)
            continue;
        if (addr < 0x4000 && s->bank != 0)
            continue;
        if (s->addr <= addr && (!best || s->addr > best->addr))
            best = s;
    }
    if (!best)
        snprintf(buf, n, "%02x:%04x", bank, addr);
    else if (best->addr == addr)
        snprintf(buf, n, "%s", best->name);
    else
        snprintf(buf, n, "%s+%u", best->name, (unsigned)(addr - best->addr));
    return buf;
}

static uint32_t fnv(const void *p, size_t n, uint32_t h)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

static uint32_t machine_hash(GB *gb)
{
    uint32_t h = 2166136261u;
    h = fnv(gb->wram, sizeof gb->wram, h);
    h = fnv(gb->hram, sizeof gb->hram, h);
    h = fnv(gb->vram, sizeof gb->vram, h);
    h = fnv(gb->oam, sizeof gb->oam, h);
    h = fnv(gb->r, sizeof gb->r, h);
    h = fnv(&gb->pc, sizeof gb->pc, h);
    h = fnv(&gb->sp, sizeof gb->sp, h);
    h = fnv(&gb->cycles, sizeof gb->cycles, h);
    return h;
}

typedef struct {
    int bp_vblank, bp_gameloop;
    long vblank_hits, gameloop_hits;
    int max_depth;
    long bad_vblank_frames;
} Stats;

/* run one frame, servicing breakpoints */
static int run_frame(GB *gb, Stats *st)
{
    for (;;) {
        int r = gb_run(gb, 70224 * 4, 1);
        if (gb->cs_depth > st->max_depth)
            st->max_depth = gb->cs_depth;
        if (r == GB_STOP_BREAKPOINT) {
            if (gb->hit_breakpoint == st->bp_vblank) {
                st->vblank_hits++;
                /* VBlankHandler is reached via the $0040 vector (jp VBlankHandler) */
                const GBFrame *top = gb_callstack_top(gb);
                if (!top || !top->is_interrupt || top->target != 0x0040 || top->sp != gb->sp)
                    st->bad_vblank_frames++;
            } else if (gb->hit_breakpoint == st->bp_gameloop) {
                st->gameloop_hits++;
            }
            continue;
        }
        return r;
    }
}

static void print_callstack(GB *gb)
{
    char buf[200];
    printf("  callstack depth %d (dropped %u):\n", gb->cs_depth, gb->cs_dropped);
    for (int i = gb->cs_depth - 1; i >= 0 && i >= gb->cs_depth - 8; i--) {
        const GBFrame *fr = &gb->cs[i];
        printf("    [%d] %s%s ret=%04x sp=%04x bank=%d\n", i, fr->is_interrupt ? "IRQ " : "",
               sym_name(fr->target < 0x4000 ? 0 : fr->bank, fr->target, buf, sizeof buf),
               fr->ret, fr->sp, fr->bank);
    }
    printf("  pc=%s (%02x:%04x)\n", sym_name(gb_rom_bank(gb), gb->pc, buf, sizeof buf),
           gb_bank_of(gb, gb->pc), gb->pc);
}

static int cmd_poketcg(const char *rom, const char *sym, const char *outdir)
{
    if (load_syms(sym))
        return 2;
    GB *gb = open_rom(rom);
    if (!gb)
        return 2;
    const Sym *vb = find_sym("VBlankHandler"), *gl = find_sym("GameLoop");
    if (!vb || !gl) {
        fprintf(stderr, "symbols not found\n");
        return 2;
    }
    Stats st = { 0 };
    st.bp_vblank = gb_add_breakpoint(gb, vb->bank, vb->addr);
    st.bp_gameloop = gb_add_breakpoint(gb, gl->bank, gl->addr);
    char path[1024];
    int fails = 0;

    /* phase 1: 600 frames, no input */
    double t0 = now_sec();
    for (int f = 0; f < 600; f++) {
        int r = run_frame(gb, &st);
        if (r == GB_STOP_ERROR) {
            printf("ERROR at frame %d: %s\n", f, gb_error_string(gb));
            print_callstack(gb);
            return 1;
        }
    }
    snprintf(path, sizeof path, "%s/poketcg_600.png", outdir);
    gb_write_png(gb, path);
    printf("phase 1: frames=%llu cycles=%llu (%.2fs) vblank_hits=%ld gameloop_hits=%ld max_depth=%d bad_vblank=%ld -> %s\n",
           (unsigned long long)gb->frames, (unsigned long long)gb->cycles, now_sec() - t0,
           st.vblank_hits, st.gameloop_hits, st.max_depth, st.bad_vblank_frames, path);
    print_callstack(gb);
    if (st.gameloop_hits < 1 || st.vblank_hits < 500 || st.bad_vblank_frames)
        fails++;

    /* phase 2: press START / A periodically, screenshots along the way */
    long vb_before = st.vblank_hits;
    for (int f = 0; f < 3000; f++) {
        int phase = f % 90;
        uint8_t btn = 0;
        if (phase < 6)
            btn = (f / 90) % 2 ? GB_BTN_A : GB_BTN_START;
        gb_set_buttons(gb, btn);
        int r = run_frame(gb, &st);
        if (r == GB_STOP_ERROR) {
            printf("ERROR at phase-2 frame %d: %s\n", f, gb_error_string(gb));
            print_callstack(gb);
            return 1;
        }
        if (f % 500 == 499) {
            snprintf(path, sizeof path, "%s/poketcg_input_%04d.png", outdir, f + 1);
            gb_write_png(gb, path);
            printf("  shot %s (frame %llu, depth %d)\n", path, (unsigned long long)gb->frames, gb->cs_depth);
        }
    }
    gb_set_buttons(gb, 0);
    printf("phase 2: frames=%llu vblank_hits(+%ld) max_depth=%d bad_vblank=%ld dropped=%u\n",
           (unsigned long long)gb->frames, st.vblank_hits - vb_before, st.max_depth,
           st.bad_vblank_frames, gb->cs_dropped);
    print_callstack(gb);
    if (st.vblank_hits - vb_before < 2500 || st.bad_vblank_frames)
        fails++;

    /* phase 3: snapshot determinism */
    size_t ss = gb_state_size();
    void *snap = malloc(ss);
    gb_save_state(gb, snap);
    uint32_t h1[3], h2[3];
    for (int k = 0; k < 3; k++) {
        for (int f = 0; f < 100; f++) {
            gb_set_buttons(gb, (f % 40) < 3 ? GB_BTN_A : 0);
            run_frame(gb, &st);
        }
        h1[k] = machine_hash(gb);
    }
    gb_load_state(gb, snap);
    for (int k = 0; k < 3; k++) {
        for (int f = 0; f < 100; f++) {
            gb_set_buttons(gb, (f % 40) < 3 ? GB_BTN_A : 0);
            run_frame(gb, &st);
        }
        h2[k] = machine_hash(gb);
    }
    int det = !memcmp(h1, h2, sizeof h1);
    printf("phase 3: state size %zu bytes, replay after restore %s (%08x %08x %08x)\n", ss,
           det ? "identical" : "DIVERGED", h1[0], h1[1], h1[2]);
    if (!det)
        fails++;
    free(snap);

    /* phase 4: breakpoint resume semantics */
    {
        GB *g2 = open_rom(rom);
        g2->render = 0;
        int id = gb_add_breakpoint(g2, vb->bank, vb->addr);
        long hits = 0;
        int ok = 1;
        while (g2->frames < 300) {
            int r = gb_run(g2, 70224 * 300, 0);
            if (r == GB_STOP_BREAKPOINT) {
                if (g2->hit_breakpoint != id || g2->pc != vb->addr)
                    ok = 0;
                hits++;
                /* resume without touching PC: must not re-trigger immediately */
                uint64_t c = g2->cycles;
                r = gb_run(g2, 1, 0);
                if (r == GB_STOP_BREAKPOINT || g2->cycles == c)
                    ok = 0;
            } else if (r != GB_STOP_CYCLES) {
                ok = 0;
                break;
            }
        }
        /* PC change honors breakpoint at new PC */
        int id2 = gb_add_breakpoint(g2, 0, 0x0150);
        g2->ime = 0;
        g2->ei_pending = 0;
        g2->pc = 0x0150;
        int r = gb_run(g2, 100, 0);
        if (r != GB_STOP_BREAKPOINT || g2->hit_breakpoint != id2)
            ok = 0;
        printf("phase 4: breakpoint semantics %s (%ld VBlankHandler hits in 300 frames)\n",
               ok ? "ok" : "BROKEN", hits);
        if (!ok)
            fails++;
        gb_free(g2);
    }

    gb_free(gb);
    printf("poketcg: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* selftest: targeted checks for behaviour Blargg's ROMs do not cover */

static int st_fail;
#define CHECK(cond) do { if (!(cond)) { printf("  FAIL %s:%d: %s\n", __func__, __LINE__, #cond); st_fail++; } } while (0)

/* 64KB MBC5+RAM+BATTERY ROM, 4 banks; bank n (n>=1) starts with byte n at $4000 */
static uint8_t st_rom[0x10000];

static GB *st_new(const uint8_t *code, size_t n, uint16_t at)
{
    memset(st_rom, 0, sizeof st_rom);
    st_rom[0x147] = 0x1B;
    st_rom[0x148] = 0x01;
    st_rom[0x149] = 0x03;
    for (int b = 1; b < 4; b++)
        st_rom[b * 0x4000] = (uint8_t)b;
    if (code)
        memcpy(st_rom + at, code, n);
    GB *gb = gb_new(st_rom, sizeof st_rom);
    gb->render = 0;
    gb->pc = at;
    return gb;
}

static void st_ei_di(void)
{
    static const uint8_t code[] = { 0xFB, 0xF3, 0x00, 0x00 }; /* ei; di; nop */
    GB *gb = st_new(code, sizeof code, 0x150);
    gb->ie = GB_INT_TIMER;
    gb->io[0x0F] = GB_INT_TIMER;
    gb_step(gb); gb_step(gb); gb_step(gb);
    CHECK(gb->pc == 0x153 && !gb->ime);
    gb_free(gb);

    static const uint8_t code2[] = { 0xFB, 0x00, 0x00 }; /* ei; nop; nop */
    gb = st_new(code2, sizeof code2, 0x150);
    gb->ie = GB_INT_TIMER;
    gb->io[0x0F] = GB_INT_TIMER;
    gb_step(gb);
    CHECK(!gb->ime && gb->pc == 0x151);
    gb_step(gb); /* nop after EI still executes */
    CHECK(gb->ime && gb->pc == 0x152);
    uint64_t c = gb->cycles;
    gb_step(gb); /* dispatch */
    CHECK(gb->pc == 0x50 && gb->cycles - c == 20 && !gb->ime && !(gb->io[0x0F] & GB_INT_TIMER));
    CHECK(gb_read16(gb, gb->sp) == 0x152);
    CHECK(gb->cs_depth == 1 && gb->cs[0].is_interrupt && gb->cs[0].target == 0x50 &&
          gb->cs[0].ret == 0x152 && gb->cs[0].sp == gb->sp);
    gb_free(gb);
}

static void st_priority(void)
{
    GB *gb = st_new(NULL, 0, 0x150);
    gb->ie = 0x1F;
    gb->io[0x0F] = 0x1F;
    gb->ime = 1;
    static const uint16_t vec[5] = { 0x40, 0x48, 0x50, 0x58, 0x60 };
    for (int i = 0; i < 5; i++) {
        gb->pc = 0x150;
        gb->ime = 1;
        gb_step(gb);
        CHECK(gb->pc == vec[i]);
        CHECK(gb->io[0x0F] == (0x1F & ~((2 << i) - 1)));
    }
    gb_free(gb);
}

static void st_halt(void)
{
    static const uint8_t code[] = { 0x76, 0x3C, 0x00 }; /* halt; inc a */
    GB *gb = st_new(code, sizeof code, 0x150);
    gb->ie = GB_INT_SERIAL;
    gb_write(gb, 0xFF02, 0x81); /* serial completes after 4096 cycles */
    gb_step(gb);
    CHECK(gb->halted);
    uint64_t c = gb->cycles;
    gb_run(gb, 100000, 0);
    CHECK(gb->a == 0x02 && !gb->halted); /* woke and continued (IME=0: no dispatch) */
    CHECK(gb->cs_depth == 0 && gb->cycles - c >= 4096);
    gb_free(gb);

    /* halt bug: IME=0 with interrupt pending -> next byte executed twice */
    gb = st_new(code, sizeof code, 0x150);
    gb->a = 0;
    gb->ie = GB_INT_VBLANK;
    gb->io[0x0F] = GB_INT_VBLANK;
    gb_step(gb); /* halt (does not halt) */
    CHECK(!gb->halted);
    gb_step(gb);
    gb_step(gb);
    CHECK(gb->a == 2 && gb->pc == 0x152);
    gb_free(gb);
}

static void st_callstack(void)
{
    uint8_t code[0x40] = { 0 };
    /* 0150: call 0160 ; 0153: nop */
    code[0x00] = 0xCD; code[0x01] = 0x60; code[0x02] = 0x01;
    /* 0160: push bc; pop bc; call 0170; ret */
    code[0x10] = 0xC5; code[0x11] = 0xC1; code[0x12] = 0xCD; code[0x13] = 0x70; code[0x14] = 0x01; code[0x15] = 0xC9;
    /* 0170: pop hl (drop return address); rst $08 ; ... */
    code[0x20] = 0xE1; code[0x21] = 0xCF;
    /* 0008: ret (rst handler) */
    GB *gb = st_new(code, sizeof code, 0x150);
    gb_free(gb);
    st_rom[0x08] = 0xC9;
    gb = gb_new(st_rom, sizeof st_rom);
    gb->render = 0;
    gb->pc = 0x150;

    gb_step(gb);
    CHECK(gb->pc == 0x160 && gb->cs_depth == 1 && gb->cs[0].target == 0x160 && gb->cs[0].ret == 0x153 &&
          gb->cs[0].sp == 0xFFFC && !gb->cs[0].is_interrupt && gb->cs[0].bank == 1);
    gb_step(gb); gb_step(gb);           /* push/pop */
    CHECK(gb->cs_depth == 1);
    gb_step(gb);                        /* call 0170 */
    CHECK(gb->cs_depth == 2 && gb->cs[1].sp == 0xFFFA);
    gb_step(gb);                        /* pop hl */
    CHECK(gb->cs_depth == 1 && gb->h == 0x01 && gb->l == 0x65);
    gb_step(gb);                        /* rst 08 */
    CHECK(gb->cs_depth == 2 && gb->cs[1].target == 0x08 && gb->cs[1].ret == 0x172);
    gb_step(gb);                        /* ret from rst */
    CHECK(gb->cs_depth == 1 && gb->pc == 0x172);
    /* harness-style early return */
    gb_ret(gb);
    CHECK(gb->cs_depth == 0 && gb->pc == 0x153 && gb->sp == 0xFFFE);

    /* LD SP,nn above all frames pops everything */
    gb->sp = 0xD000;
    gb->pc = 0x150;
    gb_step(gb);
    CHECK(gb->cs_depth == 1);
    gb->wram[0] = 0x31; gb->wram[1] = 0xFE; gb->wram[2] = 0xDF; /* ld sp,$dffe */
    gb->pc = 0xC000;
    gb_step(gb);
    CHECK(gb->sp == 0xDFFE && gb->cs_depth == 0);
    gb_free(gb);
}

static void st_timer(void)
{
    GB *gb = st_new(NULL, 0, 0x150); /* NOPs */
    gb_write(gb, 0xFF04, 0);         /* DIV reset */
    gb_write(gb, 0xFF06, 0x10);      /* TMA */
    gb_write(gb, 0xFF05, 0xFE);      /* TIMA */
    gb_write(gb, 0xFF07, 0x05);      /* 262144 Hz: +1 every 16 cycles */
    gb->io[0x0F] = 0;
    gb_run(gb, 16, 0);
    CHECK(gb_read(gb, 0xFF05) == 0xFF);
    gb_run(gb, 16, 0);
    CHECK(gb_read(gb, 0xFF05) == 0x00 && !(gb->io[0x0F] & GB_INT_TIMER));
    gb_run(gb, 4, 0);
    CHECK(gb_read(gb, 0xFF05) == 0x10 && (gb->io[0x0F] & GB_INT_TIMER));
    /* DIV */
    gb_write(gb, 0xFF07, 0);
    gb_write(gb, 0xFF04, 0);
    gb_run(gb, 256 * 3, 0);
    CHECK(gb_read(gb, 0xFF04) == 3);
    /* 4096 Hz with halt fast-forward: 0x100 increments == 0x40000 cycles */
    static const uint8_t code[] = { 0x76, 0x00, 0x18, 0xFC }; /* halt; nop; jr -4 */
    gb_free(gb);
    gb = st_new(code, sizeof code, 0x150);
    gb_write(gb, 0xFF04, 0);
    gb_write(gb, 0xFF06, 0x00);
    gb_write(gb, 0xFF05, 0x00);
    gb_write(gb, 0xFF07, 0x04);
    gb_write(gb, 0xFF40, 0x00);      /* LCD off: no other events */
    gb->ie = GB_INT_TIMER;
    gb->io[0x0F] = 0;
    gb_add_breakpoint(gb, -1, 0x151);   /* instruction after HALT: stops on wake-up */
    uint64_t c0 = gb->cycles;
    int r = gb_run(gb, 0x80000, 0);
    CHECK(r == GB_STOP_BREAKPOINT && (gb->io[0x0F] & GB_INT_TIMER) && gb_read(gb, 0xFF05) == 0);
    CHECK(gb->cycles - c0 >= 0x40000 && gb->cycles - c0 <= 0x40000 + 16);
    gb_free(gb);
}

static void st_serial(void)
{
    GB *gb = st_new(NULL, 0, 0x150);
    gb->io[0x0F] = 0;
    gb_write(gb, 0xFF01, 0x42);
    gb_write(gb, 0xFF02, 0x81);
    gb_run(gb, 4092, 0);
    CHECK((gb_read(gb, 0xFF02) & 0x80) && !(gb->io[0x0F] & GB_INT_SERIAL));
    gb_run(gb, 4, 0);
    CHECK(!(gb_read(gb, 0xFF02) & 0x80) && (gb->io[0x0F] & GB_INT_SERIAL) && gb_read(gb, 0xFF01) == 0xFF);
    gb->io[0x0F] = 0;
    gb_write(gb, 0xFF02, 0x80); /* external clock: never completes */
    gb_run(gb, 1000000, 0);
    CHECK((gb_read(gb, 0xFF02) & 0x80) && !(gb->io[0x0F] & GB_INT_SERIAL));
    gb_free(gb);
}

static void st_lcd(void)
{
    GB *gb = st_new(NULL, 0, 0x150);
    CHECK(gb_read(gb, 0xFF44) == 0 && (gb_read(gb, 0xFF41) & 3) == 2);
    gb_run(gb, 80, 0);
    CHECK((gb_read(gb, 0xFF41) & 3) == 3);
    gb_run(gb, 172, 0);
    CHECK((gb_read(gb, 0xFF41) & 3) == 0);
    gb_run(gb, 204, 0);
    CHECK(gb_read(gb, 0xFF44) == 1 && (gb_read(gb, 0xFF41) & 3) == 2);
    gb->io[0x0F] = 0;
    int r = gb_run(gb, 200000, 1);
    CHECK(r == GB_STOP_FRAME && gb->frames == 1 && gb_read(gb, 0xFF44) == 144 &&
          (gb_read(gb, 0xFF41) & 3) == 1 && (gb->io[0x0F] & GB_INT_VBLANK));
    CHECK(gb->cycles == 456 * 144);
    gb_run(gb, 456 * 10, 0);
    CHECK(gb_read(gb, 0xFF44) == 0);    /* 154 lines per frame */
    gb_run(gb, 456 * 154 - 456 * 10, 1);
    CHECK(gb->frames == 2);
    /* LYC coincidence + STAT interrupt */
    gb->io[0x0F] = 0;
    gb_write(gb, 0xFF45, 10);
    gb_write(gb, 0xFF41, 0x40);
    while (!(gb->io[0x0F] & GB_INT_STAT))
        gb_run(gb, 4, 0);
    CHECK(gb_read(gb, 0xFF44) == 10 && (gb_read(gb, 0xFF41) & 4));
    /* LCD off: LY=0, mode 0, no frames / interrupts */
    gb_write(gb, 0xFF41, 0x78);
    gb_write(gb, 0xFF40, 0x11);
    gb->io[0x0F] = 0;
    uint64_t f = gb->frames;
    r = gb_run(gb, 300000, 1);
    CHECK(r == GB_STOP_CYCLES && gb->frames == f && gb_read(gb, 0xFF44) == 0 &&
          (gb_read(gb, 0xFF41) & 3) == 0 && gb->io[0x0F] == 0);
    /* back on: restart at line 0 */
    gb_write(gb, 0xFF40, 0x91);
    CHECK(gb_read(gb, 0xFF44) == 0);
    gb_run(gb, 456, 0);
    CHECK(gb_read(gb, 0xFF44) == 1);
    gb_run(gb, 456 * 143, 0);
    CHECK(gb_read(gb, 0xFF44) == 144 && gb->frames == f + 1);
    gb_free(gb);
}

static void st_breakpoints(void)
{
    uint8_t code[0x20];
    int n = 0;
    /* 0150: ld a,2; ld [2000],a; call 4000; ld a,3; ld [2000],a; call 4000; jr $ */
    code[n++] = 0x3E; code[n++] = 0x02; code[n++] = 0xEA; code[n++] = 0x00; code[n++] = 0x20;
    code[n++] = 0xCD; code[n++] = 0x00; code[n++] = 0x40;
    code[n++] = 0x3E; code[n++] = 0x03; code[n++] = 0xEA; code[n++] = 0x00; code[n++] = 0x20;
    code[n++] = 0xCD; code[n++] = 0x00; code[n++] = 0x40;
    code[n++] = 0x18; code[n++] = 0xFE;
    memset(st_rom, 0, sizeof st_rom);
    GB *gb = st_new(code, (size_t)n, 0x150);
    gb_free(gb);
    /* bank 2 / bank 3 routines at $4000: "nop; ret" (byte 0 is bank id: ld bc,... avoided) */
    st_rom[0x8000] = 0x00; st_rom[0x8001] = 0xC9;
    st_rom[0xC000] = 0x00; st_rom[0xC001] = 0xC9;
    gb = gb_new(st_rom, sizeof st_rom);
    gb->render = 0;
    gb->pc = 0x150;
    gb_write(gb, 0xFF40, 0); /* LCD off: no interrupts */
    int id3 = gb_add_breakpoint(gb, 3, 0x4000);
    int r = gb_run(gb, 10000, 0);
    CHECK(r == GB_STOP_BREAKPOINT && gb->hit_breakpoint == id3 && gb_rom_bank(gb) == 3 && gb->pc == 0x4000);
    CHECK(gb->cs_depth == 1 && gb->cs[0].bank == 3);
    r = gb_run(gb, 1000, 0);
    CHECK(r == GB_STOP_CYCLES && gb->pc == 0x160);
    /* bank -1 matches both calls; removed breakpoints are ignored */
    gb_remove_breakpoint(gb, id3);
    gb_reset(gb);
    gb->pc = 0x150;
    gb_write(gb, 0xFF40, 0);
    int idany = gb_add_breakpoint(gb, -1, 0x4000);
    r = gb_run(gb, 10000, 0);
    CHECK(r == GB_STOP_BREAKPOINT && gb->hit_breakpoint == idany && gb_rom_bank(gb) == 2);
    r = gb_run(gb, 10000, 0);
    CHECK(r == GB_STOP_BREAKPOINT && gb->hit_breakpoint == idany && gb_rom_bank(gb) == 3);
    /* resume after an interrupt that fires right away: must not re-stop at the same PC */
    gb->ie = GB_INT_SERIAL;
    gb->io[0x0F] = GB_INT_SERIAL;
    gb->ime = 1;
    /* the interrupt is dispatched before the resumed instruction runs; stop at its vector */
    int idvec = gb_add_breakpoint(gb, -1, 0x58);
    r = gb_run(gb, 10000, 0);
    CHECK(r == GB_STOP_BREAKPOINT && gb->hit_breakpoint == idvec && gb->cs_depth == 2 && gb->cs[1].is_interrupt);
    /* simulate the handler returning */
    gb_ret(gb);
    gb->ime = 1;
    CHECK(gb->pc == 0x4000 && gb->cs_depth == 1);
    r = gb_run(gb, 4, 0);
    CHECK(r == GB_STOP_CYCLES && gb->pc == 0x4001); /* bp at 4000 not re-triggered */
    gb_clear_breakpoints(gb);
    CHECK(gb->bp_active == 0);
    gb_free(gb);
}

static void st_joypad(void)
{
    GB *gb = st_new(NULL, 0, 0x150);
    gb_set_buttons(gb, GB_BTN_START | GB_BTN_RIGHT);
    gb_write(gb, 0xFF00, 0x20);
    CHECK(gb_read(gb, 0xFF00) == 0xEE);
    gb_write(gb, 0xFF00, 0x10);
    CHECK(gb_read(gb, 0xFF00) == 0xD7);
    gb_write(gb, 0xFF00, 0x30);
    CHECK(gb_read(gb, 0xFF00) == 0xFF);
    gb->io[0x0F] = 0;
    gb_write(gb, 0xFF00, 0x10);
    CHECK(gb->io[0x0F] & GB_INT_JOYPAD); /* selecting a pressed line = falling edge */
    gb->io[0x0F] = 0;
    gb_set_buttons(gb, GB_BTN_START | GB_BTN_RIGHT | GB_BTN_A);
    CHECK(gb->io[0x0F] & GB_INT_JOYPAD);
    gb_free(gb);
}

static void st_mbc5(void)
{
    GB *gb = st_new(NULL, 0, 0x150);
    CHECK(gb_rom_bank(gb) == 1 && gb_read(gb, 0x4000) == 1);
    gb_write(gb, 0x2000, 3);
    CHECK(gb_rom_bank(gb) == 3 && gb_read(gb, 0x4000) == 3);
    gb_write(gb, 0x2000, 0);
    CHECK(gb_rom_bank(gb) == 0 && gb_read(gb, 0x4000) == gb_read(gb, 0x0000)); /* MBC5 allows bank 0 */
    gb_write(gb, 0x2000, 6);
    CHECK(gb_rom_bank(gb) == 2);   /* wraps modulo ROM size */
    gb_write(gb, 0x3000, 1);
    CHECK(gb_rom_bank(gb) == (0x106 % 4));
    gb_write(gb, 0x3000, 0);
    CHECK(gb_read(gb, 0xA000) == 0xFF); /* RAM disabled */
    gb_write(gb, 0x0000, 0x0A);
    gb_write(gb, 0x4000, 2);
    gb_write(gb, 0xA123, 0x5A);
    CHECK(gb_read(gb, 0xA123) == 0x5A && gb_sram(gb)[2 * 0x2000 + 0x123] == 0x5A);
    gb_write(gb, 0x4000, 0);
    CHECK(gb_read(gb, 0xA123) == 0x00);
    gb_write(gb, 0x0000, 0x00);
    CHECK(gb_read(gb, 0xA123) == 0xFF);
    /* echo RAM, HRAM, IE, DMA */
    gb_write(gb, 0xC010, 0x77);
    CHECK(gb_read(gb, 0xE010) == 0x77);
    gb_write(gb, 0xFF90, 0x12);
    CHECK(gb_hram(gb)[0x10] == 0x12);
    gb_write(gb, 0xFFFF, 0x1F);
    CHECK(gb->ie == 0x1F && gb_read(gb, 0xFFFF) == 0x1F);
    for (int i = 0; i < 0xA0; i++)
        gb_wram(gb)[0x100 + i] = (uint8_t)(i * 3);
    gb_write(gb, 0xFF46, 0xC1);
    CHECK(gb->oam[0] == 0 && gb->oam[0x9F] == (uint8_t)(0x9F * 3));
    /* NR52 */
    CHECK(gb_read(gb, 0xFF26) == 0xF0);
    gb_write(gb, 0xFF26, 0x00);
    CHECK(gb_read(gb, 0xFF26) == 0x70 && gb_read(gb, 0xFF12) == 0x00);
    gb_write(gb, 0xFF12, 0xF0); /* ignored while powered off */
    CHECK(gb_read(gb, 0xFF12) == 0x00);
    gb_free(gb);
}

static void st_illegal(void)
{
    static const uint8_t code[] = { 0x00, 0xD3 };
    GB *gb = st_new(code, sizeof code, 0x150);
    int r = gb_run(gb, 1000, 0);
    CHECK(r == GB_STOP_ERROR && gb->error == GB_ERR_ILLEGAL_OPCODE && gb->error_pc == 0x151 &&
          gb->pc == 0x151 && gb->error_opcode == 0xD3);
    CHECK(gb_run(gb, 1000, 0) == GB_STOP_ERROR);
    gb_free(gb);
}

static void st_postboot(void)
{
    GB *gb = st_new(NULL, 0, 0x100);
    CHECK(gb->a == 0x01 && gb->f == 0xB0 && gb->b == 0x00 && gb->c == 0x13 && gb->d == 0x00 &&
          gb->e == 0xD8 && gb->h == 0x01 && gb->l == 0x4D && gb->sp == 0xFFFE && gb->pc == 0x100);
    CHECK(gb_read(gb, 0xFF00) == 0xCF && gb_read(gb, 0xFF04) == 0xAB && gb_read(gb, 0xFF07) == 0xF8 &&
          gb_read(gb, 0xFF0F) == 0xE1 && gb_read(gb, 0xFF40) == 0x91 && gb_read(gb, 0xFF47) == 0xFC &&
          gb_read(gb, 0xFF02) == 0x7E && gb_read(gb, 0xFFFF) == 0x00 && gb_read(gb, 0xFF4D) == 0xFF);
    gb_free(gb);
}

static int cmd_selftest(void)
{
    st_postboot();
    st_ei_di();
    st_priority();
    st_halt();
    st_callstack();
    st_timer();
    st_serial();
    st_lcd();
    st_breakpoints();
    st_joypad();
    st_mbc5();
    st_illegal();
    printf("selftest: %s (%d failures)\n", st_fail ? "FAIL" : "PASS", st_fail);
    return st_fail ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "selftest"))
        return cmd_selftest();
    if (argc >= 3 && !strcmp(argv[1], "blargg"))
        return cmd_blargg(argv[2], argc > 3 ? atol(argv[3]) : 60 * 120);
    if (argc >= 3 && !strcmp(argv[1], "bench"))
        return cmd_bench(argv[2], argc > 3 ? atol(argv[3]) : 10000, argc > 4 ? atoi(argv[4]) : 0);
    if (argc >= 5 && !strcmp(argv[1], "shot"))
        return cmd_shot(argv[2], atol(argv[3]), argv[4]);
    if (argc >= 5 && !strcmp(argv[1], "poketcg"))
        return cmd_poketcg(argv[2], argv[3], argv[4]);
    fprintf(stderr,
            "usage:\n"
            "  %s selftest\n"
            "  %s blargg ROM [max_frames]\n"
            "  %s bench ROM [frames] [render]\n"
            "  %s shot ROM frames out.png\n"
            "  %s poketcg ROM SYM OUTDIR\n",
            argv[0], argv[0], argv[0], argv[0], argv[0]);
    return 2;
}
