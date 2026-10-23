#include "mmx_dev_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes_game_overlay.h"
#include "snes_overlay_draw.h"
#include "snes_osd.h"
#include "mmx_boss_rush.h"
#include "desktop/sdl_compat.h"

/* RAM used here (USA Rev 1):
 *   $D1/$D2/$D3   game mode / stage phase / stage sub-phase (2/4/4 = playing)
 *   $1F7A         stage (0 intro, 1 Octopus, 2 Chameleon, 3 Armadillo,
 *                 4 Mammoth, 5 Eagle, 6 Mandrill, 7 Kuwanger, 8 Penguin,
 *                 9..12 Sigma); $1F81 checkpoint, cleared by stage init
 *   $1F88+2i      weapon of stage i+1: bit 6 owned (= Maverick defeated),
 *                 bits 0..4 energy; $DC is owned and full
 *   $1F83+n       Sub Tank n: bit 7 owned, bits 0..3 energy ($8E full)
 *   $1F99         bits 0..3 armor (legs, arms, helmet, body), bits 4..7
 *                 Sub Tanks collected
 *   $1F9C         Heart Tanks collected; $1F9A maximum health (16..32)
 *   $0BCF         health (bits 0..6); $0BDB equipped weapon * 2 */

enum { W = 512, H = 448, ROW_Y = 64, ROW_H = 22, ROWS_X = 28 };
enum { PAGE_GAME, PAGE_ITEMS, PAGE_COUNT };

static const struct { const char *name; uint8_t stage; } kStages[] = {
  {"INTRO STAGE", 0}, {"CHILL PENGUIN", 8}, {"SPARK MANDRILL", 6},
  {"ARMORED ARMADILLO", 3}, {"LAUNCH OCTOPUS", 1}, {"BOOMER KUWANGER", 7},
  {"STING CHAMELEON", 2}, {"STORM EAGLE", 5}, {"FLAME MAMMOTH", 4},
  {"SIGMA STAGE 1", 9}, {"SIGMA STAGE 2", 10}, {"SIGMA STAGE 3", 11},
  {"SIGMA STAGE 4", 12},
};
enum { STAGE_COUNT = sizeof(kStages) / sizeof(kStages[0]) };
/* Indexed by stage - 1, the weapon's own slot. */
static const char *const kWeapons[8] = {
  "HOMING TORPEDO", "CHAMELEON STING", "ROLLING SHIELD", "FIRE WAVE",
  "STORM TORNADO", "ELECTRIC SPARK", "BOOMERANG CUTTER", "SHOTGUN ICE",
};
/* Menu order: the stage-select order, as the warp list. */
static const uint8_t kWeaponOrder[8] = {7, 5, 2, 0, 6, 1, 4, 3};
static const char *const kArmor[4] = {"LEG PARTS", "ARM PARTS", "HELMET", "BODY PARTS"};

enum {
  G_RESUME, G_FREEZE, G_STAGE, G_CHECKPOINT, G_WARP, G_REFILL, G_INFINITE,
  G_BOSSES, G_ITEMS, G_COUNT
};
enum {
  I_WEAPON0, I_REFILL_WEAPONS = 8, I_TANKS, I_HEARTS, I_ARMOR0, I_COUNT = I_ARMOR0 + 4
};

static uint8_t *s_ram;
static bool s_open, s_frozen_view;
static int s_page, s_row[PAGE_COUNT];
static int s_stage = 1, s_checkpoint;
static bool s_infinite, s_respawn_bosses, s_respawn_items;
static char s_status[64];
static uint32_t s_panel[W * H];
static uint32_t s_prev_pad, s_held_dir, s_held_since, s_last_repeat;

/* Warp in progress: stage init must see the target stage and checkpoint. */
static int s_warp_stage = -1, s_warp_checkpoint;
/* $1F80..$1F9F: lives, tanks, weapons, armor and hearts. The new-game
 * initialization the warp passes through clears some of them. */
static uint8_t s_warp_inventory[0x20];

/* Respawn options: flags hidden from the guest while a stage is played. */
static struct {
  bool active, weapon_hidden, items_hidden;
  uint8_t stage, weapon, hearts, tank_bits, max_hp;
  uint8_t tanks[4], tank_bits_prev;
} s_hide;

