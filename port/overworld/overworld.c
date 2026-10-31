#include "overworld.h"
#include "../src/gen/constants.h"
#include "../src/gen/text.h"
#include "gen/data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(sizeof(OwNpc) == 6, "OwNpc wire layout");
_Static_assert(sizeof(OwDuel) == 4, "OwDuel wire layout");

static uint8_t byte(uint16_t p) {
  return p >= 0x4000 && p < 0x8000 ? ow_script_bank[p - 0x4000] : 0;
}
static uint16_t word(uint16_t p) {
  return (uint16_t)(byte(p) | (uint16_t)byte(p + 1) << 8);
}
/* Same three byte RNG and multiply-high range reduction as home/random.asm. */
static unsigned random_value(OwState *s, unsigned limit) {
  uint8_t e = (uint8_t)s->rng, d = (uint8_t)(s->rng >> 8),
          counter = (uint8_t)(s->rng >> 16);
  uint8_t a = (uint8_t)(((d << 2) | (d >> 6)) ^ e);
  unsigned carry = a & 1;
  d ^= e;
  e ^= counter;
  unsigned next = e >> 7;
  e = (uint8_t)((e << 1) | carry);
  d = (uint8_t)((d << 1) | next);
  s->rng = (uint32_t)e | ((uint32_t)d << 8) |
           ((uint32_t)(uint8_t)(counter + 1) << 16);
  unsigned raw = d ^ e;
  return limit == 256 ? raw : (raw * (limit & 255)) >> 8;
}
static void error(OwState *s, const OwHost *h, const char *what, unsigned pc) {
  snprintf(s->error, sizeof(s->error), "%s at script %04x", what, pc);
  if (h && h->error)
    h->error(h->ctx, s->error);
}
static void changed(OwState *s, const OwHost *h) {
  if (h && h->changed)
    h->changed(h->ctx, s);
}
static bool feature(OwState *s, const OwHost *h, unsigned f, unsigned a) {
  if (h && h->feature && h->feature(h->ctx, s, f, a))
    return true;
  error(s, h, "Feature unavailable", s->script_pc);
  return false;
}
static unsigned count(const OwHost *h, unsigned card, bool decks) {
  return h && h->card_count ? h->card_count(h->ctx, card, decks) : 0;
}
static void give(const OwHost *h, unsigned card, int delta) {
  if (h && h->card_change)
    h->card_change(h->ctx, card, delta);
}
unsigned ow_event(const OwState *s, unsigned e) {
  if (e >= NUM_EVENT_FLAGS)
    return 0;
  unsigned off = ow_event_masks[e * 2], mask = ow_event_masks[e * 2 + 1],
           v = s->events[off] & mask;
  while (mask && !(mask & 1)) {
    mask >>= 1;
    v >>= 1;
  }
  return v;
}
void ow_set_event(OwState *s, unsigned e, unsigned v) {
  if (e >= NUM_EVENT_FLAGS)
    return;
  unsigned off = ow_event_masks[e * 2], mask = ow_event_masks[e * 2 + 1],
           shift = 0;
  while (mask && !(mask & 1)) {
    mask >>= 1;
    shift++;
  }
  s->events[off] = (uint8_t)((s->events[off] & ~ow_event_masks[e * 2 + 1]) |
                             ((v & mask) << shift));
}
static OwNpc *npc(OwState *s, unsigned id) {
  for (unsigned i = 0; i < s->npc_count; i++)
    if (s->npcs[i].id == id && s->npcs[i].visible)
      return &s->npcs[i];
  return NULL;
}
static void text(OwState *s, const OwHost *h, unsigned id, bool named) {
  if (id && h && h->text)
    h->text(h->ctx, s, (uint16_t)id, named ? s->dialog_npc : 0);
}
static int choice(OwState *s, const OwHost *h, unsigned kind, unsigned id,
                  unsigned n, unsigned initial) {
  return h && h->choice ? h->choice(h->ctx, s, kind, (uint16_t)id, n, initial)
                        : -1;
}
static void step_position(uint8_t *x, uint8_t *y, unsigned d) {
  static const int8_t dx[] = {0, 2, 0, -2}, dy[] = {-2, 0, 2, 0};
  *x = (uint8_t)(*x + dx[d & 3]);
  *y = (uint8_t)(*y + dy[d & 3]);
}
static void movement(OwState *s, const OwHost *h, unsigned id, uint16_t pc) {
  OwNpc *n = npc(s, id);
  if (!n)
    return;
  for (unsigned i = 0; i < 256; i++) {
    uint8_t d = byte(pc++);
    if (d == 255)
      break;
    if (d >= 240) {
      pc = (uint16_t)(pc + (int8_t)byte(pc));
      continue;
    }
    n->direction = d & 3;
    if (!(d & 128))
      step_position(&n->x, &n->y, d);
    changed(s, h);
    /* UpdateNPCMovementStep advances one source pixel per 60 Hz frame.
       Rotations (NO_MOVE) advance directly to the next movement byte. */
    if (!(d & 128) && h && h->wait)
      h->wait(h->ctx, 16);
  }
}
static void medal_packs(OwState *s, const OwHost *h) {
  static const uint8_t events[] = {
      EVENT_BEAT_NIKKI, EVENT_BEAT_RICK,   EVENT_BEAT_KEN,  EVENT_BEAT_AMY,
      EVENT_BEAT_ISAAC, EVENT_BEAT_MURRAY, EVENT_BEAT_GENE, EVENT_BEAT_MITCH};
  unsigned n = 0;
  for (unsigned i = 0; i < 8; i++)
    n += !!ow_event(s, events[i]);
  ow_set_event(s, EVENT_MEDAL_COUNT, n);
  if (n >= 8)
    feature(s, h, OW_FEATURE_PC_PACK, 12);
  if (n >= 7)
    feature(s, h, OW_FEATURE_PC_PACK, 11);
  if (n >= 3)
    feature(s, h, OW_FEATURE_PC_PACK, 10);
}

