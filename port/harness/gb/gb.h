/*
 * gb.h - small headless Game Boy (DMG) emulator core, portable C11, libc only.
 *
 * Intended as the reference side of a differential test harness: correct SM83
 * semantics and M-cycle-granular timing (every memory access advances the
 * timer/PPU/serial by 4 clocks), simple scanline PPU.
 *
 * The GB struct is public so the harness can poke registers and memory
 * directly.  Everything from the start of the struct up to (not including)
 * the field `rom` is plain data with no pointers: that prefix is the machine
 * state and can be snapshotted with gb_save_state()/gb_load_state() (which
 * are just memcpy of GB_STATE_SIZE bytes).  Fields after it (ROM pointer,
 * breakpoints, frame buffers, callbacks) are host-side configuration and are
 * not part of the snapshot.
 */
#ifndef GB_H
#define GB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Button mask bits for gb_set_buttons (1 = pressed). */
enum {
    GB_BTN_A      = 0x01,
    GB_BTN_B      = 0x02,
    GB_BTN_SELECT = 0x04,
    GB_BTN_START  = 0x08,
    GB_BTN_RIGHT  = 0x10,
    GB_BTN_LEFT   = 0x20,
    GB_BTN_UP     = 0x40,
    GB_BTN_DOWN   = 0x80,
};

/* gb_run return values */
enum { GB_STOP_BREAKPOINT = 1, GB_STOP_FRAME, GB_STOP_CYCLES, GB_STOP_ERROR };

/* gb->error values */
enum { GB_ERR_NONE = 0, GB_ERR_ILLEGAL_OPCODE = 1 };

/* Interrupt bits (IF / IE) */
enum { GB_INT_VBLANK = 1, GB_INT_STAT = 2, GB_INT_TIMER = 4, GB_INT_SERIAL = 8, GB_INT_JOYPAD = 16 };

#define GB_CALLSTACK_MAX 256
#define GB_SCREEN_W 160
#define GB_SCREEN_H 144

/* One shadow call stack frame. */
typedef struct GBFrame {
    uint16_t target;       /* address called / RST vector / interrupt vector */
    uint16_t ret;          /* return address that was pushed */
    uint16_t sp;           /* SP right after the return address was pushed */
    uint16_t bank;         /* ROM bank mapped at $4000 when the call happened */
    uint8_t  is_interrupt; /* 1 = interrupt dispatch, 0 = CALL / RST */
    uint8_t  _pad;
} GBFrame;

typedef struct GBBreakpoint {
    int      bank;         /* -1 = any bank */
    uint16_t addr;
    uint8_t  active;
} GBBreakpoint;

typedef struct GB GB;
/* Called whenever a serial transfer is started with the internal clock
 * (SC written with bit7|bit0 set); `byte` is the value in SB. */
typedef void (*gb_serial_fn)(GB *gb, uint8_t byte, void *user);

struct GB {
    /* ===================== saved state (plain data) ===================== */
    /* CPU registers. Index order matches SM83 r8 encoding (B C D E H L - A)
     * with F stored in the (HL) slot. */
    union {
        struct { uint8_t b, c, d, e, h, l, f, a; };
        uint8_t r[8];
    };
    uint16_t sp, pc;
    uint8_t  ime;          /* interrupt master enable */
    uint8_t  ei_pending;   /* EI executed; IME becomes 1 after the next instruction */
    uint8_t  halted;
    uint8_t  halt_bug;     /* next opcode fetch does not increment PC */

    uint64_t cycles;       /* T-cycles (4 MHz clocks) since reset */
    uint64_t frames;       /* number of VBlank starts (LY 143->144) */

    int      error;        /* GB_ERR_* */
    uint16_t error_pc;     /* address of the offending instruction */
    uint8_t  error_opcode;

    /* breakpoint resume bookkeeping */
    int      hit_breakpoint;   /* id of the breakpoint that stopped gb_run, -1 otherwise */
    uint8_t  bp_skip_valid;
    uint16_t bp_skip_pc, bp_skip_sp;
    int      bp_skip_bank;

    /* shadow call stack */
    int      cs_depth;
    uint32_t cs_dropped;   /* frames discarded from the bottom because the stack was full */
    GBFrame  cs[GB_CALLSTACK_MAX];

    /* memory */
    uint8_t  vram[0x2000];
    uint8_t  wram[0x2000];
    uint8_t  oam[0xA0];
    uint8_t  hram[0x80];   /* $FF80-$FFFE (index 0x7F unused) */
    uint8_t  io[0x80];     /* $FF00-$FF7F raw register storage */
    uint8_t  ie;           /* $FFFF */
    uint8_t  sram[0x8000];

    /* cartridge / MBC */
    uint8_t  mbc;          /* 0 = none, 1 = MBC1, 5 = MBC5 */
    uint8_t  ram_enable;
    uint16_t rom_bank;     /* MBC5: 9-bit bank register; MBC1: low 5 bits */
    uint8_t  ram_bank;     /* MBC5: 4-bit; MBC1: 2-bit upper register */
    uint8_t  mbc1_mode;
    uint32_t rom0_off;     /* offsets into rom[] of the banks mapped at $0000 / $4000 */
    uint32_t romx_off;
    uint32_t sram_off;     /* offset of the bank mapped at $A000 */
    uint32_t sram_size;    /* usable SRAM bytes (<= 0x8000), 0 = none */

    /* timer */
    uint16_t div16;        /* internal 16-bit divider; DIV = div16 >> 8 */
    uint8_t  tima_overflow;/* TIMA overflowed last M-cycle; reload pending */

