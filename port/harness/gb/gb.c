/*
 * gb.c - headless Game Boy (DMG) emulator core. See gb.h.
 *
 * Timing model: every CPU memory access (and every internal delay cycle) is
 * one M-cycle = 4 T-cycles. tick() advances the divider/timer per M-cycle and
 * runs the PPU / serial state machines when their next event time is reached.
 * Accesses happen at the end of their M-cycle (tick first, then access).
 */
#include "gb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------- */
/* constants                                                              */

enum {
    R_P1 = 0x00, R_SB = 0x01, R_SC = 0x02, R_DIV = 0x04, R_TIMA = 0x05,
    R_TMA = 0x06, R_TAC = 0x07, R_IF = 0x0F,
    R_NR10 = 0x10, R_NR51 = 0x25, R_NR52 = 0x26,
    R_LCDC = 0x40, R_STAT = 0x41, R_SCY = 0x42, R_SCX = 0x43, R_LY = 0x44,
    R_LYC = 0x45, R_DMA = 0x46, R_BGP = 0x47, R_OBP0 = 0x48, R_OBP1 = 0x49,
    R_WY = 0x4A, R_WX = 0x4B,
};

enum { FZ = 0x80, FN = 0x40, FH = 0x20, FC = 0x10 };

enum { MBC_NONE = 0, MBC_1 = 1, MBC_5 = 5 };

enum { PH_LINE = 0, PH_MODE3 = 1, PH_MODE0 = 2 };

#define NEVER UINT64_MAX

/* line timing (dots == T-cycles on DMG) */
#define DOTS_MODE2 80
#define DOTS_MODE3 172
#define DOTS_MODE0 (456 - DOTS_MODE2 - DOTS_MODE3)
#define DOTS_LINE 456

#define SERIAL_CYCLES (8 * 512)

/* TAC clock select -> divider bit whose falling edge clocks TIMA */
static const uint16_t tac_bit[4] = { 1u << 9, 1u << 3, 1u << 5, 1u << 7 };
/* log2 of the TIMA period for each clock select */
static const uint8_t tac_shift[4] = { 10, 4, 6, 8 };

/* OR masks for APU register reads, $FF10-$FF3F */
static const uint8_t apu_read_or[0x30] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF,             /* NR10-NR14 */
    0xFF, 0x3F, 0x00, 0xFF, 0xBF,             /* ----, NR21-NR24 */
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF,             /* NR30-NR34 */
    0xFF, 0xFF, 0x00, 0x00, 0xBF,             /* ----, NR41-NR44 */
    0x00, 0x00, 0x70,                         /* NR50-NR52 */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* $FF27-$FF2F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* wave RAM */
};

/* ---------------------------------------------------------------------- */
/* forward decls                                                          */

static void do_events(GB *gb);
static void render_line(GB *gb, int ly);

/* ---------------------------------------------------------------------- */
/* scheduling                                                             */

static inline void update_next_event(GB *gb)
{
    gb->next_event = gb->ppu_next < gb->serial_next ? gb->ppu_next : gb->serial_next;
}

/* ---------------------------------------------------------------------- */
/* timer                                                                  */

static inline int timer_signal(const GB *gb)
{
    uint8_t tac = gb->io[R_TAC];
    return (tac & 4) && (gb->div16 & tac_bit[tac & 3]);
}

static inline void tima_inc(GB *gb)
{
    if (++gb->io[R_TIMA] == 0)
        gb->tima_overflow = 1;   /* TIMA reads 0 for one M-cycle, then reloads */
}

/* Advance the machine by one M-cycle. */
static inline void tick(GB *gb)
{
    gb->cycles += 4;
    if (gb->tima_overflow) {
        if (gb->tima_overflow == 1) {
            gb->io[R_TIMA] = gb->io[R_TMA];
            gb->io[R_IF] |= GB_INT_TIMER;
            gb->tima_overflow = 2;       /* reload cycle: TIMA writes ignored */
        } else {
            gb->tima_overflow = 0;
        }
    }
    uint16_t old = gb->div16;
    gb->div16 = (uint16_t)(old + 4);
    uint8_t tac = gb->io[R_TAC];
    if (tac & 4) {
        uint16_t m = tac_bit[tac & 3];
        if ((old & m) && !(gb->div16 & m))
            tima_inc(gb);
    }
    if (gb->cycles >= gb->next_event)
        do_events(gb);
}

/* ---------------------------------------------------------------------- */
/* PPU                                                                    */

static inline void set_mode(GB *gb, int mode)
{
    gb->io[R_STAT] = (uint8_t)((gb->io[R_STAT] & ~3) | mode);
}

static inline void check_lyc(GB *gb)
{
    if (gb->io[R_LY] == gb->io[R_LYC])
        gb->io[R_STAT] |= 4;
    else
        gb->io[R_STAT] &= (uint8_t)~4;
}

/* Re-evaluate the STAT interrupt line; request the interrupt on a rising
 * edge. `oam_extra` models the DMG quirk where the mode-2 source also fires
 * when entering VBlank at line 144. */
static void stat_update(GB *gb, int oam_extra)
{
    uint8_t s = gb->io[R_STAT];
    int line = 0;
    if (gb->io[R_LCDC] & 0x80) {
        int mode = s & 3;
        line = ((s & 0x40) && (s & 0x04)) ||
               ((s & 0x08) && mode == 0) ||
               ((s & 0x10) && mode == 1) ||
               ((s & 0x20) && (mode == 2 || oam_extra));
    }
    if (line && !gb->stat_line)
        gb->io[R_IF] |= GB_INT_STAT;
    gb->stat_line = (uint8_t)line;
}

static void ppu_event(GB *gb)
{
    switch (gb->ppu_phase) {
    case PH_MODE3:
        set_mode(gb, 3);
        stat_update(gb, 0);
        gb->ppu_phase = PH_MODE0;
        gb->ppu_next += DOTS_MODE3;
        break;
    case PH_MODE0:
        if (gb->render)
            render_line(gb, gb->ppu_line);
        set_mode(gb, 0);
        stat_update(gb, 0);
        gb->ppu_phase = PH_LINE;
        gb->ppu_next += DOTS_MODE0;
        break;
    default: { /* PH_LINE: start of a new line */
        int line = gb->ppu_line + 1;
        if (line == 154)
            line = 0;
        gb->ppu_line = line;
        gb->io[R_LY] = (uint8_t)line;
        check_lyc(gb);
        if (line < 144) {
            if (line == 0) {
                gb->window_line = 0;
                gb->wy_hit = 0;
            }
            if (line == gb->io[R_WY])
                gb->wy_hit = 1;
            set_mode(gb, 2);
            stat_update(gb, 0);
            gb->ppu_phase = PH_MODE3;
            gb->ppu_next += DOTS_MODE2;
        } else {
            if (line == 144) {
                set_mode(gb, 1);
                gb->io[R_IF] |= GB_INT_VBLANK;
                gb->frames++;
                if (gb->render)
                    memcpy(gb->frame, gb->screen, sizeof gb->frame);
                stat_update(gb, 1);
            } else {
                stat_update(gb, 0);
            }
            gb->ppu_phase = PH_LINE;
            gb->ppu_next += DOTS_LINE;
        }
        break;
    }
    }
}