static void status(const char *text) { snprintf(s_status, sizeof(s_status), "%s", text); }

static bool playing(const uint8_t *r) {
  return r[0xd1] == 2 && r[0xd2] == 4 && r[0xd3] == 4 && r[0x1f7a] <= 12;
}
static bool in_stage(const uint8_t *r) {
  return r[0xd1] == 2 && r[0xd2] == 4 && r[0xd3] <= 6 && !(r[0xd3] & 1) &&
      r[0x1f7a] <= 12 && !MmxBossRushActive();
}
static unsigned bits(unsigned v) { unsigned n = 0; for (; v; v &= v - 1) ++n; return n; }

/* Put every hidden flag back. Inventory edits and warps start from here. */
static void reveal(uint8_t *r) {
  if (!s_hide.active) return;
  if (s_hide.weapon_hidden) {
    unsigned w = 0x1f88 + 2 * (s_hide.stage - 1);
    /* Defeated again: keep the fresh weapon. */
    if (!(r[w] & 0x40)) r[w] = s_hide.weapon;
  }
  if (s_hide.items_hidden) {
    r[0x1f9c] |= s_hide.hearts;
    r[0x1f99] |= s_hide.tank_bits;
  }
  memset(&s_hide, 0, sizeof(s_hide));
}
static void hide(uint8_t *r) {
  memset(&s_hide, 0, sizeof(s_hide));
  s_hide.active = true;
  s_hide.stage = r[0x1f7a];
  if (s_respawn_bosses && s_hide.stage >= 1 && s_hide.stage <= 8) {
    unsigned w = 0x1f88 + 2 * (s_hide.stage - 1);
    if ((r[w] & 0x40) && r[0xbdb] / 2 != s_hide.stage) {
      s_hide.weapon = r[w]; r[w] = 0; s_hide.weapon_hidden = true;
    }
  }
  if (s_respawn_items) {
    s_hide.items_hidden = true;
    s_hide.hearts = r[0x1f9c]; s_hide.tank_bits = r[0x1f99] & 0xf0;
    s_hide.max_hp = r[0x1f9a];
    r[0x1f9c] = 0; r[0x1f99] &= 0x0f;
    memcpy(s_hide.tanks, r + 0x1f83, 4);
  }
}
static void items_upkeep(uint8_t *r) {
  /* A repeated Heart Tank adds nothing; a new one adds its two points. */
  unsigned max = s_hide.max_hp + 2 * bits(r[0x1f9c] & ~s_hide.hearts);
  if (max > 32) max = 32;
  if (r[0x1f9a] > max) {
    r[0x1f9a] = (uint8_t)max;
    if ((r[0xbcf] & 0x7f) > max) r[0xbcf] = (uint8_t)((r[0xbcf] & 0x80) | max);
  }
  /* A repeated Sub Tank keeps its energy instead of arriving empty. */
  uint8_t collected = (uint8_t)(r[0x1f99] & 0xf0 & ~s_hide.tank_bits_prev);
  for (unsigned n = 0; n < 4 && collected; ++n)
    if (r[0x1f83 + n] == 0x80 && s_hide.tanks[n] != 0x80 && (s_hide.tanks[n] & 0x80))
      r[0x1f83 + n] = s_hide.tanks[n];
  memcpy(s_hide.tanks, r + 0x1f83, 4);
  s_hide.tank_bits_prev = r[0x1f99] & 0xf0;
}

static void test_step(unsigned frame);
void MmxDevMenuFrame(uint8_t *r) {
  if (!r) return;
  s_ram = r;
  { static unsigned frame; test_step(++frame); }
  if (s_warp_stage >= 0) {
    /* Game start resets $1F7A to the intro until its stage intro runs. */
    /* Phase 2 step 0 is the frame before the stage intro reads $1F7A; the
     * intro then borrows it for its own scenes. */
    if (r[0xd2] < 2 || (r[0xd2] == 2 && r[0xd3] == 0)) r[0x1f7a] = (uint8_t)s_warp_stage;
    if (r[0xd2] == 2 && r[0xd3] == 0) {
      uint8_t checkpoint = r[0x1f81];
      memcpy(r + 0x1f80, s_warp_inventory, sizeof(s_warp_inventory));
      r[0x1f81] = checkpoint;
    }
    if (r[0xd2] == 4 && r[0xd3] == 0) r[0x1f81] = (uint8_t)s_warp_checkpoint;
    if (r[0xd2] == 4 && r[0xd3] != 0) s_warp_stage = -1;
  }
  bool stage = in_stage(r);
  if (s_hide.active && (!stage || r[0x1f7a] != s_hide.stage)) reveal(r);
  if (!s_hide.active && stage && (s_respawn_bosses || s_respawn_items)) hide(r);
  if (s_hide.active && s_hide.items_hidden) items_upkeep(r);
  if (s_infinite && playing(r)) {
    unsigned hp = r[0xbcf] & 0x7f, max = r[0x1f9a];
    if (hp && max >= 16 && max <= 32 && hp < max) r[0xbcf] = (uint8_t)((r[0xbcf] & 0x80) | max);
  }
}
void MmxDevMenuStateLoaded(void) { memset(&s_hide, 0, sizeof(s_hide)); s_warp_stage = -1; }