    /* PPU */
    int      ppu_line;     /* current line 0..153 */
    int      ppu_phase;    /* internal: which event comes next */
    uint64_t ppu_next;     /* absolute cycle of next PPU event */
    uint8_t  stat_line;    /* STAT interrupt line level (for rising-edge detection) */
    uint8_t  wy_hit;       /* WY == LY happened this frame */
    int      window_line;  /* internal window line counter */

    /* serial */
    uint64_t serial_next;  /* absolute cycle when the active transfer completes */

    uint64_t next_event;   /* min(ppu_next, serial_next) */

    /* joypad */
    uint8_t  buttons;      /* GB_BTN_* mask, 1 = pressed */

    /* ===================== host side (not saved) ===================== */
    const uint8_t *rom;    /* owned copy of the ROM (padded to >= 32KB) */
    size_t   rom_size;
    uint32_t rom_banks;

    int      render;       /* 1 = render scanlines into screen[] (default 1) */
    uint8_t  screen[GB_SCREEN_H][GB_SCREEN_W]; /* shades 0 (white) .. 3 (black), being drawn */
    uint8_t  frame[GB_SCREEN_H][GB_SCREEN_W];  /* last completed frame (copied at VBlank) */

    gb_serial_fn serial_cb;
    void    *serial_user;

    GBBreakpoint *bps;
    int      bp_cap, bp_count;      /* bp_count = slots used (ids are slot indices) */
    int      bp_active;             /* number of active breakpoints */
    uint8_t  bp_map[0x10000 / 8];   /* bit set = at least one active breakpoint at addr */
};

#define GB_STATE_SIZE offsetof(GB, rom)

/* ---- lifetime ---- */
GB  *gb_new(const uint8_t *rom, size_t rom_size);  /* copies the ROM; NULL on failure */
void gb_free(GB *gb);
void gb_reset(GB *gb);   /* post-boot DMG state, PC=$0100. Keeps SRAM and breakpoints. */

/* ---- bus (no clock advance) ---- */
uint8_t  gb_read(GB *gb, uint16_t addr);
void     gb_write(GB *gb, uint16_t addr, uint8_t v);  /* with hardware side effects (MBC, DIV, ...) */
uint16_t gb_read16(GB *gb, uint16_t addr);

uint8_t *gb_wram(GB *gb);   /* 8KB at $C000 */
uint8_t *gb_hram(GB *gb);   /* $FF80 */
uint8_t *gb_sram(GB *gb);   /* 32KB, bank n at offset n*0x2000 */
uint8_t *gb_vram(GB *gb);   /* 8KB at $8000 */
int      gb_rom_bank(GB *gb);   /* ROM bank currently mapped at $4000 */
int      gb_bank_of(GB *gb, uint16_t addr); /* bank for addr: 0 for <$4000, current for $4000-$7FFF, -1 otherwise */

/* register pair helpers */
static inline uint16_t gb_af(const GB *gb) { return (uint16_t)(gb->a << 8 | gb->f); }
static inline uint16_t gb_bc(const GB *gb) { return (uint16_t)(gb->b << 8 | gb->c); }
static inline uint16_t gb_de(const GB *gb) { return (uint16_t)(gb->d << 8 | gb->e); }
static inline uint16_t gb_hl(const GB *gb) { return (uint16_t)(gb->h << 8 | gb->l); }
static inline void gb_set_af(GB *gb, uint16_t v) { gb->a = (uint8_t)(v >> 8); gb->f = (uint8_t)(v & 0xF0); }
static inline void gb_set_bc(GB *gb, uint16_t v) { gb->b = (uint8_t)(v >> 8); gb->c = (uint8_t)v; }
static inline void gb_set_de(GB *gb, uint16_t v) { gb->d = (uint8_t)(v >> 8); gb->e = (uint8_t)v; }
static inline void gb_set_hl(GB *gb, uint16_t v) { gb->h = (uint8_t)(v >> 8); gb->l = (uint8_t)v; }

/* ---- input ---- */
void gb_set_buttons(GB *gb, uint8_t mask);

/* ---- execution ---- */
/* Run until a breakpoint is hit (before executing the instruction at its
 * address), a new frame starts (if stop_on_frame), max_cycles T-cycles have
 * elapsed, or an error occurs. Returns GB_STOP_*. */
int  gb_run(GB *gb, uint64_t max_cycles, int stop_on_frame);
/* Execute exactly one instruction (or one interrupt dispatch, or one halted
 * M-cycle). Ignores breakpoints. Returns 0, or GB_STOP_ERROR. */
int  gb_step(GB *gb);

/* ---- breakpoints ---- */
int  gb_add_breakpoint(GB *gb, int bank, uint16_t addr);  /* returns id >= 0, -1 on OOM */
void gb_remove_breakpoint(GB *gb, int id);
void gb_clear_breakpoints(GB *gb);

/* ---- shadow call stack ---- */
int            gb_callstack_depth(GB *gb);
const GBFrame *gb_callstack(GB *gb);          /* frames[0] = outermost */
const GBFrame *gb_callstack_top(GB *gb);      /* innermost frame or NULL */

/* ---- snapshots ---- */
size_t gb_state_size(void);                    /* == GB_STATE_SIZE */
void   gb_save_state(const GB *gb, void *buf);
void   gb_load_state(GB *gb, const void *buf);

/* ---- debugging output ---- */
int  gb_write_ppm(GB *gb, const char *path);   /* last completed frame; 0 on success */
int  gb_write_png(GB *gb, const char *path);   /* 8-bit grayscale, stored deflate; 0 on success */
const char *gb_error_string(GB *gb);

#ifdef __cplusplus
}
#endif
#endif /* GB_H */