static void lcd_on(GB *gb)
{
    /* The first line after enabling starts without an OAM scan (mode 0). */
    gb->ppu_line = 0;
    gb->io[R_LY] = 0;
    gb->window_line = 0;
    gb->wy_hit = gb->io[R_WY] == 0;
    check_lyc(gb);
    set_mode(gb, 0);
    gb->ppu_phase = PH_MODE3;
    gb->ppu_next = gb->cycles + DOTS_MODE2;
    stat_update(gb, 0);
    update_next_event(gb);
}

static void lcd_off(GB *gb)
{
    gb->ppu_line = 0;
    gb->io[R_LY] = 0;
    set_mode(gb, 0);
    gb->ppu_next = NEVER;
    gb->stat_line = 0;
    update_next_event(gb);
}

static void do_events(GB *gb)
{
    while (gb->cycles >= gb->ppu_next)
        ppu_event(gb);
    if (gb->cycles >= gb->serial_next) {
        /* no link partner: shifted in all 1s */
        gb->serial_next = NEVER;
        gb->io[R_SB] = 0xFF;
        gb->io[R_SC] &= 0x7F;
        gb->io[R_IF] |= GB_INT_SERIAL;
    }
    update_next_event(gb);
}

static inline const uint8_t *tile_row(const GB *gb, uint8_t lcdc, uint8_t tile, int row)
{
    if (lcdc & 0x10)
        return gb->vram + tile * 16 + row * 2;
    return gb->vram + 0x1000 + (int8_t)tile * 16 + row * 2;
}

static void render_line(GB *gb, int ly)
{
    if (ly < 0 || ly >= GB_SCREEN_H)
        return;
    uint8_t *out = gb->screen[ly];
    uint8_t bgc[GB_SCREEN_W];
    uint8_t lcdc = gb->io[R_LCDC];

    if (lcdc & 0x01) {
        const uint8_t *map = gb->vram + ((lcdc & 0x08) ? 0x1C00 : 0x1800);
        int y = (ly + gb->io[R_SCY]) & 0xFF;
        int scx = gb->io[R_SCX];
        for (int x = 0; x < GB_SCREEN_W; x++) {
            int px = (x + scx) & 0xFF;
            const uint8_t *td = tile_row(gb, lcdc, map[(y >> 3) * 32 + (px >> 3)], y & 7);
            int bit = 7 - (px & 7);
            bgc[x] = (uint8_t)(((td[0] >> bit) & 1) | (((td[1] >> bit) & 1) << 1));
        }
        if ((lcdc & 0x20) && gb->wy_hit && gb->io[R_WX] <= 166) {
            const uint8_t *wmap = gb->vram + ((lcdc & 0x40) ? 0x1C00 : 0x1800);
            int wx = gb->io[R_WX] - 7;
            int wy = gb->window_line & 0xFF;
            for (int x = wx < 0 ? 0 : wx; x < GB_SCREEN_W; x++) {
                int px = x - wx;
                const uint8_t *td = tile_row(gb, lcdc, wmap[(wy >> 3) * 32 + (px >> 3)], wy & 7);
                int bit = 7 - (px & 7);
                bgc[x] = (uint8_t)(((td[0] >> bit) & 1) | (((td[1] >> bit) & 1) << 1));
            }
            gb->window_line++;
        }
    } else {
        memset(bgc, 0, sizeof bgc);
    }

    uint8_t bgp = gb->io[R_BGP];
    for (int x = 0; x < GB_SCREEN_W; x++)
        out[x] = (bgp >> (bgc[x] * 2)) & 3;

    if (lcdc & 0x02) {
        int h = (lcdc & 0x04) ? 16 : 8;
        int sel[10], n = 0;
        for (int i = 0; i < 40 && n < 10; i++) {
            int sy = gb->oam[i * 4] - 16;
            if (ly >= sy && ly < sy + h)
                sel[n++] = i;
        }
        /* DMG priority: lower X first, then lower OAM index (stable sort) */
        for (int i = 1; i < n; i++) {
            int v = sel[i], j = i - 1;
            while (j >= 0 && gb->oam[sel[j] * 4 + 1] > gb->oam[v * 4 + 1]) {
                sel[j + 1] = sel[j];
                j--;
            }
            sel[j + 1] = v;
        }
        uint8_t taken[GB_SCREEN_W];
        memset(taken, 0, sizeof taken);
        for (int k = 0; k < n; k++) {
            const uint8_t *s = &gb->oam[sel[k] * 4];
            int sy = s[0] - 16, sx = s[1] - 8;
            uint8_t tile = s[2], attr = s[3];
            int row = ly - sy;
            if (attr & 0x40)
                row = h - 1 - row;
            if (h == 16)
                tile &= 0xFE;
            const uint8_t *td = gb->vram + tile * 16 + row * 2;
            uint8_t pal = (attr & 0x10) ? gb->io[R_OBP1] : gb->io[R_OBP0];
            for (int px = 0; px < 8; px++) {
                int x = sx + px;
                if (x < 0 || x >= GB_SCREEN_W || taken[x])
                    continue;
                int bit = (attr & 0x20) ? px : 7 - px;
                int c = ((td[0] >> bit) & 1) | (((td[1] >> bit) & 1) << 1);
                if (!c)
                    continue;
                taken[x] = 1;
                if ((attr & 0x80) && bgc[x])
                    continue;
                out[x] = (pal >> (c * 2)) & 3;
            }
        }
    }
}

/* ---------------------------------------------------------------------- */
/* joypad                                                                 */

static inline uint8_t joy_lines(const GB *gb)
{
    uint8_t sel = gb->io[R_P1];
    uint8_t v = 0x0F;
    if (!(sel & 0x10))
        v &= (uint8_t)~(gb->buttons >> 4);
    if (!(sel & 0x20))
        v &= (uint8_t)~gb->buttons;
    return v & 0x0F;
}

void gb_set_buttons(GB *gb, uint8_t mask)
{
    uint8_t before = joy_lines(gb);
    gb->buttons = mask;
    if (before & ~joy_lines(gb))
        gb->io[R_IF] |= GB_INT_JOYPAD;
}

/* ---------------------------------------------------------------------- */
/* cartridge                                                              */

static void mbc_update(GB *gb)
{
    uint32_t bank = 1, bank0 = 0, rbank = 0;
    switch (gb->mbc) {
    case MBC_NONE:
        break;
    case MBC_1: {
        uint32_t lo = gb->rom_bank & 0x1F;
        if (!lo)
            lo = 1;
        bank = (uint32_t)(gb->ram_bank & 3) << 5 | lo;
        if (gb->mbc1_mode) {
            bank0 = (uint32_t)(gb->ram_bank & 3) << 5;
            rbank = gb->ram_bank & 3;
        }
        break;
    }
    default: /* MBC5 */
        bank = gb->rom_bank & 0x1FF;
        rbank = gb->ram_bank & 0x0F;
        break;
    }
    bank %= gb->rom_banks;
    bank0 %= gb->rom_banks;
    gb->romx_off = bank * 0x4000;
    gb->rom0_off = bank0 * 0x4000;
    gb->sram_off = gb->sram_size ? (rbank * 0x2000) & (gb->sram_size - 1) : 0;
}