typedef struct {
  uint16_t af, bc, de, hl;
  bool z, carry;
} Reg;
static unsigned reg_get(Reg *r, const char *n) {
  if (!strcmp(n, "a"))
    return r->af >> 8;
  if (!strcmp(n, "b"))
    return r->bc >> 8;
  if (!strcmp(n, "c"))
    return r->bc & 255;
  if (!strcmp(n, "d"))
    return r->de >> 8;
  if (!strcmp(n, "e"))
    return r->de & 255;
  if (!strcmp(n, "h"))
    return r->hl >> 8;
  if (!strcmp(n, "l"))
    return r->hl & 255;
  if (!strcmp(n, "bc"))
    return r->bc;
  if (!strcmp(n, "de"))
    return r->de;
  if (!strcmp(n, "hl"))
    return r->hl;
  if (!strcmp(n, "af"))
    return (r->af & 0xff00) | (r->z ? 128 : 0) | (r->carry ? 16 : 0);
  return (unsigned)strtoul(n, NULL, 10);
}
static void reg_set(Reg *r, const char *n, unsigned v) {
  v &= 65535;
  if (!strcmp(n, "a"))
    r->af = (uint16_t)((v & 255) << 8);
  else if (!strcmp(n, "b"))
    r->bc = (uint16_t)((r->bc & 255) | (v & 255) << 8);
  else if (!strcmp(n, "c"))
    r->bc = (uint16_t)((r->bc & 0xff00) | (v & 255));
  else if (!strcmp(n, "d"))
    r->de = (uint16_t)((r->de & 255) | (v & 255) << 8);
  else if (!strcmp(n, "e"))
    r->de = (uint16_t)((r->de & 0xff00) | (v & 255));
  else if (!strcmp(n, "h"))
    r->hl = (uint16_t)((r->hl & 255) | (v & 255) << 8);
  else if (!strcmp(n, "l"))
    r->hl = (uint16_t)((r->hl & 0xff00) | (v & 255));
  else if (!strcmp(n, "bc"))
    r->bc = (uint16_t)v;
  else if (!strcmp(n, "de"))
    r->de = (uint16_t)v;
  else if (!strcmp(n, "hl"))
    r->hl = (uint16_t)v;
  else if (!strcmp(n, "af")) {
    r->af = (uint16_t)v;
    r->z = !!(v & 128);
    r->carry = !!(v & 16);
  }
}
#include "gen/native_fields.h"
static unsigned field_index(const char *operand, unsigned *extra) {
  char name[80];
  size_t n = strcspn(operand + 1, "] +");
  if (n > 79)
    n = 79;
  memcpy(name, operand + 1, n);
  name[n] = 0;
  const char *plus = strchr(operand, '+');
  *extra = plus ? (unsigned)strtoul(plus + 1, NULL, 0) : 0;
  for (unsigned i = 0; i < sizeof(native_fields) / sizeof(native_fields[0]);
       i++)
    if (!strcmp(name, native_fields[i].name))
      return i;
  return 255;
}
static unsigned native_field_get(OwState *s, unsigned i, unsigned extra) {
  switch (i) {
  case 1:
    return s->challenge_npc;
  case 2:
    return CONSOLE_CGB;
  case 3:
    return s->map;
  case 4:
    return (s->native_words[0] >> (extra * 8)) & 255;
  case 5:
    return s->default_song;
  case 6:
    return s->last_duel_result;
  case 12:
    return s->duel_npc;
  case 13:
    return s->direction;
  case 14:
    return s->x;
  case 15:
    return s->y;
  case 17:
    return (s->txram[0] >> (extra * 8)) & 255;
  case 18:
    return (s->txram[1] >> (extra * 8)) & 255;
  case 19:
    return (s->txram[2] >> (extra * 8)) & 255;
  case 20:
    return (s->txram[3] >> (extra * 8)) & 255;
  case 22:
    return s->challenge_history[extra];
  default:
    return s->native_vars[i];
  }
}
static void native_field_set(OwState *s, unsigned i, unsigned extra,
                             unsigned v) {
  v &= 255;
  uint16_t mask = (uint16_t)(255u << (extra * 8));
  switch (i) {
  case 1:
    s->challenge_npc = (uint8_t)v;
    break;
  case 3:
    s->map = (uint8_t)v;
    break;
  case 4:
    s->native_words[0] =
        (uint16_t)((s->native_words[0] & ~mask) | (v << (extra * 8)));
    break;
  case 5:
    s->default_song = (uint8_t)v;
    break;
  case 6:
    s->last_duel_result = (uint8_t)v;
    break;
  case 12:
    s->duel_npc = (uint8_t)v;
    break;
  case 13:
    s->direction = (uint8_t)v;
    break;
  case 14:
    s->x = (uint8_t)v;
    break;
  case 15:
    s->y = (uint8_t)v;
    break;
  case 17:
    s->txram[0] = (uint16_t)((s->txram[0] & ~mask) | (v << (extra * 8)));
    break;
  case 18:
    s->txram[1] = (uint16_t)((s->txram[1] & ~mask) | (v << (extra * 8)));
    break;
  case 19:
    s->txram[2] = (uint16_t)((s->txram[2] & ~mask) | (v << (extra * 8)));
    break;
  case 20:
    s->txram[3] = (uint16_t)((s->txram[3] & ~mask) | (v << (extra * 8)));
    break;
  case 22:
    s->challenge_history[extra] = (uint8_t)v;
    break;
  default:
    s->native_vars[i] = (uint8_t)v;
    break;
  }
}
static unsigned ram_field(unsigned address, unsigned *extra) {
  for (unsigned i = 0; i < sizeof(native_fields) / sizeof(native_fields[0]);
       i++)
    if (address >= native_fields[i].address &&
        address < native_fields[i].address + native_fields[i].width) {
      *extra = address - native_fields[i].address;
      return i;
    }
  return 255;
}
static unsigned mem_get(OwState *s, Reg *r, const char *p) {
  unsigned i, extra = 0;
  if (!strcmp(p, "[hl]") || !strcmp(p, "[hli]") || !strcmp(p, "[hld]")) {
    unsigned address = r->hl;
    if (p[3] == 'i')
      r->hl++;
    if (p[3] == 'd')
      r->hl--;
    if (address >= 0x4000 && address < 0x8000)
      return byte((uint16_t)address);
    i = ram_field(address, &extra);
  } else
    i = field_index(p, &extra);
  if (i == 255 || extra >= native_fields[i].width) {
    snprintf(s->error, sizeof(s->error),
             "Unmapped native memory read %s at %04x", p, s->script_pc);
    return 0;
  }
  return native_field_get(s, i, extra);
}
static void mem_set(OwState *s, Reg *r, const char *p, unsigned v) {
  unsigned i, extra = 0;
  if (!strcmp(p, "[hl]") || !strcmp(p, "[hli]") || !strcmp(p, "[hld]")) {
    unsigned address = r->hl;
    if (p[3] == 'i')
      r->hl++;
    if (p[3] == 'd')
      r->hl--;
    i = ram_field(address, &extra);
  } else
    i = field_index(p, &extra);
  if (i == 255 || extra >= native_fields[i].width) {
    snprintf(s->error, sizeof(s->error),
             "Unmapped native memory write %s at %04x", p, s->script_pc);
    return;
  }
  native_field_set(s, i, extra, v);
}
static unsigned operand(OwState *s, Reg *r, const char *p) {
  return p[0] == '[' ? mem_get(s, r, p) : reg_get(r, p);
}
static void assign(OwState *s, Reg *r, const char *p, unsigned v) {
  if (p[0] == '[')
    mem_set(s, r, p, v);
  else
    reg_set(r, p, v);
}
static bool condition(Reg *r, const char *p) {
  return !strcmp(p, "z")    ? r->z
         : !strcmp(p, "nz") ? !r->z
         : !strcmp(p, "c")  ? r->carry
         : !strcmp(p, "nc") ? !r->carry
                            : true;
}
static const OwNative *native_at(uint16_t pc) {
  unsigned lo = 0, hi = ow_native_count;
  while (lo < hi) {
    unsigned m = (lo + hi) / 2;
    if (ow_native[m].pc < pc)
      lo = m + 1;
    else
      hi = m;
  }
  return lo < ow_native_count && ow_native[lo].pc == pc ? &ow_native[lo] : NULL;
}
static bool helper(OwState *s, const OwHost *h, Reg *r, uint16_t target) {
  const char *names[] = {"FindLoadedNPC",
                         "SetNextScript",
                         "SetNextNPCAndScript",
                         "FindEndOfDuelScript",
                         "TryGiveMedalPCPacks",
                         "TryGivePCPack",
                         "GetEventValue",
                         "MaxOutEventValue",
                         "GetCardCountInCollection",
                         "AddCardToCollection",
                         "GetCardName",
                         "Random",
                         "UpdateRNGSources",
                         "PlaySFX",
                         "SetOWMapEvent",
                         "ApplyOWMapEventChangeIfEventSet",
                         "FindExtraInteractableObjects"};
  for (unsigned i = 0; i < sizeof(names) / sizeof(*names); i++)
    if (target == ow_script_address(names[i])) {
      unsigned a = reg_get(r, "a");
      switch (i) {
      case 0:
        r->carry = npc(s, s->native_vars[16]) == NULL;
        break;
      case 1:
        s->next_script = r->bc;
        break;
      case 2:
        s->next_script = r->bc;
        s->next_npc = s->native_vars[16];
        break;
      case 3:
        for (uint16_t p = r->hl; byte(p); p += 6)
          if (byte(p) == s->duel_npc) {
            s->next_npc = byte(p + 1);
            s->next_script = word(p + (s->last_duel_result ? 4 : 2));
            break;
          }
        break;
      case 4:
        medal_packs(s, h);
        reg_set(r, "a", ow_event(s, EVENT_MEDAL_COUNT));
        break;
      case 5:
        feature(s, h, OW_FEATURE_PC_PACK, a);
        break;
      case 6:
        a = ow_event(s, a);
        r->z = !a;
        r->carry = false;
        reg_set(r, "a", a);
        break;
      case 7:
        ow_set_event(s, a, 255);
        break;
      case 8:
        a = count(h, a, false);
        reg_set(r, "a", a);
        r->z = !a;
        break;
      case 9:
        give(h, a, 1);
        break;
      case 10:
        reg_set(r, "de",
                reg_get(r, "e") <= 228 ? native_card_names[reg_get(r, "e")]
                                       : 0);
        break;
      case 11:
        a = random_value(s, a);
        reg_set(r, "a", a);
        r->z = true;
        r->carry = false;
        break;
      case 12:
        a = random_value(s, 256);
        reg_set(r, "a", a);
        r->carry = false;
        r->z = !a;
        break;
      case 13:
        if (h && h->sound)
          h->sound(h->ctx, 1, a);
        break;
      case 14:
        if (a < 11)
          s->map_patch[a] = 1;
        feature(s, h, OW_FEATURE_MAP_PATCH, a);
        break;
      case 15:
        if (a < 11 && s->map_patch[a])
          feature(s, h, OW_FEATURE_MAP_PATCH, a);
        break;
      case 16:
        for (uint16_t p = r->hl; byte(p) || byte(p + 1); p += 5)
          if (byte(p) == s->x && byte(p + 1) == s->y &&
              byte(p + 2) == s->direction) {
            s->next_script = word(p + 3);
            break;
          }
        break;
      }
      return true;
    }
  return false;
}
static bool native_run(OwState *s, const OwHost *h, uint16_t pc, Reg *result) {
  Reg r = {0};
  uint16_t stack[64];
  unsigned depth = 0;
  for (unsigned step = 0; step < 10000; step++) {
    const OwNative *n = native_at(pc);
    if (!n) {
      if (byte(pc) == 0xe7) {
        s->next_script = pc + 1;
        s->next_npc = s->active_npc;
      } else
        error(s, h, "Native control flow unavailable", pc);
      if (result)
        *result = r;
      return !s->error[0];
    }
    unsigned a = !strcmp(n->op, "ld") ? 0 : operand(s, &r, n->a),
             b = operand(s, &r, n->b), v;
    pc = n->next;
    const char *op = n->op;
    if (!strcmp(op, "ld")) {
      assign(s, &r, n->a, b);
    } else if (!strcmp(op, "lb")) {
      reg_set(&r, n->a, (b << 8) | operand(s, &r, n->c));
    } else if (!strcmp(op, "get_event_value")) {
      v = ow_event(s, a);
      reg_set(&r, "a", v);
      r.z = !v;
      r.carry = false;
    } else if (!strcmp(op, "set_event_value"))
      ow_set_event(s, a, r.bc & 255);
    else if (!strcmp(op, "max_event_value"))
      ow_set_event(s, a, 255);
    else if (!strcmp(op, "set_event_false") || !strcmp(op, "set_event_zero"))
      ow_set_event(s, a, 0);
    else if (!strcmp(op, "scf"))
      r.carry = true;
    else if (!strcmp(op, "ccf"))
      r.carry = !r.carry;
    else if (!strcmp(op, "cp")) {
      v = reg_get(&r, "a");
      r.z = v == a;
      r.carry = v < a;
    } else if (!strcmp(op, "or") || !strcmp(op, "and") || !strcmp(op, "xor")) {
      v = reg_get(&r, "a");
      v = !strcmp(op, "or") ? v | a : !strcmp(op, "and") ? v & a : v ^ a;
      reg_set(&r, "a", v);
      r.z = !v;
      r.carry = false;
    } else if (!strcmp(op, "add") || !strcmp(op, "sub")) {
      const char *dst = n->b[0] ? n->a : "a";
      v = operand(s, &r, dst);
      unsigned right = n->b[0] ? b : a;
      bool sub = !strcmp(op, "sub");
      unsigned sum = sub ? v - right : v + right;
      assign(s, &r, dst, sum);
      r.carry = sub ? v<right : sum>(!strcmp(dst, "hl") ? 65535 : 255);
      if (strcmp(dst, "hl"))
        r.z = (sum & 255) == 0;
    } else if (!strcmp(op, "inc") || !strcmp(op, "dec")) {
      v = a + (!strcmp(op, "inc") ? 1 : -1);
      assign(s, &r, n->a, v);
      if (strlen(n->a) != 2)
        r.z = (v & 255) == 0;
    } else if (!strcmp(op, "srl")) {
      assign(s, &r, n->a, a >> 1);
      r.carry = a & 1;
      r.z = !(a >> 1);
    } else if (!strcmp(op, "rlca")) {
      v = reg_get(&r, "a");
      reg_set(&r, "a", (v << 1) | (v >> 7));
      r.carry = v >> 7;
      r.z = false;
    } else if (!strcmp(op, "push")) {
      if (depth >= 64) {
        error(s, h, "Native stack overflow", pc);
        break;
      }
      stack[depth++] = (uint16_t)a;
    } else if (!strcmp(op, "pop")) {
      if (!depth) {
        error(s, h, "Native stack underflow", pc);
        break;
      }
      reg_set(&r, n->a, stack[--depth]);
    } else if (!strcmp(op, "ret")) {
      if (!n->a[0] || condition(&r, n->a)) {
        if (!depth) {
          if (result)
            *result = r;
          return true;
        }
        pc = stack[--depth];
      }
    } else if (!strcmp(op, "call") || !strcmp(op, "farcall") ||
               !strcmp(op, "jp") || !strcmp(op, "jr")) {
      bool conditional = n->b[0] != 0;
      uint16_t target = (uint16_t)(conditional ? b : a);
      if (conditional && !condition(&r, n->a))
        continue;
      bool call = !strcmp(op, "call") || !strcmp(op, "farcall");
      if (target == ow_script_address("JumpToFunctionInTable")) {
        pc = word(r.hl + (reg_get(&r, "a") * 2));
        continue;
      }
      if (helper(s, h, &r, target)) {
        if (!call) {
          if (!depth) {
            if (result)
              *result = r;
            return true;
          }
          pc = stack[--depth];
        }
      } else {
        if (call) {
          if (depth >= 64) {
            error(s, h, "Native stack overflow", pc);
            break;
          }
          stack[depth++] = pc;
        }
        pc = target;
      }
    } else if (strcmp(op, "debug_nop")) {
      error(s, h, "Unknown native operation", n->pc);
      break;
    }
    if (s->error[0])
      break;
  }
  if (!s->error[0])
    error(s, h, "Native instruction limit", pc);
  if (result)
    *result = r;
  return false;
}