/* ---- actions ---------------------------------------------------------- */

static bool require_game(void) {
  if (!s_ram || s_ram[0xd1] != 2 || s_ram[0xd2] != 4 || s_ram[0x1f7a] > 12) {
    status("START A STAGE FIRST"); return false;
  }
  if (MmxBossRushActive()) { status("NOT AVAILABLE IN BOSS RUSH"); return false; }
  return true;
}
static void warp(void) {
  if (!require_game()) return;
  if (s_ram[0xd3] != 4 && s_ram[0xd3] != 6) { status("WAIT UNTIL X CAN MOVE"); return; }
  reveal(s_ram);
  s_warp_stage = kStages[s_stage].stage; s_warp_checkpoint = s_checkpoint;
  memcpy(s_warp_inventory, s_ram + 0x1f80, sizeof(s_warp_inventory));
  s_ram[0x1f7a] = (uint8_t)s_warp_stage;
  /* The stage-select hand-off: phase 0 reloads everything for $1F7A. */
  s_ram[0xd2] = 0; s_ram[0xd3] = 0;
  s_open = false;
  snes_osd_push("Warping", 1500);
}
static void refill_health(void) {
  if (!require_game()) return;
  unsigned max = s_ram[0x1f9a];
  if (max < 16 || max > 32) return;
  if (s_ram[0xbcf] & 0x7f) s_ram[0xbcf] = (uint8_t)((s_ram[0xbcf] & 0x80) | max);
  for (unsigned i = 0; i < 8; ++i)
    if (s_ram[0x1f88 + 2 * i] & 0x40) s_ram[0x1f88 + 2 * i] = (s_ram[0x1f88 + 2 * i] & 0xe0) | 0x1c;
  for (unsigned n = 0; n < 4; ++n) if (s_ram[0x1f83 + n] & 0x80) s_ram[0x1f83 + n] = 0x8e;
  status("HEALTH, WEAPONS AND TANKS REFILLED");
}
static void toggle_weapon(unsigned slot) {
  if (!require_game()) return;
  reveal(s_ram);
  unsigned a = 0x1f88 + 2 * slot;
  if (s_ram[a] & 0x40) {
    if (s_ram[0xbdb] / 2 == slot + 1) { status("SWITCH TO THE X-BUSTER FIRST"); return; }
    s_ram[a] = 0;
  } else {
    s_ram[a] = 0xdc;
  }
}
static unsigned tank_count(void) {
  unsigned n = 0;
  for (unsigned i = 0; i < 4; ++i) n += (s_ram[0x1f83 + i] & 0x80) != 0;
  return n;
}
static void set_tanks(unsigned count) {
  reveal(s_ram);
  for (unsigned i = 0; i < 4; ++i) {
    bool own = i < count;
    s_ram[0x1f83 + i] = own ? 0x8e : 0;
    s_ram[0x1f99] = (uint8_t)(own ? s_ram[0x1f99] | (0x10 << i) : s_ram[0x1f99] & ~(0x10 << i));
  }
}
static void set_hearts(unsigned count) {
  reveal(s_ram);
  s_ram[0x1f9c] = (uint8_t)((1u << count) - 1);
  s_ram[0x1f9a] = (uint8_t)(16 + 2 * count);
  if ((s_ram[0xbcf] & 0x7f) > s_ram[0x1f9a])
    s_ram[0xbcf] = (uint8_t)((s_ram[0xbcf] & 0x80) | s_ram[0x1f9a]);
}
static void toggle_armor(unsigned part) {
  reveal(s_ram);
  s_ram[0x1f99] ^= (uint8_t)(1u << part);
}