static void mbc_write(GB *gb, uint16_t a, uint8_t v)
{
    if (gb->mbc == MBC_NONE)
        return;
    switch (a >> 13) {
    case 0: /* $0000-$1FFF */
        gb->ram_enable = (v & 0x0F) == 0x0A;
        return;
    case 1: /* $2000-$3FFF */
        if (gb->mbc == MBC_1)
            gb->rom_bank = v & 0x1F;
        else if (a < 0x3000)
            gb->rom_bank = (uint16_t)((gb->rom_bank & 0x100) | v);
        else
            gb->rom_bank = (uint16_t)((gb->rom_bank & 0xFF) | ((v & 1) << 8));
        break;
    case 2: /* $4000-$5FFF */
        gb->ram_bank = gb->mbc == MBC_1 ? (v & 3) : (v & 0x0F);
        break;
    default: /* $6000-$7FFF */
        if (gb->mbc == MBC_1)
            gb->mbc1_mode = v & 1;
        break;
    }
    mbc_update(gb);
}

/* ---------------------------------------------------------------------- */
/* IO registers                                                           */

static uint8_t io_read(GB *gb, uint8_t r)
{
    switch (r) {
    case R_P1:   return (uint8_t)(0xC0 | gb->io[R_P1] | joy_lines(gb));
    case R_SB:   return gb->io[R_SB];
    case R_SC:   return gb->io[R_SC] | 0x7E;
    case R_DIV:  return (uint8_t)(gb->div16 >> 8);
    case R_TIMA:
    case R_TMA:  return gb->io[r];
    case R_TAC:  return gb->io[R_TAC] | 0xF8;
    case R_IF:   return gb->io[R_IF] | 0xE0;
    case R_NR52: return (gb->io[R_NR52] & 0x80) | 0x70; /* no channel ever reports active */
    case R_LCDC: case R_SCY: case R_SCX: case R_LY: case R_LYC: case R_DMA:
    case R_BGP: case R_OBP0: case R_OBP1: case R_WY: case R_WX:
        return gb->io[r];
    case R_STAT: return gb->io[R_STAT] | 0x80;
    default:
        if (r >= 0x10 && r < 0x40)
            return gb->io[r] | apu_read_or[r - 0x10];
        return 0xFF;
    }
}

static void bus_write(GB *gb, uint16_t a, uint8_t v);

static void io_write(GB *gb, uint8_t r, uint8_t v)
{
    switch (r) {
    case R_P1: {
        uint8_t before = joy_lines(gb);
        gb->io[R_P1] = v & 0x30;
        if (before & ~joy_lines(gb))
            gb->io[R_IF] |= GB_INT_JOYPAD;
        break;
    }
    case R_SB:
        gb->io[R_SB] = v;
        break;
    case R_SC:
        gb->io[R_SC] = v & 0x81;
        if ((v & 0x81) == 0x81) {
            if (gb->serial_cb)
                gb->serial_cb(gb, gb->io[R_SB], gb->serial_user);
            gb->serial_next = gb->cycles + SERIAL_CYCLES;
        } else {
            gb->serial_next = NEVER; /* external clock: no partner, never completes */
        }
        update_next_event(gb);
        break;
    case R_DIV:
        if (timer_signal(gb))
            tima_inc(gb);
        gb->div16 = 0;
        break;
    case R_TIMA:
        if (gb->tima_overflow == 2)
            break;              /* write ignored during the reload cycle */
        gb->tima_overflow = 0;  /* write during the overflow cycle cancels the reload */
        gb->io[R_TIMA] = v;
        break;
    case R_TMA:
        gb->io[R_TMA] = v;
        if (gb->tima_overflow == 2)
            gb->io[R_TIMA] = v;
        break;
    case R_TAC: {
        int before = timer_signal(gb);
        gb->io[R_TAC] = v & 7;
        if (before && !timer_signal(gb))
            tima_inc(gb);
        break;
    }
    case R_IF:
        gb->io[R_IF] = v & 0x1F;
        break;
    case R_NR52:
        if (!(v & 0x80) && (gb->io[R_NR52] & 0x80))
            memset(&gb->io[R_NR10], 0, R_NR51 - R_NR10 + 1);
        gb->io[R_NR52] = v & 0x80;
        break;
    case R_LCDC: {
        uint8_t old = gb->io[R_LCDC];
        gb->io[R_LCDC] = v;
        if ((old ^ v) & 0x80) {
            if (v & 0x80)
                lcd_on(gb);
            else
                lcd_off(gb);
        }
        break;
    }
    case R_STAT:
        gb->io[R_STAT] = (uint8_t)((gb->io[R_STAT] & 0x07) | (v & 0x78));
        stat_update(gb, 0);
        break;
    case R_LY:
        break;
    case R_LYC:
        gb->io[R_LYC] = v;
        if (gb->io[R_LCDC] & 0x80) {
            check_lyc(gb);
            stat_update(gb, 0);
        }
        break;
    case R_DMA: {
        gb->io[R_DMA] = v;
        uint16_t src = (uint16_t)((v >= 0xE0 ? v - 0x20 : v) << 8);
        for (int i = 0; i < 0xA0; i++) {
            uint16_t sa = (uint16_t)(src + i);
            uint8_t b;
            if (sa >= 0x8000 && sa < 0xA000)
                b = gb->vram[sa & 0x1FFF];
            else if (sa >= 0xC000)
                b = gb->wram[sa & 0x1FFF];
            else
                b = gb_read(gb, sa);
            gb->oam[i] = b;
        }
        break;
    }
    case R_SCY: case R_SCX: case R_BGP: case R_OBP0: case R_OBP1: case R_WY: case R_WX:
        gb->io[r] = v;
        break;
    default:
        if (r >= 0x10 && r < 0x30) {
            if (gb->io[R_NR52] & 0x80)
                gb->io[r] = v;
        } else if (r >= 0x30 && r < 0x40) {
            gb->io[r] = v; /* wave RAM */
        }
        /* other registers (CGB-only, unused) are ignored on DMG */
        break;
    }
}

/* ---------------------------------------------------------------------- */
/* bus                                                                    */

static inline uint8_t bus_read(GB *gb, uint16_t a)
{
    switch (a >> 12) {
    case 0x0: case 0x1: case 0x2: case 0x3:
        return gb->rom[gb->rom0_off + a];
    case 0x4: case 0x5: case 0x6: case 0x7:
        return gb->rom[gb->romx_off + (a & 0x3FFF)];
    case 0x8: case 0x9:
        return gb->vram[a & 0x1FFF];
    case 0xA: case 0xB:
        if (!gb->ram_enable || !gb->sram_size)
            return 0xFF;
        return gb->sram[(gb->sram_off + (a & 0x1FFF)) & (gb->sram_size - 1)];
    case 0xC: case 0xD: case 0xE:
        return gb->wram[a & 0x1FFF];
    default:
        if (a < 0xFE00)
            return gb->wram[a & 0x1FFF];
        if (a < 0xFEA0)
            return gb->oam[a - 0xFE00];
        if (a < 0xFF00)
            return 0x00;
        if (a < 0xFF80)
            return io_read(gb, (uint8_t)a);
        if (a < 0xFFFF)
            return gb->hram[a - 0xFF80];
        return gb->ie;
    }
}

static void bus_write(GB *gb, uint16_t a, uint8_t v)
{
    switch (a >> 12) {
    case 0x0: case 0x1: case 0x2: case 0x3:
    case 0x4: case 0x5: case 0x6: case 0x7:
        mbc_write(gb, a, v);
        return;
    case 0x8: case 0x9:
        gb->vram[a & 0x1FFF] = v;
        return;
    case 0xA: case 0xB:
        if (gb->ram_enable && gb->sram_size)
            gb->sram[(gb->sram_off + (a & 0x1FFF)) & (gb->sram_size - 1)] = v;
        return;
    case 0xC: case 0xD: case 0xE:
        gb->wram[a & 0x1FFF] = v;
        return;
    default:
        if (a < 0xFE00)
            gb->wram[a & 0x1FFF] = v;
        else if (a < 0xFEA0)
            gb->oam[a - 0xFE00] = v;
        else if (a < 0xFF00)
            ;
        else if (a < 0xFF80)
            io_write(gb, (uint8_t)a, v);
        else if (a < 0xFFFF)
            gb->hram[a - 0xFF80] = v;
        else
            gb->ie = v;
        return;
    }
}

