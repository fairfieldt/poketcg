/*
 * gbtest.c - test driver for the gb core.
 *
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
    char buf[96];
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

int main(int argc, char **argv)
{
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
            "  %s blargg ROM [max_frames]\n"
            "  %s bench ROM [frames] [render]\n"
            "  %s shot ROM frames out.png\n"
            "  %s poketcg ROM SYM OUTDIR\n",
            argv[0], argv[0], argv[0], argv[0]);
    return 2;
}