/* Left/right (dir = -1/+1) or confirm (dir = 0) on the current row. */
static void activate(int dir) {
  int row = s_row[s_page];
  s_status[0] = 0;
  if (s_page == PAGE_GAME) {
    switch (row) {
    case G_RESUME: if (!dir) s_open = false; break;
    case G_FREEZE: if (!dir) s_frozen_view = true; break;
    case G_STAGE: s_stage = (s_stage + (dir ? dir : 1) + STAGE_COUNT) % STAGE_COUNT; break;
    case G_CHECKPOINT: s_checkpoint = (s_checkpoint + (dir ? dir : 1) + 4) % 4; break;
    case G_WARP: if (!dir) warp(); break;
    case G_REFILL: if (!dir) refill_health(); break;
    case G_INFINITE: s_infinite = !s_infinite; break;
    case G_BOSSES: s_respawn_bosses = !s_respawn_bosses; if (s_ram) reveal(s_ram); break;
    case G_ITEMS: s_respawn_items = !s_respawn_items; if (s_ram) reveal(s_ram); break;
    }
    return;
  }
  if (!require_game()) return;
  if (row < I_REFILL_WEAPONS) toggle_weapon(kWeaponOrder[row]);
  else if (row == I_REFILL_WEAPONS) { if (!dir) refill_health(); }
  else if (row == I_TANKS) { reveal(s_ram); set_tanks((tank_count() + (dir < 0 ? 4 : 1)) % 5); }
  else if (row == I_HEARTS) {
    reveal(s_ram);
    set_hearts((bits(s_ram[0x1f9c]) + (dir < 0 ? 8 : 1)) % 9);
  } else toggle_armor((unsigned)(row - I_ARMOR0));
}

/* ---- drawing ------------------------------------------------------------ */