uint8_t gb_read(GB *gb, uint16_t addr) { return bus_read(gb, addr); }
void gb_write(GB *gb, uint16_t addr, uint8_t v) { bus_write(gb, addr, v); }
uint16_t gb_read16(GB *gb, uint16_t addr)
{
    return (uint16_t)(bus_read(gb, addr) | bus_read(gb, (uint16_t)(addr + 1)) << 8);
}

uint8_t *gb_wram(GB *gb) { return gb->wram; }
uint8_t *gb_hram(GB *gb) { return gb->hram; }
uint8_t *gb_sram(GB *gb) { return gb->sram; }
uint8_t *gb_vram(GB *gb) { return gb->vram; }
int gb_rom_bank(GB *gb) { return (int)(gb->romx_off / 0x4000); }

int gb_bank_of(GB *gb, uint16_t addr)
{
    if (addr < 0x4000)
        return (int)(gb->rom0_off / 0x4000);
    if (addr < 0x8000)
        return gb_rom_bank(gb);
    return -1;
}

/* ---------------------------------------------------------------------- */
/* CPU helpers                                                            */

static inline uint8_t rd(GB *gb, uint16_t a)
{
    tick(gb);
    return bus_read(gb, a);
}

static inline void wr(GB *gb, uint16_t a, uint8_t v)
{
    tick(gb);
    bus_write(gb, a, v);
}

static inline uint8_t imm8(GB *gb)
{
    uint8_t v = rd(gb, gb->pc);
    gb->pc++;
    return v;
}

static inline uint16_t imm16(GB *gb)
{
    uint8_t lo = imm8(gb);
    uint8_t hi = imm8(gb);
    return (uint16_t)(lo | hi << 8);
}

static inline void push16(GB *gb, uint16_t v)
{
    gb->sp--;
    wr(gb, gb->sp, (uint8_t)(v >> 8));
    gb->sp--;
    wr(gb, gb->sp, (uint8_t)v);
}

static inline uint16_t pop16(GB *gb)
{
    uint8_t lo = rd(gb, gb->sp);
    gb->sp++;
    uint8_t hi = rd(gb, gb->sp);
    gb->sp++;
    return (uint16_t)(lo | hi << 8);
}

#define BC ((uint16_t)(gb->b << 8 | gb->c))
#define DE ((uint16_t)(gb->d << 8 | gb->e))
#define HL ((uint16_t)(gb->h << 8 | gb->l))
#define SET_HL(v) do { uint16_t v_ = (uint16_t)(v); gb->h = (uint8_t)(v_ >> 8); gb->l = (uint8_t)v_; } while (0)

static inline uint16_t get_rr(GB *gb, int i)
{
    switch (i) {
    case 0: return BC;
    case 1: return DE;
    case 2: return HL;
    default: return gb->sp;
    }
}

static inline void set_rr(GB *gb, int i, uint16_t v)
{
    switch (i) {
    case 0: gb->b = (uint8_t)(v >> 8); gb->c = (uint8_t)v; break;
    case 1: gb->d = (uint8_t)(v >> 8); gb->e = (uint8_t)v; break;
    case 2: gb->h = (uint8_t)(v >> 8); gb->l = (uint8_t)v; break;
    default: gb->sp = v; break;
    }
}

static inline int cond(const GB *gb, int cc)
{
    switch (cc) {
    case 0: return !(gb->f & FZ);
    case 1: return (gb->f & FZ) != 0;
    case 2: return !(gb->f & FC);
    default: return (gb->f & FC) != 0;
    }
}

static inline void alu(GB *gb, int op, uint8_t v)
{
    uint8_t a = gb->a;
    unsigned r, c;
    switch (op) {
    case 0: /* ADD */
        r = (unsigned)a + v;
        gb->f = (uint8_t)(((r & 0xFF) ? 0 : FZ) | (((a & 0xF) + (v & 0xF)) > 0xF ? FH : 0) | (r > 0xFF ? FC : 0));
        gb->a = (uint8_t)r;
        break;
    case 1: /* ADC */
        c = (gb->f >> 4) & 1;
        r = (unsigned)a + v + c;
        gb->f = (uint8_t)(((r & 0xFF) ? 0 : FZ) | (((a & 0xF) + (v & 0xF) + c) > 0xF ? FH : 0) | (r > 0xFF ? FC : 0));
        gb->a = (uint8_t)r;
        break;
    case 2: /* SUB */
        r = (unsigned)a - v;
        gb->f = (uint8_t)(FN | ((r & 0xFF) ? 0 : FZ) | ((a & 0xF) < (v & 0xF) ? FH : 0) | (a < v ? FC : 0));
        gb->a = (uint8_t)r;
        break;
    case 3: /* SBC */
        c = (gb->f >> 4) & 1;
        r = (unsigned)a - v - c;
        gb->f = (uint8_t)(FN | ((r & 0xFF) ? 0 : FZ) | ((a & 0xF) < (v & 0xF) + c ? FH : 0) | ((unsigned)a < v + c ? FC : 0));
        gb->a = (uint8_t)r;
        break;
    case 4: /* AND */
        gb->a = a & v;
        gb->f = (uint8_t)((gb->a ? 0 : FZ) | FH);
        break;
    case 5: /* XOR */
        gb->a = a ^ v;
        gb->f = gb->a ? 0 : FZ;
        break;
    case 6: /* OR */
        gb->a = a | v;
        gb->f = gb->a ? 0 : FZ;
        break;
    default: /* CP */
        r = (unsigned)a - v;
        gb->f = (uint8_t)(FN | ((r & 0xFF) ? 0 : FZ) | ((a & 0xF) < (v & 0xF) ? FH : 0) | (a < v ? FC : 0));
        break;
    }
}

static inline uint8_t inc8(GB *gb, uint8_t v)
{
    uint8_t r = (uint8_t)(v + 1);
    gb->f = (uint8_t)((gb->f & FC) | (r ? 0 : FZ) | ((v & 0xF) == 0xF ? FH : 0));
    return r;
}

static inline uint8_t dec8(GB *gb, uint8_t v)
{
    uint8_t r = (uint8_t)(v - 1);
    gb->f = (uint8_t)((gb->f & FC) | FN | (r ? 0 : FZ) | ((v & 0xF) == 0 ? FH : 0));
    return r;
}

/* CB-prefix rotate/shift group; sets Z per result, N=H=0 */
static inline uint8_t rot(GB *gb, int op, uint8_t v)
{
    uint8_t r, c;
    switch (op) {
    case 0: c = v >> 7; r = (uint8_t)(v << 1 | c); break;                     /* RLC */
    case 1: c = v & 1; r = (uint8_t)(v >> 1 | c << 7); break;                  /* RRC */
    case 2: c = v >> 7; r = (uint8_t)(v << 1 | ((gb->f >> 4) & 1)); break;     /* RL */
    case 3: c = v & 1; r = (uint8_t)(v >> 1 | ((gb->f & FC) << 3)); break;     /* RR */
    case 4: c = v >> 7; r = (uint8_t)(v << 1); break;                          /* SLA */
    case 5: c = v & 1; r = (uint8_t)(v >> 1 | (v & 0x80)); break;              /* SRA */
    case 6: c = 0; r = (uint8_t)(v >> 4 | v << 4); break;                      /* SWAP */
    default: c = v & 1; r = v >> 1; break;                                     /* SRL */
    }
    gb->f = (uint8_t)((r ? 0 : FZ) | (c ? FC : 0));
    return r;
}