static void run_pending(OwState *, const OwHost *);
static bool apply_warp(OwState *s, const OwHost *h);
static unsigned default_song(const OwState *s) {
  bool ronald = false;
  for (unsigned i = 0; i < s->npc_count; i++)
    if (s->npcs[i].visible &&
        (s->npcs[i].id == NPC_RONALD1 || s->npcs[i].id == NPC_RONALD2 ||
         s->npcs[i].id == NPC_RONALD3))
      ronald = true;
  return ronald && s->overworld_selection != OWMAP_ISHIHARAS_HOUSE &&
                 s->overworld_selection != OWMAP_CHALLENGE_HALL &&
                 s->overworld_selection != OWMAP_POKEMON_DOME
             ? MUSIC_RONALD
             : s->default_song;
}
static void play_default_song(OwState *s, const OwHost *h) {
  unsigned song = default_song(s);
  if (song < NUM_SONGS) {
    s->song_override = (uint8_t)song;
    if (h && h->sound)
      h->sound(h->ctx, 0, song);
  }
}
static unsigned permission(const OwState *s, unsigned x, unsigned y) {
  const OwMap *m = &ow_maps[s->map];
  unsigned value = m->permissions[(y / 2) * ((m->width + 1) / 2) + x / 2];
  for (unsigned i = 0; i < 11; i++) {
    const OwPermissionPatch *p = &ow_permission_patches[i];
    if (s->map_patch[i] && p->map == s->map && p->permissions && x >= p->x &&
        y >= p->y && x < p->x + p->w && y < p->y + p->h)
      value =
          p->permissions[((y - p->y) / 2) * ((p->w + 1) / 2) + (x - p->x) / 2];
  }
  return value;
}
static bool load_npcs(OwState *s, const OwHost *h) {
  unsigned map = s->map;
  s->npc_count = 0;
  memset(s->npc_flags, 0, sizeof(s->npc_flags));
  memset(s->npc_animation, 0, sizeof(s->npc_animation));
  const OwNpcDef *d = ow_map_npcs[map];
  for (; d->id; d++) {
    if (s->npc_count >= OW_NPC_LIMIT) {
      error(s, h, "Too many NPCs", map);
      return false;
    }
    s->native_vars[16] = d->id;
    s->native_vars[8] = d->dir;
    s->native_vars[9] = d->x;
    s->native_vars[10] = d->y;
    Reg r = {0};
    if (d->preload && !native_run(s, h, d->preload, &r))
      return false;
    if (d->preload && !r.carry)
      continue;
    unsigned id = s->native_vars[16];
    if (id >= NUM_NPCS)
      continue;
    s->npc_flags[s->npc_count] = ow_npc_headers[id].flags;
    s->npc_animation[s->npc_count] = ow_npc_headers[id].animation;
    if (id == NPC_AMY && s->native_vars[21]) {
      /* Func_c998 changes Amy to the ordinary upright animation when her
         after-duel preload places her beside the bench at (20,4). */
      s->npc_animation[s->npc_count] = SPRITE_ANIM_BLUE_NPC_UP;
      s->npc_flags[s->npc_count] = 0;
    }
    OwNpc *n = &s->npcs[s->npc_count++];
    *n = (OwNpc){
        id, s->native_vars[9],        s->native_vars[10], s->native_vars[8],
        1,  ow_npc_headers[id].sprite};
  }
  return true;
}
void ow_load_map(OwState *s, const OwHost *h, unsigned map, unsigned x,
                 unsigned y, unsigned dir) {
  if (map >= 34) {
    error(s, h, "Invalid map", map);
    return;
  }
  s->map = map;
  s->default_song = ow_maps[map].song;
  s->map_patch[0] = 0;
  if (map == POKEMON_DOME_ENTRANCE)
    s->map_patch[1] = 0;
  s->x = x;
  s->y = y;
  s->direction = dir;
  s->next_script = 0;
  if (!load_npcs(s, h))
    return;
  play_default_song(s, h);
  if (ow_map_handlers[map][4])
    native_run(s, h, ow_map_handlers[map][4], NULL);
  changed(s, h);
  run_pending(s, h);
}
static void run_pending(OwState *s, const OwHost *h) {
  for (unsigned loops = 0; s->next_script && !s->error[0] && loops < 100;
       loops++) {
    uint16_t p = s->next_script;
    unsigned n = s->next_npc;
    s->next_script = 0;
    ow_run(s, h, p, n);
  }
  if (!s->error[0] && !s->duel_pending && !s->next_script)
    apply_warp(s, h);
}
void ow_new_game(OwState *s, const OwHost *h, uint32_t seed) {
  memset(s, 0, sizeof(*s));
  s->version = OW_SAVE_VERSION;
  s->rng = seed ? seed : 1;
  /* Func_c1b1 starts at the Dome before Script_BeginGame walks to Mason. */
  s->overworld_selection = OWMAP_POKEMON_DOME;
  ow_load_map(s, h, OVERWORLD_MAP, 12, 12, SOUTH);
}
void ow_resume(OwState *s, const OwHost *h) {
  changed(s, h);
  run_pending(s, h);
}
/* DetermineImakuniAndChallengeHall on a normal saved-game session start. */
void ow_continue(OwState *s, const OwHost *h) {
  if (s->duel_pending) {
    ow_resume(s, h);
    return;
  }
  s->events[63] = 0;
  unsigned room = 0;
  if (ow_event(s, EVENT_IMAKUNI_STATE) >= IMAKUNI_TALKED) {
    static const uint8_t rooms[] = {FIGHTING_CLUB_LOBBY, SCIENCE_CLUB_LOBBY,
                                    LIGHTNING_CLUB_LOBBY, WATER_CLUB_LOBBY};
    do {
      room = random_value(s, 256) & 3;
    } while (rooms[room] == s->map);
  }
  ow_set_event(s, EVENT_IMAKUNI_ROOM, room);
  if (s->overworld_selection != OWMAP_CHALLENGE_HALL) {
    if (ow_event(s, EVENT_RECEIVED_LEGENDARY_CARDS)) {
      ow_set_event(s, EVENT_CHALLENGE_CUP_3_STATE,
                   (random_value(s, 256) & 3) ? CHALLENGE_CUP_NOT_STARTED
                                              : CHALLENGE_CUP_READY_TO_START);
      ow_set_event(s, EVENT_CHALLENGE_CUP_2_STATE, CHALLENGE_CUP_OVER);
      ow_set_event(s, EVENT_CHALLENGE_CUP_1_STATE, CHALLENGE_CUP_OVER);
    } else {
      unsigned two = ow_event(s, EVENT_CHALLENGE_CUP_2_STATE),
               one = ow_event(s, EVENT_CHALLENGE_CUP_1_STATE);
      if (two != CHALLENGE_CUP_OVER) {
        if (two) {
          if (two != CHALLENGE_CUP_WON)
            ow_set_event(s, EVENT_CHALLENGE_CUP_2_STATE,
                         CHALLENGE_CUP_READY_TO_START);
          ow_set_event(s, EVENT_CHALLENGE_CUP_1_STATE, CHALLENGE_CUP_OVER);
        } else if (one && one != CHALLENGE_CUP_OVER && one != CHALLENGE_CUP_WON)
          ow_set_event(s, EVENT_CHALLENGE_CUP_1_STATE,
                       CHALLENGE_CUP_READY_TO_START);
      }
    }
  }
  unsigned map = s->map, x = s->x, y = s->y, d = s->direction;
  ow_load_map(s, h, map, x, y, d);
}
bool ow_world_select(OwState *s, unsigned direction) {
  if (s->map || direction > 3 || s->overworld_selection > 12)
    return false;
  unsigned target = ow_world_neighbors[s->overworld_selection][direction];
  if (!target)
    return false;
  s->overworld_selection = (uint8_t)target;
  return true;
}
unsigned ow_world_path(unsigned start, unsigned target, uint8_t (*points)[2],
                       unsigned capacity) {
  if (!start || start > 12 || !target || target > 12)
    return 0;
  const uint8_t *p = ow_world_paths[start - 1][target - 1];
  unsigned count = 0;
  for (unsigned i = 0; i < 64; i++, p += 2) {
    if (p[0] == 255 && p[1] == 255)
      break;
    unsigned x = p[0], y = p[1];
    if (!x && !y) {
      if (start == target)
        break;
      x = ow_location_positions[target][0];
      y = ow_location_positions[target][1];
    }
    if (count < capacity && points) {
      points[count][0] = (uint8_t)x;
      points[count][1] = (uint8_t)y;
    }
    count++;
  }
  return count;
}
static bool apply_warp(OwState *s, const OwHost *h) {
  for (const OwWarp *w = ow_map_warps[s->map]; w->x || w->y; w++)
    if (w->x == s->x && w->y == s->y) {
      unsigned map = w->map, nx = w->tx, ny = w->ty;
      if (!map) {
        s->map = 0;
        s->default_song = ow_maps[0].song;
        const uint8_t *p = ow_location_positions[s->overworld_selection];
        s->x = p[0] / 8;
        s->y = p[1] / 8;
        changed(s, h);
        play_default_song(s, h);
      } else
        ow_load_map(s, h, map, nx, ny, s->direction);
      return true;
    }
  return false;
}
bool ow_enter_location(OwState *s, const OwHost *h, unsigned location) {
  if (!location || location > 12)
    return false;
  s->overworld_selection = location;
  const uint8_t *w = ow_location_warps[location];
  ow_load_map(s, h, w[0], w[1], w[2], NORTH);
  return !s->error[0];
}
bool ow_move(OwState *s, const OwHost *h, unsigned dir) {
  if (s->error[0] || s->map == 0)
    return false;
  s->direction = dir & 3;
  uint8_t x = s->x, y = s->y;
  step_position(&x, &y, dir);
  const OwMap *m = &ow_maps[s->map];
  if (x >= m->width || y >= m->height)
    return false;
  if (permission(s, x, y) & 128)
    return false;
  for (unsigned i = 0; i < s->npc_count; i++)
    if (s->npcs[i].visible && s->npcs[i].x == x && s->npcs[i].y == y)
      return false;
  s->x = x;
  s->y = y;
  changed(s, h);
  if (ow_map_handlers[s->map][6])
    native_run(s, h, ow_map_handlers[s->map][6], NULL);
  run_pending(s, h);
  return true;
}
bool ow_interact(OwState *s, const OwHost *h) {
  uint8_t x = s->x, y = s->y;
  step_position(&x, &y, s->direction);
  for (unsigned i = 0; i < s->npc_count; i++) {
    OwNpc *n = &s->npcs[i];
    if (n->visible && !(s->npc_flags[i] & NPC_FLAG_UNKNOWN) && n->x == x &&
        n->y == y) {
      s->active_npc = n->id;
      s->dialog_npc = n->id;
      unsigned old_direction = n->direction, old_map = s->map, old_id = n->id;
      n->direction = (s->direction ^ 2) & 3;
      changed(s, h);
      ow_run(s, h, ow_npc_headers[n->id].script, n->id);
      run_pending(s, h);
      OwNpc *restored = npc(s, old_id);
      if (s->map == old_map && restored)
        restored->direction = (uint8_t)old_direction;
      changed(s, h);
      return true;
    }
  }
  uint16_t table = ow_map_handlers[s->map][2];
  if (table) { /* Object data itself lives in bank4, copied into generated
                  object tables. */
    extern const uint16_t ow_objects[][6];
    extern const unsigned ow_object_count;
    for (unsigned i = 0; i < ow_object_count; i++) {
      const uint16_t *o = ow_objects[i];
      if (o[0] == s->map && o[1] == s->direction && o[2] == x && o[3] == y) {
        uint16_t p = o[4];
        if (p == ow_script_address("PrintInteractableObjectText"))
          text(s, h, o[5], false);
        else if (p == ow_script_address("PCMenu"))
          feature(s, h, OW_FEATURE_PC, 0);
        else
          ow_run(s, h, p, 0);
        run_pending(s, h);
        return true;
      }
    }
  }
  if (ow_map_handlers[s->map][3])
    native_run(s, h, ow_map_handlers[s->map][3], NULL);
  run_pending(s, h);
  return false;
}