static void text(int x, int y, const char *s, uint32_t col, int scale) {
  snes_ovl_draw_text(s_panel, W, H, x, y, s, col, scale);
}
static void row_text(int row, const char *label, const char *value, bool selected) {
  int y = ROW_Y + row * ROW_H;
  if (selected) snes_ovl_fill_rect(s_panel, W, H, ROWS_X - 8, y - 3, W - 2 * ROWS_X + 16, ROW_H, 0xFF2A3550u);
  uint32_t fg = selected ? 0xFFFFFFFFu : 0xFFB8BDC8u;
  text(ROWS_X, y, label, fg, 2);
  if (value) text(W - ROWS_X - (int)strlen(value) * 16, y, value, selected ? 0xFFFFD24Du : 0xFF8FD18Fu, 2);
}
static const char *onoff(bool v) { return v ? "ON" : "OFF"; }
static void draw(void) {
  for (int i = 0; i < W * H; ++i) s_panel[i] = 0xF0000000u;
  snes_ovl_stroke_rect(s_panel, W, H, 4, 4, W - 8, H - 8, 0xFF5F6B85u);
  text(ROWS_X, 18, "DEVELOPER MENU", 0xFFFFD24Du, 2);
  text(W - ROWS_X - 9 * 8, 18, s_page == PAGE_GAME ? "GAME  1/2" : "ITEMS 2/2", 0xFF7F8796u, 1);
  text(W - ROWS_X - 13 * 8, 32, "L OR Y: PAGE", 0xFF7F8796u, 1);
  int sel = s_row[s_page];
  char buf[32];
  if (s_page == PAGE_GAME) {
    row_text(G_RESUME, "RESUME", NULL, sel == G_RESUME);
    row_text(G_FREEZE, "FREEZE (HIDE MENU)", NULL, sel == G_FREEZE);
    row_text(G_STAGE, "STAGE", kStages[s_stage].name, sel == G_STAGE);
    snprintf(buf, sizeof(buf), "%d", s_checkpoint);
    row_text(G_CHECKPOINT, "CHECKPOINT", buf, sel == G_CHECKPOINT);
    row_text(G_WARP, "WARP NOW", NULL, sel == G_WARP);
    row_text(G_REFILL, "REFILL EVERYTHING", NULL, sel == G_REFILL);
    row_text(G_INFINITE, "INFINITE HEALTH", onoff(s_infinite), sel == G_INFINITE);
    row_text(G_BOSSES, "MAVERICKS RESPAWN", onoff(s_respawn_bosses), sel == G_BOSSES);
    row_text(G_ITEMS, "ITEMS RESPAWN", onoff(s_respawn_items), sel == G_ITEMS);
    const char *help[] = {
      "", "THE GAME STAYS PAUSED. ANY BUTTON SHOWS THE MENU AGAIN.",
      "", "0 IS THE STAGE START. HIGHER NUMBERS ARE LATER IN THE STAGE.",
      "", "HEALTH, WEAPON ENERGY AND SUB TANKS.",
      "PITS AND SPIKES STILL DEFEAT X.",
      "A DEFEATED STAGE BOSS FIGHTS AGAIN. ITS WEAPON IS",
      "HEART AND SUB TANKS REAPPEAR. COLLECTING ONE AGAIN",
    };
    const char *help2[] = {
      "", "", "", "", "", "", "",
      "UNAVAILABLE INSIDE ITS OWN STAGE UNTIL YOU LEAVE.",
      "CHANGES NOTHING. ARMOR CAPSULES DO NOT REAPPEAR.",
    };
    text(ROWS_X, ROW_Y + G_COUNT * ROW_H + 10, help[sel], 0xFF7F8796u, 1);
    text(ROWS_X, ROW_Y + G_COUNT * ROW_H + 22, help2[sel], 0xFF7F8796u, 1);
  } else {
    for (int i = 0; i < 8; ++i) {
      unsigned slot = kWeaponOrder[i];
      bool own = s_ram && (s_ram[0x1f88 + 2 * slot] & 0x40);
      row_text(I_WEAPON0 + i, kWeapons[slot], onoff(own), sel == I_WEAPON0 + i);
    }
    row_text(I_REFILL_WEAPONS, "REFILL EVERYTHING", NULL, sel == I_REFILL_WEAPONS);
    snprintf(buf, sizeof(buf), "%u / 4", s_ram ? tank_count() : 0);
    row_text(I_TANKS, "SUB TANKS", buf, sel == I_TANKS);
    snprintf(buf, sizeof(buf), "%u / 8", s_ram ? bits(s_ram[0x1f9c] | (s_hide.items_hidden ? s_hide.hearts : 0)) : 0);
    row_text(I_HEARTS, "HEART TANKS", buf, sel == I_HEARTS);
    for (int i = 0; i < 4; ++i)
      row_text(I_ARMOR0 + i, kArmor[i], onoff(s_ram && (s_ram[0x1f99] & (1u << i))), sel == I_ARMOR0 + i);
  }
  text(ROWS_X, H - 40, "A: SELECT   LEFT/RIGHT: CHANGE   B: RESUME", 0xFFE2E5EBu, 1);
  if (s_status[0]) text(ROWS_X, H - 24, s_status, 0xFFFFD24Du, 1);
  else text(ROWS_X, H - 24, "WEAPONS ON = MAVERICK DEFEATED", 0xFF7F8796u, 1);
}

/* ---- overlay callbacks --------------------------------------------------- */