/* ---------------------------------------------------------------------- */
/* shadow call stack                                                      */

static void cs_push(GB *gb, uint16_t target, uint16_t ret, int is_int)
{
    if (gb->cs_depth >= GB_CALLSTACK_MAX) {
        memmove(gb->cs, gb->cs + 1, sizeof(GBFrame) * (GB_CALLSTACK_MAX - 1));
        gb->cs_depth = GB_CALLSTACK_MAX - 1;
        gb->cs_dropped++;
    }
    GBFrame *fr = &gb->cs[gb->cs_depth++];
    fr->target = target;
    fr->ret = ret;
    fr->sp = gb->sp;
    fr->bank = (uint16_t)gb_rom_bank(gb);
    fr->is_interrupt = (uint8_t)is_int;
    fr->_pad = 0;
}

static inline void cs_pop_check(GB *gb)
{
    while (gb->cs_depth > 0 && gb->sp > gb->cs[gb->cs_depth - 1].sp)
        gb->cs_depth--;
}

int gb_callstack_depth(GB *gb) { return gb->cs_depth; }
void gb_callstack_sync(GB *gb) { cs_pop_check(gb); }

void gb_ret(GB *gb)
{
    gb->pc = (uint16_t)(bus_read(gb, gb->sp) | bus_read(gb, (uint16_t)(gb->sp + 1)) << 8);
    gb->sp = (uint16_t)(gb->sp + 2);
    cs_pop_check(gb);
}
const GBFrame *gb_callstack(GB *gb) { return gb->cs; }
const GBFrame *gb_callstack_top(GB *gb) { return gb->cs_depth ? &gb->cs[gb->cs_depth - 1] : NULL; }

/* ---------------------------------------------------------------------- */
/* interrupts / halt                                                      */

static inline uint8_t pending_ints(const GB *gb)
{
    return gb->ie & gb->io[R_IF] & 0x1F;
}

static void dispatch(GB *gb)
{
    gb->ime = 0;
    gb->ei_pending = 0;
    if (gb->halt_bug) {
        /* "ei; halt" with an interrupt pending: returns to the HALT */
        gb->pc--;
        gb->halt_bug = 0;
    }
    tick(gb);
    tick(gb);
    uint16_t ret = gb->pc;
    gb->sp--;
    wr(gb, gb->sp, (uint8_t)(ret >> 8));
    uint8_t p = pending_ints(gb);   /* re-sampled: the high-byte push may hit IE */
    gb->sp--;
    wr(gb, gb->sp, (uint8_t)ret);
    if (p) {
        int bit = 0;
        while (!(p & (1 << bit)))
            bit++;
        gb->io[R_IF] &= (uint8_t)~(1 << bit);
        gb->pc = (uint16_t)(0x40 + bit * 8);
    } else {
        gb->pc = 0;
    }
    tick(gb);
    cs_push(gb, gb->pc, ret, 1);
}

/* While halted, skip ahead in bulk to just before the next event that could
 * change anything (PPU/serial event, TIMA overflow), leaving the final
 * M-cycle to tick(). */
static void halt_skip(GB *gb, uint64_t limit)
{
#ifdef GB_NO_HALT_SKIP
    (void)gb; (void)limit;
    return;
#endif
    if (gb->tima_overflow)
        return;
    uint64_t target = gb->next_event < limit ? gb->next_event : limit;
    if (target <= gb->cycles + 4)
        return;
    uint64_t d = (target - gb->cycles - 4) & ~(uint64_t)3;
    uint8_t tac = gb->io[R_TAC];
    if (tac & 4) {
        unsigned sh = tac_shift[tac & 3];
        uint32_t period = 1u << sh;
        uint32_t to_edge = period - (gb->div16 & (period - 1));
        uint64_t maxd = to_edge + (uint64_t)(0xFF - gb->io[R_TIMA]) * period - 4;
        if (d > maxd)
            d = maxd & ~(uint64_t)3;
        uint64_t inc = (((uint64_t)gb->div16 + d) >> sh) - (gb->div16 >> sh);
        gb->io[R_TIMA] = (uint8_t)(gb->io[R_TIMA] + inc);
    }
    gb->div16 = (uint16_t)(gb->div16 + d);
    gb->cycles += d;
}

/* ---------------------------------------------------------------------- */
/* instruction execution                                                  */