void ow_run(OwState *s, const OwHost *h, uint16_t pc, unsigned id) {
  s->active_npc = id;
  s->dialog_npc = id;
  s->script_pc = pc;
  if (pc == ow_script_address("Script_Clerk10"))
    return;
  OwDuel duel = {0};
  bool pending_duel = false;
  if (native_at(pc)) {
    native_run(s, h, pc, NULL);
    return;
  }
  if (byte(pc) == 0xe7)
    pc++;
  for (unsigned step = 0; step < 10000 && !s->error[0]; step++) {
    s->script_pc = pc;
    unsigned op = byte(pc), a = byte(pc + 1), b = byte(pc + 2),
             w = word(pc + 1), w2 = word(pc + 3);
    if (op >= 104) {
      error(s, h, "Invalid story opcode", pc);
      break;
    }
    unsigned length = ow_script_lengths[op];
    uint16_t next = pc + length;
    bool pass = false;
    OwNpc *n = npc(s, s->active_npc);
    switch (op) {
    case 0:
    case 0x35:
    case 0x53:
    case 0x54:
    case 0x55:
    case 0x56:
    case 0x57:
    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67:
      goto finished;
    case 1:
    case 0x0b:
      break;
    case 2:
    case 3:
      text(s, h, w, op == 2);
      break;
    case 4:
    case 0x4f: {
      int answer = choice(s, h, OW_CHOICE_YES_NO, w, 2, op == 0x4f ? 1 : 0);
      s->script_control = answer > 0 ? 0 : 1;
      if (answer > 0 && w2)
        next = w2;
      break;
    }
    case 5:
    case 0x2d:
      duel = (OwDuel){op == 5 ? s->active_npc : s->challenge_npc, a, b,
                      byte(pc + 3)};
      if (duel.deck == 255) {
        static const uint8_t decks[] = {LIGHTNING_AND_FIRE_DECK_ID,
                                        WATER_AND_FIGHTING_DECK_ID,
                                        GRASS_AND_PSYCHIC_DECK_ID};
        duel.deck = decks[s->native_vars[11] % 3];
      }
      if (op == 0x2d) {
        duel.deck = ow_npc_headers[duel.npc].deck;
        duel.music = ow_npc_headers[duel.npc].music;
      }
      s->duel_npc = duel.npc;
      s->pending_duel = duel;
      s->duel_pending = 1;
      pending_duel = true;
      break;
    case 6:
    case 7:
      text(s, h, s->script_control ? w : w2, op == 6);
      break;
    case 8:
      text(s, h, w, true);
      pc = next;
      goto finished;
    case 9:
      if (n)
        n->visible = 0;
      break;
    case 0x0a:
      movement(s, h, s->active_npc, word(w + (n ? n->direction : 0) * 2));
      break;
    case 0x0c:
      for (unsigned i = 0; i < 3; i++) {
        unsigned pack = byte(pc + 1 + i);
        if (pack != 255 && h && h->booster)
          h->booster(h->ctx, pack);
      }
      break;
    case 0x0d:
    case 0x0e:
      pass = count(h, a, op == 0x0d) > 0;
      s->script_control = pass ? 255 : 0;
      if (pass && word(pc + 2))
        next = word(pc + 2);
      break;
    case 0x0f:
      give(h, a ? a : s->received_card, 1);
      break;
    case 0x10:
      give(h, a, -1);
      break;
    case 0x11:
      for (unsigned c = GRASS_ENERGY; c <= DOUBLE_COLORLESS_ENERGY; c++)
        pass |= count(h, c, false) > 0;
      s->script_control = pass ? 255 : 0;
      if (pass && w)
        next = w;
      break;
    case 0x12:
      for (unsigned c = GRASS_ENERGY; c <= DOUBLE_COLORLESS_ENERGY; c++) {
        unsigned k = count(h, c, false);
        while (k--)
          give(h, c, -1);
      }
      break;
    case 0x13:
      pass = h && h->total_cards && h->total_cards(h->ctx) >= w;
      s->script_control = pass ? 255 : 0;
      if (pass && w2)
        next = w2;
      break;
    case 0x14: {
      unsigned defeated = 0;
      if (ow_event(s, EVENT_PUPIL_MICHAEL_STATE)) {
        defeated = 1;
        defeated += ow_event(s, EVENT_PUPIL_MICHAEL_STATE) >= PUPIL_DEFEATED;
        defeated += ow_event(s, EVENT_PUPIL_CHRIS_STATE) >= PUPIL_DEFEATED;
        defeated += ow_event(s, EVENT_PUPIL_JESSICA_STATE) >= PUPIL_DEFEATED;
      }
      next = word(pc + 1 + defeated * 2);
      break;
    }
    case 0x15:
      if (n)
        n->direction = a;
      break;
    case 0x16: {
      unsigned used = ow_event(s, EVENT_MAN1_GIFTED_CARD_FLAGS), k;
      if ((used & 31) == 31) {
        error(s, h, "All requested cards exhausted", pc);
        break;
      }
      do {
        k = random_value(s, 5);
      } while (used & (1u << k));
      s->requested_card = byte(ow_script_address("Man1RequestedCardsList") + k);
      ow_set_event(s, EVENT_MAN1_GIFTED_CARD_FLAGS, used | (1u << k));
      ow_set_event(s, EVENT_MAN1_REQUESTED_CARD_ID, s->requested_card);
      break;
    }
    case 0x17:
      if (a < 4)
        s->txram[a] =
            native_card_names[ow_event(s, EVENT_MAN1_REQUESTED_CARD_ID)];
      break;
    case 0x18:
    case 0x19:
      pass =
          count(h, ow_event(s, EVENT_MAN1_REQUESTED_CARD_ID), op == 0x18) > 0;
      s->script_control = pass ? 255 : 0;
      if (pass && w)
        next = w;
      break;
    case 0x1a:
      give(h, ow_event(s, EVENT_MAN1_REQUESTED_CARD_ID), -1);
      break;
    case 0x1b:
      next = w;
      break;
    case 0x1c:
      medal_packs(s, h);
      break;
    case 0x1d:
      s->direction = a;
      break;
    case 0x1e:
      s->direction = a;
      step_position(&s->x, &s->y, a);
      changed(s, h);
      break;
    case 0x1f:
      feature(s, h, OW_FEATURE_CARD_RECEIVED, a == 0 ? s->received_card : a);
      break;
    case 0x20:
      s->dialog_npc = a;
      break;
    case 0x21:
      s->next_npc = a;
      s->next_script = word(pc + 2);
      break;
    case 0x22:
      if (n) {
        unsigned index = (unsigned)(n - s->npcs);
        s->npc_flags[index] =
            (s->npc_flags[index] & ~NPC_FLAG_DIRECTIONLESS) | byte(pc + 3);
        s->npc_animation[index] = (uint8_t)b;
        changed(s, h);
      }
      break;
    case 0x23:
      if (n) {
        n->x = a;
        n->y = b;
      }
      break;
    case 0x24:
      if (h && h->wait)
        h->wait(h->ctx, a);
      break;
    case 0x25:
    case 0x26:
      pass = op == 0x25 ? n && n->x == a && n->y == b : s->x == a && s->y == b;
      s->script_control = pass ? 255 : 0;
      if (pass && w2)
        next = w2;
      break;
    case 0x27:
      movement(s, h, s->active_npc, w);
      break;
    case 0x28:
      if (h && h->booster) {
        h->booster(h->ctx, BOOSTER_COLOSSEUM_TRAINER);
        h->booster(h->ctx, BOOSTER_EVOLUTION_TRAINER);
        h->booster(h->ctx, BOOSTER_MYSTERY_TRAINER_COLORLESS);
        h->booster(h->ctx, BOOSTER_LABORATORY_TRAINER);
      }
      break;
    case 0x29:
      pass = npc(s, a) != NULL;
      s->script_control = pass ? 255 : 0;
      if (pass && word(pc + 2))
        next = word(pc + 2);
      break;
    case 0x2a:
      feature(s, h, OW_FEATURE_MEDAL, a);
      break;
    case 0x2b:
      if (a < 2)
        s->txram[a] = word(ow_script_address("MapNames") +
                           (s->overworld_selection - 1) * 2);
      break;
    case 0x2c:
      if (a < 4)
        s->txram[a] = ow_npc_headers[s->challenge_npc].name;
      break;
    case 0x2e: {
      unsigned cup = ow_event(s, EVENT_CHALLENGE_CUP_NUMBER);
      text(s, h, word(pc + 1 + ((cup - 1) & 3) * 2), true);
      break;
    }
    case 0x2f:
      movement(s, h, s->challenge_npc, w);
      break;
    case 0x30: {
      OwNpc *c = npc(s, s->challenge_npc);
      if (c)
        c->visible = 0;
      break;
    }
    case 0x31: {
      OwNpc *c = npc(s, s->challenge_npc);
      if (!c && s->npc_count < OW_NPC_LIMIT)
        c = &s->npcs[s->npc_count++];
      if (c)
        *c = (OwNpc){s->challenge_npc,
                     a,
                     b,
                     SOUTH,
                     1,
                     ow_npc_headers[s->challenge_npc].sprite};
      break;
    }
    case 0x32: {
      Reg r;
      ow_set_event(s, EVENT_CHALLENGE_CUP_OPPONENT_NUMBER,
                   ow_event(s, EVENT_CHALLENGE_CUP_OPPONENT_NUMBER) + 1);
      native_run(s, h, ow_script_address("Func_f580"), &r);
      break;
    }
    case 0x33:
      feature(s, h, OW_FEATURE_PAUSE, 0);
      break;
    case 0x34: {
      unsigned cup = ow_event(s, EVENT_CHALLENGE_CUP_NUMBER),
               k = cup < 3 ? cup - 1 : random_value(s, 13) + 2;
      uint16_t p = ow_script_address("ChallengeCupPrizeCards") + k * 3;
      s->received_card = byte(p);
      s->txram[0] = word(p + 1);
      break;
    }
    case 0x36:
      if (a < 11)
        s->map_patch[a] = 1;
      feature(s, h, OW_FEATURE_MAP_PATCH, a);
      break;
    case 0x37:
      s->native_vars[11] = (uint8_t)choice(s, h, OW_CHOICE_AARON, 0, 4, 0);
      ow_set_event(s, EVENT_AARON_DECK_MENU_CHOICE, s->native_vars[11]);
      break;
    case 0x38:
      feature(s, h, OW_FEATURE_DECK_MACHINE, a);
      break;
    case 0x39:
      s->starter = (uint8_t)choice(s, h, OW_CHOICE_STARTER, 0, 3, 0);
      /* ShowMultichoiceTextbox also puts the chosen entry in wTxRam2.
         The confirmation and starter reward both interpolate that text. */
      if (s->starter < 3)
        s->txram[0] = word(ow_script_address(
            "ScriptCommand_ChooseStarterDeckMultichoice.text_entries") +
            s->starter * 2);
      break;
    case 0x3a:
      ow_load_map(s, h, b, byte(pc + 3), byte(pc + 4), byte(pc + 5));
      break;
    case 0x3b:
      movement(s, h, a, word(pc + 2));
      break;
    case 0x3c: {
      unsigned flags = ow_event(s, EVENT_LEGENDARY_CARDS_RECEIVED_FLAGS), k;
      if (flags == 15) {
        error(s, h, "All legendary cards already selected", pc);
        break;
      }
      do {
        k = random_value(s, 256) & 3;
      } while (flags & (8u >> k));
      uint16_t p = ow_script_address("LegendaryCards") + k * 3;
      s->received_card = byte(p);
      s->txram[0] = word(p + 1);
      ow_set_event(s, byte(ow_script_address("LegendaryCardEvents") + k), 255);
      break;
    }
    case 0x3d:
      if (h && h->sound)
        h->sound(h->ctx, 3, a);
      break;
    case 0x3e:
      if (h && h->save) {
        if (a) {
          OwState saved = *s;
          saved.map = MASON_LABORATORY;
          saved.x = 2;
          saved.y = 4;
          saved.direction = SOUTH;
          saved.overworld_selection = OWMAP_MASON_LABORATORY;
          h->save(h->ctx, &saved);
        } else
          h->save(h->ctx, s);
      }
      break;
    case 0x3f:
      feature(s, h, OW_FEATURE_BATTLE_CENTER, 0);
      break;
    case 0x40:
      if (a)
        feature(s, h, OW_FEATURE_GIFT_CENTER,
                ow_event(s, EVENT_GIFT_CENTER_MENU_CHOICE));
      else {
        static const uint16_t action_text[] = {SendCardText, ReceiveCardText,
                                               SendDeckConfigurationText,
                                               ReceiveDeckConfigurationText};
        unsigned selected = choice(s, h, OW_CHOICE_GIFT, 0, 5, 4);
        ow_set_event(s, EVENT_GIFT_CENTER_MENU_CHOICE, selected);
        s->native_vars[7] = (uint8_t)selected;
        if (selected < 4)
          s->txram[0] = action_text[selected];
      }
      break;
    case 0x41:
      feature(s, h, OW_FEATURE_CREDITS, 0);
      break;
    case 0x42:
      feature(s, h, OW_FEATURE_PC_PACK, a);
      break;
    case 0x43:
      break;
    case 0x44:
      if (h && h->starter)
        h->starter(h->ctx, s->starter);
      break;
    case 0x45:
      s->overworld_selection = OWMAP_MASON_LABORATORY;
      changed(s, h);
      break;
    case 0x46:
      s->song_override = a;
      if (h && h->sound)
        h->sound(h->ctx, 0, a);
      break;
    case 0x47:
      s->default_song = a;
      break;
    case 0x48:
      if (h && h->sound)
        h->sound(h->ctx, 0, a);
      break;
    case 0x49:
      if (h && h->sound)
        h->sound(h->ctx, 1, a);
      break;
    case 0x4a:
    case 0x4b:
    case 0x4d:
      if (h && h->sound)
        h->sound(h->ctx, op, 0);
      break;
    case 0x4c:
      play_default_song(s, h);
      break;
    case 0x4e:
      for (unsigned i = 0; i < 10; i++) {
        if (s->masters[i] == a)
          break;
        if (!s->masters[i]) {
          s->masters[i] = a;
          break;
        }
      }
      break;
    case 0x50:
    case 0x51: {
      unsigned answer =
          choice(s, h, op == 0x50 ? OW_CHOICE_SAM : OW_CHOICE_SAM_RULES, 0,
                 op == 0x50 ? 4 : 8, 0);
      ow_set_event(s, EVENT_SAM_MENU_CHOICE, answer);
      break;
    }
    case 0x52:
      feature(s, h, OW_FEATURE_CHALLENGE_MACHINE, 0);
      break;
    case 0x58:
      ow_set_event(s, a, b);
      break;
    case 0x59:
    case 0x5a:
    case 0x61:
    case 0x62:
      pass = ow_event(s, a) != 0;
      if (op == 0x59 || op == 0x62)
        pass = !pass;
      s->script_control = pass ? 255 : 0;
      if (pass && word(pc + 2))
        next = word(pc + 2);
      break;
    case 0x5b:
    case 0x5c:
    case 0x5d:
    case 0x5e: {
      unsigned v = ow_event(s, a);
      pass = op == 0x5b   ? v == b
             : op == 0x5c ? v != b
             : op == 0x5d ? v >= b
                          : v < b;
      s->script_control = pass ? 255 : 0;
      if (pass && w2)
        next = w2;
      break;
    }
    case 0x5f:
      ow_set_event(s, a, 255);
      break;
    case 0x60:
      ow_set_event(s, a, 0);
      break;
    case 0x63:
      ow_set_event(s, a, ow_event(s, a) + 1);
      break;
    default:
      error(s, h, "Unimplemented story opcode", pc);
      break;
    }
    pc = next;
    continue;
  finished:
    s->script_pc = pc;
    if (pending_duel) {
      if (!h || !h->duel) {
        error(s, h, "Duel callback unavailable", pc);
        break;
      }
      s->script_pc = next;
      int result = h->duel(h->ctx, s, &duel);
      ow_finish_duel(s, h, result);
    }
    changed(s, h);
    return;
  }
  if (!s->error[0])
    error(s, h, "Script instruction limit", pc);
}