static int ov_open(void) {
  if (!s_ram) return 0;
  s_open = true; s_frozen_view = false; s_status[0] = 0;
  s_prev_pad = ~0u; /* the gesture's buttons are still held */
  return 1;
}
static int ov_is_open(void) { return s_open; }
static void ov_close(void) { s_open = false; }
static int rows(void) { return s_page == PAGE_GAME ? G_COUNT : I_COUNT; }
static void move(int d) { s_row[s_page] = (s_row[s_page] + d + rows()) % rows(); }
static void ov_poll_nav(uint32_t pad, uint32_t ticks) {
  uint32_t down = pad & ~s_prev_pad;
  s_prev_pad = pad;
  if (s_frozen_view) {
    if (down & SNES_PAD_B) s_open = false;
    else if (down) s_frozen_view = false;
    return;
  }
  if (down & (SNES_PAD_B | SNES_PAD_START)) { s_open = false; return; }
  if (down & (SNES_PAD_L | SNES_PAD_Y)) { s_page = (s_page + 1) % PAGE_COUNT; s_status[0] = 0; }
  if (down & (SNES_PAD_A | SNES_PAD_R)) activate(0);
  uint32_t dir = pad & (SNES_PAD_UP | SNES_PAD_DOWN | SNES_PAD_LEFT | SNES_PAD_RIGHT);
  bool fire = false;
  if (dir && dir != s_held_dir) { s_held_dir = dir; s_held_since = s_last_repeat = ticks; fire = true; }
  else if (dir && ticks - s_held_since >= SNES_OVL_REPEAT_DELAY &&
           ticks - s_last_repeat >= SNES_OVL_REPEAT_RATE) { s_last_repeat = ticks; fire = true; }
  else if (!dir) s_held_dir = 0;
  if (!fire) return;
  if (dir & SNES_PAD_UP) move(-1);
  else if (dir & SNES_PAD_DOWN) move(1);
  else if (dir & SNES_PAD_LEFT) activate(-1);
  else if (dir & SNES_PAD_RIGHT) activate(1);
}
static void ov_handle_key(int key, int repeat) {
  if (s_frozen_view) { if (!repeat) s_frozen_view = false; return; }
  switch (key) {
  case SDLK_UP: move(-1); break;
  case SDLK_DOWN: move(1); break;
  case SDLK_LEFT: activate(-1); break;
  case SDLK_RIGHT: activate(1); break;
  case SDLK_RETURN: case SDLK_SPACE: if (!repeat) activate(0); break;
  case SDLK_TAB: if (!repeat) s_page = (s_page + 1) % PAGE_COUNT; break;
  case SDLK_ESCAPE: case SDLK_BACKSPACE: if (!repeat) s_open = false; break;
  default: break;
  }
}
static int ov_image(const uint32_t **px, int *w, int *h) {
  if (!s_open || s_frozen_view) return 0;
  draw();
  *px = s_panel; *w = W; *h = H;
  return 1;
}

static const SnesGameOverlay kOverlay = {
  .gesture = "Select+X",
  .open = ov_open, .is_open = ov_is_open, .close = ov_close,
  .poll_nav = ov_poll_nav, .handle_key = ov_handle_key, .overlay_image = ov_image,
};
void MmxDevMenuRegister(void) { snes_game_overlay_register(&kOverlay); }

/* MMX_DEV_MENU_TEST="<frame>:<keys>;..." drives the menu without a pad, for
 * headless checks: keys are U D L R (directions), A, B, P (page), O (open).
 * MMX_DEV_MENU_DUMP=<dir> writes the panel after each step as a PPM. */
static void test_dump(unsigned frame, unsigned step) {
  const char *dir = getenv("MMX_DEV_MENU_DUMP");
  const uint32_t *px; int w, h;
  if (!dir || !ov_image(&px, &w, &h)) return;
  char path[512]; snprintf(path, sizeof(path), "%s/menu_%06u_%u.ppm", dir, frame, step);
  FILE *f = fopen(path, "wb"); if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int i = 0; i < w * h; ++i) { fputc((px[i] >> 16) & 255, f); fputc((px[i] >> 8) & 255, f); fputc(px[i] & 255, f); }
  fclose(f);
}
static void test_step(unsigned frame) {
  const char *spec = getenv("MMX_DEV_MENU_TEST");
  if (!spec) return;
  for (const char *p = spec; *p; ) {
    unsigned at = (unsigned)strtoul(p, (char **)&p, 10);
    if (*p != ':') return;
    ++p;
    const char *keys = p;
    while (*p && *p != ';') ++p;
    if (at == frame) {
      unsigned step = 0;
      for (const char *k = keys; k < p; ++k) {
        static const struct { char c; uint32_t pad; } map[] = {
          {'U', SNES_PAD_UP}, {'D', SNES_PAD_DOWN}, {'L', SNES_PAD_LEFT}, {'R', SNES_PAD_RIGHT},
          {'A', SNES_PAD_A}, {'B', SNES_PAD_B}, {'P', SNES_PAD_L},
        };
        if (*k == 'O') { ov_open(); s_prev_pad = 0; }
        for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); ++i) if (map[i].c == *k) {
          s_held_dir = 0; ov_poll_nav(map[i].pad, 0); ov_poll_nav(0, 1);
        }
        test_dump(frame, step++);
      }
    }
    if (*p == ';') ++p;
  }
}