static void execute(GB *gb)
{
    uint16_t op_pc = gb->pc;
    uint8_t op = rd(gb, gb->pc);
    if (gb->halt_bug)
        gb->halt_bug = 0;
    else
        gb->pc++;
    int ei_was_pending = gb->ei_pending;

    switch (op) {
    case 0x00: /* NOP */
        break;

    /* 16-bit loads */
    case 0x01: case 0x11: case 0x21: case 0x31:
        set_rr(gb, op >> 4, imm16(gb));
        break;
    case 0x08: { /* LD (nn),SP */
        uint16_t nn = imm16(gb);
        wr(gb, nn, (uint8_t)gb->sp);
        wr(gb, (uint16_t)(nn + 1), (uint8_t)(gb->sp >> 8));
        break;
    }
    case 0xF9: /* LD SP,HL */
        gb->sp = HL;
        tick(gb);
        break;
    case 0xC1: { uint16_t v = pop16(gb); gb->b = (uint8_t)(v >> 8); gb->c = (uint8_t)v; break; }
    case 0xD1: { uint16_t v = pop16(gb); gb->d = (uint8_t)(v >> 8); gb->e = (uint8_t)v; break; }
    case 0xE1: { uint16_t v = pop16(gb); gb->h = (uint8_t)(v >> 8); gb->l = (uint8_t)v; break; }
    case 0xF1: { uint16_t v = pop16(gb); gb->a = (uint8_t)(v >> 8); gb->f = (uint8_t)(v & 0xF0); break; }
    case 0xC5: tick(gb); push16(gb, BC); break;
    case 0xD5: tick(gb); push16(gb, DE); break;
    case 0xE5: tick(gb); push16(gb, HL); break;
    case 0xF5: tick(gb); push16(gb, (uint16_t)(gb->a << 8 | gb->f)); break;

    /* 8-bit loads via (rr) */
    case 0x02: wr(gb, BC, gb->a); break;
    case 0x12: wr(gb, DE, gb->a); break;
    case 0x22: { uint16_t hl = HL; wr(gb, hl, gb->a); SET_HL(hl + 1); break; }
    case 0x32: { uint16_t hl = HL; wr(gb, hl, gb->a); SET_HL(hl - 1); break; }
    case 0x0A: gb->a = rd(gb, BC); break;
    case 0x1A: gb->a = rd(gb, DE); break;
    case 0x2A: { uint16_t hl = HL; gb->a = rd(gb, hl); SET_HL(hl + 1); break; }
    case 0x3A: { uint16_t hl = HL; gb->a = rd(gb, hl); SET_HL(hl - 1); break; }

    /* 16-bit inc/dec/add */
    case 0x03: case 0x13: case 0x23: case 0x33:
        set_rr(gb, op >> 4, (uint16_t)(get_rr(gb, op >> 4) + 1));
        tick(gb);
        break;
    case 0x0B: case 0x1B: case 0x2B: case 0x3B:
        set_rr(gb, op >> 4, (uint16_t)(get_rr(gb, op >> 4) - 1));
        tick(gb);
        break;
    case 0x09: case 0x19: case 0x29: case 0x39: {
        uint16_t hl = HL, v = get_rr(gb, op >> 4);
        unsigned r = (unsigned)hl + v;
        gb->f = (uint8_t)((gb->f & FZ) | (((hl & 0xFFF) + (v & 0xFFF)) > 0xFFF ? FH : 0) | (r > 0xFFFF ? FC : 0));
        SET_HL(r);
        tick(gb);
        break;
    }

    /* 8-bit inc/dec */
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x3C:
        gb->r[op >> 3] = inc8(gb, gb->r[op >> 3]);
        break;
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x3D:
        gb->r[op >> 3] = dec8(gb, gb->r[op >> 3]);
        break;
    case 0x34: { uint16_t hl = HL; uint8_t v = rd(gb, hl); wr(gb, hl, inc8(gb, v)); break; }
    case 0x35: { uint16_t hl = HL; uint8_t v = rd(gb, hl); wr(gb, hl, dec8(gb, v)); break; }

    /* LD r,n */
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x3E:
        gb->r[op >> 3] = imm8(gb);
        break;
    case 0x36: { uint8_t v = imm8(gb); wr(gb, HL, v); break; }

    /* accumulator rotates (Z always cleared) */
    case 0x07: gb->a = rot(gb, 0, gb->a); gb->f &= FC; break;
    case 0x0F: gb->a = rot(gb, 1, gb->a); gb->f &= FC; break;
    case 0x17: gb->a = rot(gb, 2, gb->a); gb->f &= FC; break;
    case 0x1F: gb->a = rot(gb, 3, gb->a); gb->f &= FC; break;

    case 0x10: /* STOP: treated as a 2-byte NOP (no speed switch on DMG) */
        gb->pc++;
        break;

    /* relative jumps */
    case 0x18: { int8_t e = (int8_t)imm8(gb); gb->pc = (uint16_t)(gb->pc + e); tick(gb); break; }
    case 0x20: case 0x28: case 0x30: case 0x38: {
        int8_t e = (int8_t)imm8(gb);
        if (cond(gb, (op >> 3) & 3)) {
            gb->pc = (uint16_t)(gb->pc + e);
            tick(gb);
        }
        break;
    }

    case 0x27: { /* DAA */
        uint8_t a = gb->a, f = gb->f;
        if (!(f & FN)) {
            if ((f & FC) || a > 0x99) { a = (uint8_t)(a + 0x60); f |= FC; }
            if ((f & FH) || (a & 0x0F) > 0x09) a = (uint8_t)(a + 0x06);
        } else {
            if (f & FC) a = (uint8_t)(a - 0x60);
            if (f & FH) a = (uint8_t)(a - 0x06);
        }
        f &= (uint8_t)~(FZ | FH);
        if (!a) f |= FZ;
        gb->a = a;
        gb->f = f;
        break;
    }
    case 0x2F: gb->a = (uint8_t)~gb->a; gb->f |= FN | FH; break;            /* CPL */
    case 0x37: gb->f = (uint8_t)((gb->f & FZ) | FC); break;                  /* SCF */
    case 0x3F: gb->f = (uint8_t)((gb->f & FZ) | ((gb->f & FC) ^ FC)); break; /* CCF */

    case 0x76: /* HALT */
        if (!gb->ime && pending_ints(gb))
            gb->halt_bug = 1;   /* does not halt; next opcode byte is read twice */
        else
            gb->halted = 1;
        break;

    /* control flow */
    case 0xC3: gb->pc = imm16(gb); tick(gb); break;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: {
        uint16_t nn = imm16(gb);
        if (cond(gb, (op >> 3) & 3)) {
            gb->pc = nn;
            tick(gb);
        }
        break;
    }
    case 0xE9: gb->pc = HL; break;
    case 0xCD: {
        uint16_t nn = imm16(gb);
        tick(gb);
        push16(gb, gb->pc);
        cs_push(gb, nn, gb->pc, 0);
        gb->pc = nn;
        break;
    }
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: {
        uint16_t nn = imm16(gb);
        if (cond(gb, (op >> 3) & 3)) {
            tick(gb);
            push16(gb, gb->pc);
            cs_push(gb, nn, gb->pc, 0);
            gb->pc = nn;
        }
        break;
    }
    case 0xC9: gb->pc = pop16(gb); tick(gb); break;
    case 0xD9: gb->pc = pop16(gb); tick(gb); gb->ime = 1; gb->ei_pending = 0; break;
    case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        tick(gb);
        if (cond(gb, (op >> 3) & 3)) {
            gb->pc = pop16(gb);
            tick(gb);
        }
        break;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        tick(gb);
        push16(gb, gb->pc);
        cs_push(gb, op & 0x38, gb->pc, 0);
        gb->pc = op & 0x38;
        break;

    /* ALU with immediate */
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        alu(gb, (op >> 3) & 7, imm8(gb));
        break;

    /* high page / absolute loads */
    case 0xE0: { uint8_t n = imm8(gb); wr(gb, (uint16_t)(0xFF00 | n), gb->a); break; }
    case 0xF0: { uint8_t n = imm8(gb); gb->a = rd(gb, (uint16_t)(0xFF00 | n)); break; }
    case 0xE2: wr(gb, (uint16_t)(0xFF00 | gb->c), gb->a); break;
    case 0xF2: gb->a = rd(gb, (uint16_t)(0xFF00 | gb->c)); break;
    case 0xEA: { uint16_t nn = imm16(gb); wr(gb, nn, gb->a); break; }
    case 0xFA: { uint16_t nn = imm16(gb); gb->a = rd(gb, nn); break; }

    /* SP arithmetic */
    case 0xE8: { /* ADD SP,e8 */
        uint8_t e = imm8(gb);
        uint16_t sp = gb->sp;
        gb->f = (uint8_t)((((sp & 0xF) + (e & 0xF)) > 0xF ? FH : 0) | (((sp & 0xFF) + e) > 0xFF ? FC : 0));
        gb->sp = (uint16_t)(sp + (int8_t)e);
        tick(gb);
        tick(gb);
        break;
    }
    case 0xF8: { /* LD HL,SP+e8 */
        uint8_t e = imm8(gb);
        uint16_t sp = gb->sp;
        gb->f = (uint8_t)((((sp & 0xF) + (e & 0xF)) > 0xF ? FH : 0) | (((sp & 0xFF) + e) > 0xFF ? FC : 0));
        SET_HL(sp + (int8_t)e);
        tick(gb);
        break;
    }

    case 0xF3: gb->ime = 0; gb->ei_pending = 0; break; /* DI */
    case 0xFB: gb->ei_pending = 1; break;              /* EI */

    case 0xCB: {
        uint8_t op2 = imm8(gb);
        int ri = op2 & 7;
        uint8_t v = ri == 6 ? rd(gb, HL) : gb->r[ri];
        int b = (op2 >> 3) & 7;
        switch (op2 >> 6) {
        case 0: v = rot(gb, b, v); break;
        case 1: /* BIT */
            gb->f = (uint8_t)((gb->f & FC) | FH | ((v >> b) & 1 ? 0 : FZ));
            goto cb_done;
        case 2: v &= (uint8_t)~(1 << b); break;
        default: v |= (uint8_t)(1 << b); break;
        }
        if (ri == 6)
            wr(gb, HL, v);
        else
            gb->r[ri] = v;
    cb_done:
        break;
    }

    default:
        if (op >= 0x40 && op < 0x80) { /* LD r,r' (0x76 handled above) */
            int src = op & 7, dst = (op >> 3) & 7;
            uint8_t v = src == 6 ? rd(gb, HL) : gb->r[src];
            if (dst == 6)
                wr(gb, HL, v);
            else
                gb->r[dst] = v;
        } else if (op >= 0x80 && op < 0xC0) {
            int src = op & 7;
            alu(gb, (op >> 3) & 7, src == 6 ? rd(gb, HL) : gb->r[src]);
        } else {
            /* D3 DB DD E3 E4 EB EC ED F4 FC FD */
            gb->error = GB_ERR_ILLEGAL_OPCODE;
            gb->error_pc = op_pc;
            gb->error_opcode = op;
            gb->pc = op_pc;
            return;
        }
        break;
    }

    if (ei_was_pending && gb->ei_pending) {
        gb->ime = 1;
        gb->ei_pending = 0;
    }
    cs_pop_check(gb);
}