const char *ow_npc_name(unsigned id) {
  extern const char *const ow_npc_names[];
  return id < NUM_NPCS ? ow_npc_names[id] : "";
}
void ow_finish_duel(OwState *s, const OwHost *h, int result) {
  s->duel_pending = 0;
  s->last_duel_result = result == 1 ? 0 : 1;
  /* Original LoadMap reloads NPCs and starts the resolved room theme before
     Func_c141 dispatches the saved GAME_EVENT_DUEL after-duel handler. */
  s->next_script = 0;
  s->default_song = ow_maps[s->map].song;
  s->map_patch[0] = 0;
  if (s->map == POKEMON_DOME_ENTRANCE)
    s->map_patch[1] = 0;
  /* LoadMap sees GAME_EVENT_DUEL during NPC preloads; Func_c141 then clears
     it before dispatching the after-duel script. Amy relies on that order. */
  s->native_vars[0] = GAME_EVENT_DUEL;
  bool loaded_npcs = load_npcs(s, h);
  s->native_vars[0] = 0;
  if (!loaded_npcs)
    return;
  play_default_song(s, h);
  uint16_t after = ow_map_handlers[s->map][5];
  if (after)
    native_run(s, h, after, NULL);
  run_pending(s, h);
  changed(s, h);
}

/* Canonical byte serialization; all integers have explicit little-endian wire
   encoding. Save files are interchangeable between host and big-endian N64. */