/* ---------------------------------------------------------------------- */
/* breakpoints                                                            */

static void bp_map_rebuild_addr(GB *gb, uint16_t addr)
{
    int any = 0;
    for (int i = 0; i < gb->bp_count; i++)
        if (gb->bps[i].active && gb->bps[i].addr == addr)
            any = 1;
    if (any)
        gb->bp_map[addr >> 3] |= (uint8_t)(1 << (addr & 7));
    else
        gb->bp_map[addr >> 3] &= (uint8_t)~(1 << (addr & 7));
}

int gb_add_breakpoint(GB *gb, int bank, uint16_t addr)
{
    int id = -1;
    for (int i = 0; i < gb->bp_count; i++)
        if (!gb->bps[i].active) { id = i; break; }
    if (id < 0) {
        if (gb->bp_count == gb->bp_cap) {
            int ncap = gb->bp_cap ? gb->bp_cap * 2 : 64;
            GBBreakpoint *n = realloc(gb->bps, sizeof *n * (size_t)ncap);
            if (!n)
                return -1;
            gb->bps = n;
            gb->bp_cap = ncap;
        }
        id = gb->bp_count++;
    }
    gb->bps[id].bank = bank;
    gb->bps[id].addr = addr;
    gb->bps[id].active = 1;
    gb->bp_active++;
    gb->bp_map[addr >> 3] |= (uint8_t)(1 << (addr & 7));
    return id;
}

void gb_remove_breakpoint(GB *gb, int id)
{
    if (id < 0 || id >= gb->bp_count || !gb->bps[id].active)
        return;
    gb->bps[id].active = 0;
    gb->bp_active--;
    bp_map_rebuild_addr(gb, gb->bps[id].addr);
}

void gb_clear_breakpoints(GB *gb)
{
    gb->bp_count = 0;
    gb->bp_active = 0;
    memset(gb->bp_map, 0, sizeof gb->bp_map);
}

/* Returns 1 if execution must stop before the instruction at PC. */
static int bp_check(GB *gb)
{
    uint16_t pc = gb->pc;
    if (!(gb->bp_map[pc >> 3] & (1 << (pc & 7))))
        return 0;
    int bank = gb_bank_of(gb, pc);
    int hit = -1;
    for (int i = 0; i < gb->bp_count; i++) {
        const GBBreakpoint *bp = &gb->bps[i];
        if (bp->active && bp->addr == pc &&
            (bp->bank < 0 || pc < 0x4000 || pc >= 0x8000 || bp->bank == bank)) {
            hit = i;
            break;
        }
    }
    if (hit < 0)
        return 0;
    for (int i = gb->bp_skip_n - 1; i >= 0; i--) {
        const GBSkip *k = &gb->bp_skip[i];
        if (k->pc == pc && k->bank == bank && k->sp == gb->sp) {
            gb->bp_skip_n = i;   /* resuming from this stop (deeper entries are stale) */
            return 0;
        }
    }
    while (gb->bp_skip_n > 0 && gb->bp_skip[gb->bp_skip_n - 1].sp <= gb->sp)
        gb->bp_skip_n--;
    if (gb->bp_skip_n == GB_BP_SKIP_MAX) {
        memmove(gb->bp_skip, gb->bp_skip + 1, sizeof(GBSkip) * (GB_BP_SKIP_MAX - 1));
        gb->bp_skip_n--;
    }
    GBSkip *k = &gb->bp_skip[gb->bp_skip_n++];
    k->pc = pc;
    k->sp = gb->sp;
    k->bank = bank;
    gb->hit_breakpoint = hit;
    return 1;
}

/* Called right before an instruction executes: drop reported stops that are
 * no longer pending at this stack level. */
static inline void bp_skip_cleanup(GB *gb)
{
    while (gb->bp_skip_n > 0 && gb->bp_skip[gb->bp_skip_n - 1].sp <= gb->sp)
        gb->bp_skip_n--;
}

/* ---------------------------------------------------------------------- */
/* run loop                                                               */

int gb_step(GB *gb)
{
    if (gb->error)
        return GB_STOP_ERROR;
    uint8_t pend = pending_ints(gb);
    if (gb->halted) {
        if (!pend) {
            tick(gb);
            return 0;
        }
        gb->halted = 0;
        tick(gb);
        return 0;
    }
    if (pend && gb->ime) {
        dispatch(gb);
        return 0;
    }
    bp_skip_cleanup(gb);
    execute(gb);
    return gb->error ? GB_STOP_ERROR : 0;
}

int gb_run(GB *gb, uint64_t max_cycles, int stop_on_frame)
{
    uint64_t end = max_cycles > UINT64_MAX - gb->cycles ? UINT64_MAX : gb->cycles + max_cycles;
    uint64_t f0 = gb->frames;
    gb->hit_breakpoint = -1;
    for (;;) {
        if (gb->error)
            return GB_STOP_ERROR;
        if (stop_on_frame && gb->frames != f0)
            return GB_STOP_FRAME;
        if (gb->cycles >= end)
            return GB_STOP_CYCLES;
        uint8_t pend = pending_ints(gb);
        if (gb->halted) {
            if (!pend) {
                uint64_t hf = gb->frames;
                while (!pending_ints(gb) && gb->cycles < end && gb->frames == hf) {
                    halt_skip(gb, end);
                    tick(gb);
                }
                continue;
            }
            gb->halted = 0;
            tick(gb);   /* wake-up M-cycle */
            continue;
        }
        if (pend && gb->ime) {
            dispatch(gb);
            continue;
        }
        if (gb->bp_active && bp_check(gb))
            return GB_STOP_BREAKPOINT;
        if (gb->bp_skip_n)
            bp_skip_cleanup(gb);
        execute(gb);
    }
}

/* ---------------------------------------------------------------------- */
/* lifetime                                                               */

static uint32_t sram_size_from_header(uint8_t code)
{
    switch (code) {
    case 1: return 0x800;
    case 2: return 0x2000;
    case 0: return 0;
    default: return 0x8000; /* 32KB (larger sizes are truncated to 32KB) */
    }
}