#define BYTE_FIELDS(X)                                                         \
  X(events)                                                                    \
  X(map)                                                                       \
  X(x)                                                                         \
  X(y) X(direction) X(overworld_selection) X(npc_count) X(active_npc)          \
      X(dialog_npc) X(script_control) X(starter) X(received_card)              \
          X(challenge_npc) X(challenge_round) X(challenge_history) X(masters)  \
              X(requested_card) X(default_song) X(song_override) X(map_patch)  \
                  X(pc_packs) X(next_npc) X(duel_npc) X(last_duel_result)      \
                      X(pending_duel) X(duel_pending) X(npcs) X(npc_flags)     \
                          X(npc_animation) X(native_vars)
size_t ow_save_size(void) {
  size_t n = 4 + 8 + 2 + 2 + 8 + 32;
#define COUNT_FIELD(f) n += sizeof(((OwState *)0)->f);
  BYTE_FIELDS(COUNT_FIELD)
#undef COUNT_FIELD
  return n;
}
static void put16(uint8_t **p, unsigned v) {
  *(*p)++ = (uint8_t)v;
  *(*p)++ = (uint8_t)(v >> 8);
}
static unsigned get16(const uint8_t **p) {
  unsigned v = (*p)[0] | (unsigned)(*p)[1] << 8;
  *p += 2;
  return v;
}
bool ow_save_encode(const OwState *s, uint8_t *out, size_t size) {
  if (size < ow_save_size())
    return false;
  uint8_t *p = out;
  memcpy(p, "OW01", 4);
  p += 4;
  put16(&p, s->version);
  put16(&p, s->version >> 16);
  put16(&p, s->rng);
  put16(&p, s->rng >> 16);
#define COPY_FIELD(f)                                                          \
  memcpy(p, &s->f, sizeof(s->f));                                              \
  p += sizeof(s->f);
  BYTE_FIELDS(COPY_FIELD)
#undef COPY_FIELD
  put16(&p, s->next_script);
  put16(&p, s->script_pc);
  for (unsigned i = 0; i < 4; i++)
    put16(&p, s->txram[i]);
  for (unsigned i = 0; i < 16; i++)
    put16(&p, s->native_words[i]);
  return (size_t)(p - out) == ow_save_size();
}
bool ow_save_decode(OwState *s, const uint8_t *in, size_t size) {
  if (size != ow_save_size() || memcmp(in, "OW01", 4))
    return false;
  const uint8_t *p = in + 4;
  OwState temp;
  memset(&temp, 0, sizeof(temp));
  temp.version = get16(&p);
  temp.version |= (uint32_t)get16(&p) << 16;
  temp.rng = get16(&p);
  temp.rng |= (uint32_t)get16(&p) << 16;
#define LOAD_FIELD(f)                                                          \
  memcpy(&temp.f, p, sizeof(temp.f));                                          \
  p += sizeof(temp.f);
  BYTE_FIELDS(LOAD_FIELD)
#undef LOAD_FIELD
  temp.next_script = get16(&p);
  temp.script_pc = get16(&p);
  for (unsigned i = 0; i < 4; i++)
    temp.txram[i] = get16(&p);
  for (unsigned i = 0; i < 16; i++)
    temp.native_words[i] = get16(&p);
  if (temp.version != OW_SAVE_VERSION || temp.map >= 34 ||
      temp.npc_count > OW_NPC_LIMIT || temp.direction > 3 ||
      temp.overworld_selection > 12)
    return false;
  *s = temp;
  return true;
}