void gb_reset(GB *gb)
{
    uint8_t *saved = malloc(sizeof gb->sram);
    if (saved)
        memcpy(saved, gb->sram, sizeof gb->sram);
    memset(gb, 0, GB_STATE_SIZE);
    if (saved) {
        memcpy(gb->sram, saved, sizeof gb->sram);
        free(saved);
    }

    /* cartridge */
    uint8_t type = gb->rom[0x147];
    if (type == 0x00 || type == 0x08 || type == 0x09)
        gb->mbc = MBC_NONE;
    else if (type >= 0x01 && type <= 0x03)
        gb->mbc = MBC_1;
    else
        gb->mbc = MBC_5;
    gb->sram_size = sram_size_from_header(gb->rom[0x149]);
    if (gb->mbc != MBC_NONE && gb->sram_size < 0x2000)
        gb->sram_size = 0x2000;
    gb->rom_bank = 1;
    mbc_update(gb);

    /* DMG post-boot CPU state */
    gb->a = 0x01; gb->f = 0xB0;
    gb->b = 0x00; gb->c = 0x13;
    gb->d = 0x00; gb->e = 0xD8;
    gb->h = 0x01; gb->l = 0x4D;
    gb->sp = 0xFFFE;
    gb->pc = 0x0100;
    gb->hit_breakpoint = -1;

    /* DMG post-boot IO state */
    gb->div16 = 0xABCC;
    gb->io[R_P1] = 0x00;
    gb->io[R_SC] = 0x00;
    gb->io[R_TAC] = 0x00;
    gb->io[R_IF] = 0x01;
    static const uint8_t apu_init[0x17] = {
        0x80, 0xBF, 0xF3, 0xFF, 0xBF, 0x00, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF,
        0x9F, 0xFF, 0xBF, 0x00, 0xFF, 0x00, 0x00, 0xBF, 0x77, 0xF3, 0x80,
    };
    memcpy(&gb->io[R_NR10], apu_init, sizeof apu_init);
    gb->io[R_LCDC] = 0x91;
    gb->io[R_DMA] = 0xFF;
    gb->io[R_BGP] = 0xFC;
    gb->io[R_OBP0] = 0xFF;
    gb->io[R_OBP1] = 0xFF;
    gb->ie = 0x00;

    /* PPU: start of line 0, OAM scan */
    gb->serial_next = NEVER;
    gb->ppu_line = 0;
    gb->io[R_LY] = 0;
    gb->io[R_STAT] = 0;
    check_lyc(gb);
    set_mode(gb, 2);
    gb->wy_hit = gb->io[R_WY] == 0;
    gb->ppu_phase = PH_MODE3;
    gb->ppu_next = DOTS_MODE2;
    gb->stat_line = 0;
    update_next_event(gb);

    if (gb->render) {
        memset(gb->screen, 0, sizeof gb->screen);
        memset(gb->frame, 0, sizeof gb->frame);
    }
}

GB *gb_new(const uint8_t *rom, size_t rom_size)
{
    if (!rom || rom_size < 0x150)
        return NULL;
    GB *gb = calloc(1, sizeof *gb);
    if (!gb)
        return NULL;
    size_t size = rom_size < 0x8000 ? 0x8000 : (rom_size + 0x3FFF) & ~(size_t)0x3FFF;
    uint8_t *copy = malloc(size);
    if (!copy) {
        free(gb);
        return NULL;
    }
    memset(copy, 0xFF, size);
    memcpy(copy, rom, rom_size);
    gb->rom = copy;
    gb->rom_size = size;
    gb->rom_banks = (uint32_t)(size / 0x4000);
    gb->render = 1;
    gb_reset(gb);
    return gb;
}

void gb_free(GB *gb)
{
    if (!gb)
        return;
    free((void *)gb->rom);
    free(gb->bps);
    free(gb);
}

/* ---------------------------------------------------------------------- */
/* snapshots                                                              */

size_t gb_state_size(void) { return GB_STATE_SIZE; }
void gb_save_state(const GB *gb, void *buf) { memcpy(buf, gb, GB_STATE_SIZE); }
void gb_load_state(GB *gb, const void *buf) { memcpy(gb, buf, GB_STATE_SIZE); }

/* ---------------------------------------------------------------------- */
/* debugging output                                                       */

static const uint8_t shade_gray[4] = { 0xFF, 0xAA, 0x55, 0x00 };

int gb_write_ppm(GB *gb, const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return -1;
    fprintf(fp, "P6\n%d %d\n255\n", GB_SCREEN_W, GB_SCREEN_H);
    for (int y = 0; y < GB_SCREEN_H; y++)
        for (int x = 0; x < GB_SCREEN_W; x++) {
            uint8_t g = shade_gray[gb->frame[y][x] & 3];
            uint8_t px[3] = { g, g, g };
            fwrite(px, 1, 3, fp);
        }
    return fclose(fp) ? -1 : 0;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    }
    return ~crc;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static int png_chunk(FILE *fp, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    put32(hdr, len);
    memcpy(hdr + 4, type, 4);
    uint32_t crc = crc32_update(0, hdr + 4, 4);
    crc = crc32_update(crc, data, len);
    uint8_t tail[4];
    put32(tail, crc);
    return fwrite(hdr, 1, 8, fp) == 8 && fwrite(data, 1, len, fp) == len && fwrite(tail, 1, 4, fp) == 4;
}

int gb_write_png(GB *gb, const char *path)
{
    enum { ROW = 1 + GB_SCREEN_W, RAW = ROW * GB_SCREEN_H };
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    uint8_t ihdr[13];
    put32(ihdr, GB_SCREEN_W);
    put32(ihdr + 4, GB_SCREEN_H);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 0;  /* grayscale */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;

    /* zlib stream: header, one stored deflate block, adler32 */
    uint8_t *z = malloc(2 + 5 + RAW + 4);
    if (!z)
        return -1;
    uint8_t *raw = z + 7;
    for (int y = 0; y < GB_SCREEN_H; y++) {
        raw[y * ROW] = 0; /* filter: none */
        for (int x = 0; x < GB_SCREEN_W; x++)
            raw[y * ROW + 1 + x] = shade_gray[gb->frame[y][x] & 3];
    }
    z[0] = 0x78; z[1] = 0x01;
    z[2] = 0x01; /* BFINAL=1, BTYPE=00 */
    z[3] = RAW & 0xFF; z[4] = RAW >> 8;
    z[5] = (uint8_t)~(RAW & 0xFF); z[6] = (uint8_t)~(RAW >> 8);
    uint32_t s1 = 1, s2 = 0;
    for (int i = 0; i < RAW; i++) {
        s1 = (s1 + raw[i]) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    put32(z + 7 + RAW, s2 << 16 | s1);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        free(z);
        return -1;
    }
    int ok = fwrite(sig, 1, 8, fp) == 8 &&
             png_chunk(fp, "IHDR", ihdr, 13) &&
             png_chunk(fp, "IDAT", z, 2 + 5 + RAW + 4) &&
             png_chunk(fp, "IEND", sig, 0);
    free(z);
    if (fclose(fp))
        ok = 0;
    return ok ? 0 : -1;
}

const char *gb_error_string(GB *gb)
{
    switch (gb->error) {
    case GB_ERR_NONE:
        return "no error";
    case GB_ERR_ILLEGAL_OPCODE: {
        int bank = gb_bank_of(gb, gb->error_pc);
        snprintf(gb->errbuf, sizeof gb->errbuf, "illegal opcode $%02X at %02X:%04X",
                 gb->error_opcode, bank < 0 ? 0 : bank, gb->error_pc);
        return gb->errbuf;
    }
    default:
        return "unknown error";
    }
}
