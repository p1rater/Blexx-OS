/*
 * fb.c – Framebuffer TTY driver + FAT32 filesystem
 * BNU/Blex kernel – Copyright (C) 2026 Blex – BOSL License
 *
 * TTY mode: 1920×1080 framebuffer used as a single full-screen
 * terminal (no window manager).  Characters are drawn with the
 * built-in 8×16 bitmap font; the screen scrolls line-by-line
 * when the cursor reaches the bottom.
 *
 * FAT32: 512 KB in-memory ramdisk, unchanged from original.
 */

#include "fb.h"
#include "kernel_utils.h"
#include <stdint.h>

/* ══════════════════════════════════════════════════════
 *  MULTIBOOT2 CONSTANTS
 * ══════════════════════════════════════════════════════ */
#define MB2_MAGIC_RESPONSE  0x36D76289
#define MB2_TAG_FRAMEBUFFER 8

typedef struct { uint32_t type; uint32_t size; } mb2_tag_t;
typedef struct {
    uint32_t type; uint32_t size;
    uint64_t addr;  uint32_t pitch;
    uint32_t width; uint32_t height;
    uint8_t  bpp;   uint8_t  fb_type;
    uint16_t reserved;
} mb2_tag_fb_t;

/* ══════════════════════════════════════════════════════
 *  GLOBALS
 * ══════════════════════════════════════════════════════ */
fb_info_t fb;
int       focused_win = 0;   /* TTY has no windows; kept for compat */

/* Legacy VFS shim */
extern vfs_node_t ram_disk[32];

/* FAT32 ramdisk */
uint8_t fat32_disk[FAT32_DISK_SIZE];

/* ── TTY cursor state ─────────────────────────────── */
static int tty_cx = 0;   /* column (chars) */
static int tty_cy = 0;   /* row    (chars) */
static uint32_t tty_fg  = COL_TEXT_FG;
static uint32_t tty_bg  = COL_SHELL_BG;

/* Derived at fb_init time */
static int tty_cols = 0;
static int tty_rows = 0;

/* ══════════════════════════════════════════════════════
 *  STANDARD 8×16 BITMAP FONT (VGA-style, ASCII 32–127)
 * ══════════════════════════════════════════════════════ */
static const uint8_t font8x16[96][16] = {
    /* 0x20  space */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 0x21  !     */ {0,0,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0,0x18,0x18,0,0,0,0},
    /* 0x22  "     */ {0,0,0x66,0x66,0x66,0x24,0,0,0,0,0,0,0,0,0,0},
    /* 0x23  #     */ {0,0,0x6C,0x6C,0xFE,0x6C,0x6C,0x6C,0xFE,0x6C,0x6C,0,0,0,0,0},
    /* 0x24  $     */ {0x18,0x18,0x7E,0xC3,0xC2,0xC0,0x7E,0x03,0x83,0xC3,0x7E,0x18,0x18,0,0,0},
    /* 0x25  %     */ {0,0,0,0xC6,0xCC,0x18,0x30,0x60,0xC6,0x86,0x0C,0,0,0,0,0},
    /* 0x26  &     */ {0,0,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0x76,0,0,0,0,0},
    /* 0x27  '     */ {0,0,0x18,0x18,0x18,0x30,0,0,0,0,0,0,0,0,0,0},
    /* 0x28  (     */ {0,0,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0,0,0,0,0},
    /* 0x29  )     */ {0,0,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0,0,0,0,0},
    /* 0x2A  *     */ {0,0,0,0x66,0x3C,0xFF,0x3C,0x66,0,0,0,0,0,0,0,0},
    /* 0x2B  +     */ {0,0,0,0x18,0x18,0x7E,0x18,0x18,0,0,0,0,0,0,0,0},
    /* 0x2C  ,     */ {0,0,0,0,0,0,0,0,0,0x18,0x18,0x18,0x30,0,0,0},
    /* 0x2D  -     */ {0,0,0,0,0,0x7E,0,0,0,0,0,0,0,0,0,0},
    /* 0x2E  .     */ {0,0,0,0,0,0,0,0,0,0x18,0x18,0,0,0,0,0},
    /* 0x2F  /     */ {0,0,0x03,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0,0,0,0,0,0},
    /* 0x30  0     */ {0,0,0x7E,0xC3,0xC3,0xDB,0xDB,0xDB,0xC3,0xC3,0x7E,0,0,0,0,0},
    /* 0x31  1     */ {0,0,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x7E,0,0,0,0,0},
    /* 0x32  2     */ {0,0,0x7E,0xC3,0x03,0x06,0x0C,0x18,0x30,0x60,0xFF,0,0,0,0,0},
    /* 0x33  3     */ {0,0,0x7E,0xC3,0x03,0x1E,0x03,0x03,0x03,0xC3,0x7E,0,0,0,0,0},
    /* 0x34  4     */ {0,0,0x06,0x0E,0x1E,0x36,0x66,0xC6,0xFF,0x06,0x06,0,0,0,0,0},
    /* 0x35  5     */ {0,0,0xFF,0xC0,0xC0,0xFE,0x03,0x03,0x03,0xC3,0x7E,0,0,0,0,0},
    /* 0x36  6     */ {0,0,0x3E,0x60,0xC0,0xFE,0xC3,0xC3,0xC3,0x63,0x3E,0,0,0,0,0},
    /* 0x37  7     */ {0,0,0xFF,0x03,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0,0,0,0,0},
    /* 0x38  8     */ {0,0,0x7E,0xC3,0xC3,0x7E,0xC3,0xC3,0xC3,0xC3,0x7E,0,0,0,0,0},
    /* 0x39  9     */ {0,0,0x7E,0xC3,0xC3,0xC3,0x7F,0x03,0x03,0x06,0x7C,0,0,0,0,0},
    /* 0x3A  :     */ {0,0,0,0x18,0x18,0,0,0,0x18,0x18,0,0,0,0,0,0},
    /* 0x3B  ;     */ {0,0,0,0x18,0x18,0,0,0,0x18,0x18,0x18,0x30,0,0,0,0},
    /* 0x3C  <     */ {0,0,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0,0,0,0,0},
    /* 0x3D  =     */ {0,0,0,0,0x7E,0,0,0x7E,0,0,0,0,0,0,0,0},
    /* 0x3E  >     */ {0,0,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0,0,0,0,0},
    /* 0x3F  ?     */ {0,0,0x7E,0xC3,0x03,0x06,0x0C,0x18,0x18,0,0x18,0x18,0,0,0,0},
    /* 0x40  @     */ {0,0,0x7E,0xC3,0xDB,0xDB,0xDB,0xDB,0xDE,0xC0,0x7F,0,0,0,0,0},
    /* 0x41  A     */ {0,0,0x18,0x3C,0x66,0xC3,0xC3,0xFF,0xC3,0xC3,0xC3,0,0,0,0,0},
    /* 0x42  B     */ {0,0,0xFE,0x63,0x63,0x7E,0x63,0x63,0x63,0x63,0xFE,0,0,0,0,0},
    /* 0x43  C     */ {0,0,0x3E,0x63,0xC0,0xC0,0xC0,0xC0,0xC0,0x63,0x3E,0,0,0,0,0},
    /* 0x44  D     */ {0,0,0xFC,0x66,0x63,0x63,0x63,0x63,0x63,0x66,0xFC,0,0,0,0,0},
    /* 0x45  E     */ {0,0,0xFF,0x61,0x60,0x7C,0x60,0x60,0x60,0x61,0xFF,0,0,0,0,0},
    /* 0x46  F     */ {0,0,0xFF,0x61,0x60,0x7C,0x60,0x60,0x60,0x60,0xF0,0,0,0,0,0},
    /* 0x47  G     */ {0,0,0x3E,0x63,0xC0,0xC0,0xCF,0xC3,0xC3,0x63,0x3E,0,0,0,0,0},
    /* 0x48  H     */ {0,0,0xC3,0xC3,0xC3,0xFF,0xC3,0xC3,0xC3,0xC3,0xC3,0,0,0,0,0},
    /* 0x49  I     */ {0,0,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0,0,0,0,0},
    /* 0x4A  J     */ {0,0,0x0F,0x06,0x06,0x06,0x06,0xC6,0xC6,0xC6,0x7C,0,0,0,0,0},
    /* 0x4B  K     */ {0,0,0xE3,0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0xE3,0,0,0,0,0},
    /* 0x4C  L     */ {0,0,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x61,0xFF,0,0,0,0,0},
    /* 0x4D  M     */ {0,0,0xC3,0xE7,0xFF,0xFF,0xDB,0xC3,0xC3,0xC3,0xC3,0,0,0,0,0},
    /* 0x4E  N     */ {0,0,0xC3,0xE3,0xF3,0xFB,0xCF,0xC7,0xC3,0xC3,0xC3,0,0,0,0,0},
    /* 0x4F  O     */ {0,0,0x7E,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0x7E,0,0,0,0,0},
    /* 0x50  P     */ {0,0,0xFE,0xC3,0xC3,0xC3,0xFE,0xC0,0xC0,0xC0,0xC0,0,0,0,0,0},
    /* 0x51  Q     */ {0,0,0x7E,0xC3,0xC3,0xC3,0xC3,0xC3,0xDB,0xDB,0x7E,0x03,0,0,0,0},
    /* 0x52  R     */ {0,0,0xFE,0xC3,0xC3,0xC3,0xFE,0xCC,0xC6,0xC3,0xC3,0,0,0,0,0},
    /* 0x53  S     */ {0,0,0x7E,0xC3,0xC0,0x7E,0x03,0x03,0x03,0xC3,0x7E,0,0,0,0,0},
    /* 0x54  T     */ {0,0,0xFF,0xDB,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0,0,0,0,0},
    /* 0x55  U     */ {0,0,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0x7E,0,0,0,0,0},
    /* 0x56  V     */ {0,0,0xC3,0xC3,0xC3,0xC3,0xC3,0xC3,0x66,0x3C,0x18,0,0,0,0,0},
    /* 0x57  W     */ {0,0,0xC3,0xC3,0xC3,0xC3,0xDB,0xDB,0xFF,0xE7,0xC3,0,0,0,0,0},
    /* 0x58  X     */ {0,0,0xC3,0xC3,0x66,0x3C,0x18,0x3C,0x66,0xC3,0xC3,0,0,0,0,0},
    /* 0x59  Y     */ {0,0,0xC3,0xC3,0xC3,0x66,0x3C,0x18,0x18,0x18,0x3C,0,0,0,0,0},
    /* 0x5A  Z     */ {0,0,0xFF,0xC3,0x86,0x0C,0x18,0x30,0x61,0xC3,0xFF,0,0,0,0,0},
    /* 0x5B  [     */ {0,0,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0,0,0,0,0},
    /* 0x5C  \     */ {0,0,0xC0,0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0,0,0,0,0,0},
    /* 0x5D  ]     */ {0,0,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0,0,0,0,0},
    /* 0x5E  ^     */ {0,0x18,0x3C,0x66,0xC3,0,0,0,0,0,0,0,0,0,0,0},
    /* 0x5F  _     */ {0,0,0,0,0,0,0,0,0,0,0,0xFF,0,0,0,0},
    /* 0x60  `     */ {0,0x30,0x18,0x0C,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 0x61  a     */ {0,0,0,0,0x7C,0xC6,0x06,0x7E,0xC6,0xC6,0x7E,0,0,0,0,0},
    /* 0x62  b     */ {0,0,0xE0,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0x7C,0,0,0,0,0},
    /* 0x63  c     */ {0,0,0,0,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0,0,0,0,0},
    /* 0x64  d     */ {0,0,0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0xCC,0xCC,0x7E,0,0,0,0,0},
    /* 0x65  e     */ {0,0,0,0,0x7C,0xC6,0xC6,0xFE,0xC0,0xC6,0x7C,0,0,0,0,0},
    /* 0x66  f     */ {0,0,0x1C,0x36,0x30,0x78,0x30,0x30,0x30,0x30,0x78,0,0,0,0,0},
    /* 0x67  g     */ {0,0,0,0,0x7E,0xC6,0xC6,0xC6,0x7E,0x06,0x7C,0,0,0,0,0},
    /* 0x68  h     */ {0,0,0xE0,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0xE7,0,0,0,0,0},
    /* 0x69  i     */ {0,0,0x18,0x18,0,0x38,0x18,0x18,0x18,0x18,0x3C,0,0,0,0,0},
    /* 0x6A  j     */ {0,0,0x0C,0x0C,0,0x1C,0x0C,0x0C,0x0C,0xCC,0x78,0,0,0,0,0},
    /* 0x6B  k     */ {0,0,0xE0,0x60,0x66,0x6C,0x78,0x6C,0x66,0x66,0xE7,0,0,0,0,0},
    /* 0x6C  l     */ {0,0,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0,0,0,0,0},
    /* 0x6D  m     */ {0,0,0,0,0xFC,0xDB,0xDB,0xDB,0xDB,0xDB,0xDB,0,0,0,0,0},
    /* 0x6E  n     */ {0,0,0,0,0xDC,0x66,0x66,0x66,0x66,0x66,0xE7,0,0,0,0,0},
    /* 0x6F  o     */ {0,0,0,0,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0,0,0,0,0},
    /* 0x70  p     */ {0,0,0,0,0xDC,0x66,0x66,0x66,0x66,0x7C,0x60,0xE0,0,0,0,0},
    /* 0x71  q     */ {0,0,0,0,0x7E,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x1E,0,0,0,0},
    /* 0x72  r     */ {0,0,0,0,0xDC,0x66,0x60,0x60,0x60,0x60,0xF0,0,0,0,0,0},
    /* 0x73  s     */ {0,0,0,0,0x7C,0xC6,0x60,0x3C,0x06,0xC6,0x7C,0,0,0,0,0},
    /* 0x74  t     */ {0,0,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0,0,0,0,0},
    /* 0x75  u     */ {0,0,0,0,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x7E,0,0,0,0,0},
    /* 0x76  v     */ {0,0,0,0,0xC3,0xC3,0xC3,0x66,0x3C,0x18,0x18,0,0,0,0,0},
    /* 0x77  w     */ {0,0,0,0,0xC3,0xC3,0xDB,0xDB,0xFF,0x66,0x66,0,0,0,0,0},
    /* 0x78  x     */ {0,0,0,0,0xC3,0x66,0x3C,0x18,0x3C,0x66,0xC3,0,0,0,0,0},
    /* 0x79  y     */ {0,0,0,0,0xC3,0xC3,0xC3,0x66,0x3C,0x18,0x18,0x30,0,0,0,0},
    /* 0x7A  z     */ {0,0,0,0,0xFE,0x8C,0x18,0x30,0x60,0xC6,0xFE,0,0,0,0,0},
    /* 0x7B  {     */ {0,0,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x0E,0,0,0,0,0},
    /* 0x7C  |     */ {0,0,0x18,0x18,0x18,0x18,0,0x18,0x18,0x18,0x18,0,0,0,0,0},
    /* 0x7D  }     */ {0,0,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x70,0,0,0,0,0},
    /* 0x7E  ~     */ {0,0,0x76,0xDC,0,0,0,0,0,0,0,0,0,0,0,0},
};

/* ══════════════════════════════════════════════════════
 *  LOW-LEVEL PIXEL HELPERS
 * ══════════════════════════════════════════════════════ */
void fb_put_pixel(int x, int y, uint32_t col) {
    if ((unsigned)x >= fb.width || (unsigned)y >= fb.height) return;
    fb.addr[y * (fb.pitch / 4) + x] = col;
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t col) {
    for (int row = y; row < y + h; row++)
        for (int c = x; c < x + w; c++)
            fb_put_pixel(c, row, col);
}

/* ── Character / string drawing ─────────────────────── */
void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 127) c = '?';
    const uint8_t* glyph = font8x16[(int)(c - 32)];
    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++)
            fb_put_pixel(x + col, y + row,
                         (bits & (0x80 >> col)) ? fg : bg);
    }
}

void fb_draw_str(int x, int y, const char* s, uint32_t fg, uint32_t bg) {
    for (int i = 0; s[i]; i++)
        fb_draw_char(x + i * FONT_W, y, s[i], fg, bg);
}

void fb_draw_str_len(int x, int y, const char* s, int len,
                     uint32_t fg, uint32_t bg) {
    for (int i = 0; i < len && s[i]; i++)
        fb_draw_char(x + i * FONT_W, y, s[i], fg, bg);
}

/* ══════════════════════════════════════════════════════
 *  MULTIBOOT2 FRAMEBUFFER INIT
 * ══════════════════════════════════════════════════════ */
void fb_init(uint32_t magic, void* mb2_info) {
    if (magic != MB2_MAGIC_RESPONSE) return;
    uint8_t* p   = (uint8_t*)mb2_info + 8;
    uint32_t tot = *(uint32_t*)mb2_info;
    uint8_t* end = (uint8_t*)mb2_info + tot;
    while (p < end) {
        mb2_tag_t* tag = (mb2_tag_t*)p;
        if (tag->type == 0) break;
        if (tag->type == MB2_TAG_FRAMEBUFFER) {
            mb2_tag_fb_t* ft = (mb2_tag_fb_t*)p;
            fb.addr   = (uint32_t*)(uintptr_t)ft->addr;
            fb.width  = ft->width;
            fb.height = ft->height;
            fb.pitch  = ft->pitch;
            fb.bpp    = ft->bpp;
            break;
        }
        p += (tag->size + 7) & ~7u;
    }

    /* Fallback if GRUB didn't report a framebuffer tag */
    if (!fb.addr) {
        fb.width  = 1920;
        fb.height = 1080;
        fb.pitch  = 1920 * 4;
        fb.bpp    = 32;
        fb.addr   = (uint32_t*)0xFD000000; /* typical VESA LFB */
    }

    /* Compute TTY grid dimensions */
    tty_cols = (int)((fb.width  - TTY_PAD_X * 2) / FONT_W);
    tty_rows = (int)((fb.height - TTY_PAD_Y * 2) / FONT_H);
    tty_cx   = 0;
    tty_cy   = 0;

    /* Blank the screen */
    fb_fill_rect(0, 0, (int)fb.width, (int)fb.height, tty_bg);
}

/* ══════════════════════════════════════════════════════
 *  TTY – FULL-SCREEN TERMINAL
 * ══════════════════════════════════════════════════════ */

/* Scroll the TTY up by one row */
static void tty_scroll(void) {
    int bx = TTY_PAD_X;
    int by = TTY_PAD_Y;
    int bw = tty_cols * FONT_W;
    int bh = (tty_rows - 1) * FONT_H;
    /* Blit rows upward by one cell height */
    for (int row = 0; row < bh; row++)
        for (int col = 0; col < bw; col++)
            fb.addr[(by + row) * (fb.pitch / 4) + bx + col] =
                fb.addr[(by + row + FONT_H) * (fb.pitch / 4) + bx + col];
    /* Clear the last row */
    fb_fill_rect(bx, by + bh, bw, FONT_H, tty_bg);
    tty_cy = tty_rows - 1;
}

/* Draw/erase the block cursor at the current position */
static void tty_draw_cursor(int visible) {
    int px = TTY_PAD_X + tty_cx * FONT_W;
    int py = TTY_PAD_Y + tty_cy * FONT_H + FONT_H - 2;
    fb_fill_rect(px, py, FONT_W, 2, visible ? COL_CURSOR : tty_bg);
}

void tty_clear(void) {
    fb_fill_rect(0, 0, (int)fb.width, (int)fb.height, tty_bg);
    tty_cx = 0;
    tty_cy = 0;
}

void tty_putchar(char c) {
    /* Erase old cursor */
    tty_draw_cursor(0);

    if (c == '\n') {
        /* Clear to end of line */
        int px = TTY_PAD_X + tty_cx * FONT_W;
        int py = TTY_PAD_Y + tty_cy * FONT_H;
        if (tty_cols > tty_cx)
            fb_fill_rect(px, py, (tty_cols - tty_cx) * FONT_W, FONT_H, tty_bg);
        tty_cx = 0;
        tty_cy++;
    } else if (c == '\b') {
        if (tty_cx > 0) {
            tty_cx--;
            fb_fill_rect(TTY_PAD_X + tty_cx * FONT_W,
                         TTY_PAD_Y + tty_cy * FONT_H,
                         FONT_W, FONT_H, tty_bg);
        }
        tty_draw_cursor(1);
        return;
    } else if (c == '\r') {
        tty_cx = 0;
    } else {
        if (tty_cx >= tty_cols) { tty_cx = 0; tty_cy++; }
        if (tty_cy >= tty_rows) tty_scroll();
        fb_draw_char(TTY_PAD_X + tty_cx * FONT_W,
                     TTY_PAD_Y + tty_cy * FONT_H,
                     c, tty_fg, tty_bg);
        tty_cx++;
    }

    if (tty_cy >= tty_rows) tty_scroll();

    /* Draw new cursor */
    tty_draw_cursor(1);
}

void tty_print(const char* s) {
    for (int i = 0; s[i]; i++) tty_putchar(s[i]);
}

void tty_print_color(const char* s, uint32_t col) {
    uint32_t old = tty_fg;
    tty_fg = col;
    tty_print(s);
    tty_fg = old;
}

void tty_print_int(int n) {
    if (n == 0) { tty_putchar('0'); return; }
    if (n < 0)  { tty_putchar('-'); n = -n; }
    char buf[12]; int i = 10; buf[11] = '\0';
    while (n > 0 && i >= 0) { buf[i--] = (char)((n % 10) + '0'); n /= 10; }
    tty_print(&buf[i + 1]);
}

void tty_print_prompt(const char* user, const char* host) {
    tty_print_color(user,   RGB(0, 255, 255));
    tty_print_color("@",    RGB(255, 255, 0));
    tty_print_color(host,   RGB(0, 255, 0));
    tty_print_color(":~# ", RGB(255, 255, 255));
}

/* ══════════════════════════════════════════════════════
 *  FAT32 IN-MEMORY FILESYSTEM
 * ══════════════════════════════════════════════════════ */

static inline uint8_t* fat32_sector(uint32_t sect) {
    return &fat32_disk[sect * FAT32_SECTOR_SZ];
}

static inline uint8_t* fat32_cluster(uint32_t clus) {
    uint32_t sect = FAT32_DATA_START + (clus - 2) * FAT32_SECTS_PER_CLUS;
    return fat32_sector(sect);
}

static uint32_t fat_read(uint32_t clus) {
    uint8_t* fat = fat32_sector(FAT32_FAT1_START);
    uint32_t off = clus * 4;
    return  ((uint32_t)fat[off])          |
            ((uint32_t)fat[off+1] <<  8)  |
            ((uint32_t)fat[off+2] << 16)  |
            ((uint32_t)fat[off+3] << 24);
}

static void fat_write(uint32_t clus, uint32_t val) {
    for (int f = 0; f < FAT32_NUM_FATS; f++) {
        uint8_t* fat = fat32_sector(FAT32_FAT1_START + f * FAT32_FAT_SZ_SECTS);
        uint32_t off = clus * 4;
        fat[off]   = (uint8_t)(val);
        fat[off+1] = (uint8_t)(val >>  8);
        fat[off+2] = (uint8_t)(val >> 16);
        fat[off+3] = (uint8_t)(val >> 24);
    }
}

static uint32_t fat_alloc(void) {
    uint32_t max_clus = (FAT32_TOTAL_SECTS - FAT32_DATA_START) / FAT32_SECTS_PER_CLUS + 2;
    for (uint32_t c = 2; c < max_clus; c++) {
        if (fat_read(c) == FAT32_FREE) {
            fat_write(c, FAT32_EOC);
            uint8_t* p = fat32_cluster(c);
            for (int i = 0; i < FAT32_SECTOR_SZ * FAT32_SECTS_PER_CLUS; i++) p[i] = 0;
            return c;
        }
    }
    return 0;
}

static void f32_memset(void* dst, int val, int len) {
    uint8_t* d = (uint8_t*)dst; while (len--) *d++ = (uint8_t)val;
}
static void f32_memcpy(void* dst, const void* src, int len) {
    uint8_t* d = (uint8_t*)dst; const uint8_t* s = (const uint8_t*)src;
    while (len--) *d++ = *s++;
}
static int f32_strlen(const char* s) { int i=0; while(s[i]) i++; return i; }
static int f32_strcmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void write_bpb(void) {
    uint8_t* b = fat32_sector(0);
    f32_memset(b, 0, FAT32_SECTOR_SZ);
    b[0]=0xEB; b[1]=0x58; b[2]=0x90;
    const char oem[] = "BLEXOS  "; f32_memcpy(&b[3], oem, 8);
    b[11]=FAT32_SECTOR_SZ&0xFF; b[12]=(FAT32_SECTOR_SZ>>8)&0xFF;
    b[13]=FAT32_SECTS_PER_CLUS;
    b[14]=FAT32_RSVD_SECTS; b[15]=0;
    b[16]=FAT32_NUM_FATS;
    b[17]=0; b[18]=0;
    b[19]=0; b[20]=0;
    b[21]=0xF8;
    b[22]=0; b[23]=0;
    b[24]=63; b[25]=0;
    b[26]=255; b[27]=0;
    b[28]=b[29]=b[30]=b[31]=0;
    b[32]=FAT32_TOTAL_SECTS&0xFF; b[33]=(FAT32_TOTAL_SECTS>>8)&0xFF;
    b[34]=(FAT32_TOTAL_SECTS>>16)&0xFF; b[35]=(FAT32_TOTAL_SECTS>>24)&0xFF;
    b[36]=FAT32_FAT_SZ_SECTS&0xFF; b[37]=(FAT32_FAT_SZ_SECTS>>8)&0xFF;
    b[38]=b[39]=b[40]=b[41]=b[42]=b[43]=0;
    b[44]=FAT32_ROOT_CLUS&0xFF; b[45]=(FAT32_ROOT_CLUS>>8)&0xFF;
    b[46]=(FAT32_ROOT_CLUS>>16)&0xFF; b[47]=(FAT32_ROOT_CLUS>>24)&0xFF;
    b[48]=1; b[49]=0; b[50]=6; b[51]=0;
    b[64]=0x80; b[66]=0x29;
    b[67]=0x42; b[68]=0x4C; b[69]=0x45; b[70]=0x58;
    const char vlabel[] = "BLEXOS     "; f32_memcpy(&b[71], vlabel, 11);
    const char fstype[] = "FAT32   ";   f32_memcpy(&b[82], fstype, 8);
    b[510]=0x55; b[511]=0xAA;
}

void fat32_format(void) {
    f32_memset(fat32_disk, 0, FAT32_DISK_SIZE);
    write_bpb();
    fat_write(0, 0x0FFFFFF8);
    fat_write(1, 0x0FFFFFFF);
    fat_write(FAT32_ROOT_CLUS, FAT32_EOC);
}

static void pack_83(const char* name, char out_name[8], char out_ext[3]) {
    f32_memset(out_name, ' ', 8); f32_memset(out_ext, ' ', 3);
    int i=0, j=0;
    while (name[i] && name[i] != '.' && j < 8) {
        char c=name[i++]; if (c>='a'&&c<='z') c-=32; out_name[j++]=c;
    }
    if (name[i]=='.') { i++; j=0;
        while (name[i] && j<3) { char c=name[i++]; if(c>='a'&&c<='z') c-=32; out_ext[j++]=c; }
    }
}

static void unpack_83(const char n[8], const char e[3], char out[13]) {
    int i=0, j=0;
    while (i<8 && n[i]!=' ') out[j++]=n[i++];
    if (e[0]!=' ') { out[j++]='.'; i=0; while(i<3&&e[i]!=' ') out[j++]=e[i++]; }
    out[j]='\0';
}

static void path_split(const char* path, char* dir, char* base) {
    int len=f32_strlen(path), last_slash=-1;
    for (int i=0; i<len; i++) if (path[i]=='/') last_slash=i;
    if (last_slash<=0) {
        dir[0]='/'; dir[1]='\0';
        int i=0; const char* s=(last_slash==0)?path+1:path;
        while (s[i]) { base[i]=s[i]; i++; } base[i]='\0';
    } else {
        int i; for (i=0; i<last_slash; i++) dir[i]=path[i]; dir[i]='\0';
        i=0; while (path[last_slash+1+i]) { base[i]=path[last_slash+1+i]; i++; } base[i]='\0';
    }
}

static fat32_dirent_t* find_dirent_in_chain(uint32_t start_clus,
                                              const char name83[8],
                                              const char ext83[3]) {
    uint32_t clus=start_clus;
    int epc=(FAT32_SECTOR_SZ*FAT32_SECTS_PER_CLUS)/sizeof(fat32_dirent_t);
    while (clus<0x0FFFFFF8 && clus>=2) {
        fat32_dirent_t* dir=(fat32_dirent_t*)fat32_cluster(clus);
        for (int i=0; i<epc; i++) {
            if ((uint8_t)dir[i].name[0]==0x00) return 0;
            if ((uint8_t)dir[i].name[0]==0xE5) continue;
            int match=1;
            for (int k=0; k<8; k++) if (dir[i].name[k]!=name83[k]) { match=0; break; }
            if (match) for (int k=0; k<3; k++) if (dir[i].ext[k]!=ext83[k]) { match=0; break; }
            if (match) return &dir[i];
        }
        clus=fat_read(clus);
    }
    return 0;
}

static uint32_t resolve_parent(const char* path, char n83[8], char e83[3]) {
    char dir[64], base[32];
    path_split(path, dir, base);
    pack_83(base, n83, e83);
    if (dir[0]=='/'&&dir[1]=='\0') return FAT32_ROOT_CLUS;
    uint32_t cur=FAT32_ROOT_CLUS;
    const char* p=dir; if (*p=='/') p++;
    while (*p) {
        char comp[9]; int ci=0;
        while (*p&&*p!='/') { if (ci<8) comp[ci++]=*p; p++; }
        if (*p=='/') p++;
        comp[ci]='\0';
        char cn[8],ce[3]; pack_83(comp,cn,ce);
        fat32_dirent_t* de=find_dirent_in_chain(cur,cn,ce);
        if (!de||(!(de->attr&FAT32_ATTR_DIR))) return 0;
        cur=((uint32_t)de->fst_clus_hi<<16)|de->fst_clus_lo;
    }
    return cur;
}

static int dir_add_entry(uint32_t dir_clus, fat32_dirent_t* new_ent) {
    int epc=(FAT32_SECTOR_SZ*FAT32_SECTS_PER_CLUS)/(int)sizeof(fat32_dirent_t);
    uint32_t clus=dir_clus, prev=0;
    while (clus<0x0FFFFFF8&&clus>=2) {
        fat32_dirent_t* dir=(fat32_dirent_t*)fat32_cluster(clus);
        for (int i=0; i<epc; i++) {
            if ((uint8_t)dir[i].name[0]==0x00||(uint8_t)dir[i].name[0]==0xE5) {
                f32_memcpy(&dir[i],new_ent,sizeof(fat32_dirent_t));
                if (i+1<epc) f32_memset(dir[i+1].name,0,1);
                return 1;
            }
        }
        prev=clus; clus=fat_read(clus);
    }
    uint32_t newc=fat_alloc(); if (!newc) return 0;
    fat_write(prev,newc);
    fat32_dirent_t* dir=(fat32_dirent_t*)fat32_cluster(newc);
    f32_memcpy(&dir[0],new_ent,sizeof(fat32_dirent_t));
    f32_memset(dir[1].name,0,1);
    return 1;
}

int fat32_create(const char* path, int is_dir) {
    char n83[8],e83[3];
    uint32_t parent=resolve_parent(path,n83,e83); if (!parent) return 0;
    if (find_dirent_in_chain(parent,n83,e83)) return 0;
    uint32_t clus=fat_alloc(); if (!clus) return 0;
    fat32_dirent_t ent; f32_memset(&ent,0,sizeof(ent));
    f32_memcpy(ent.name,n83,8); f32_memcpy(ent.ext,e83,3);
    ent.attr=is_dir?FAT32_ATTR_DIR:FAT32_ATTR_ARCHIVE;
    ent.fst_clus_hi=(uint16_t)(clus>>16); ent.fst_clus_lo=(uint16_t)(clus&0xFFFF);
    ent.file_size=0;
    return dir_add_entry(parent,&ent);
}

int fat32_write(const char* path, const char* data, int len) {
    char n83[8],e83[3];
    uint32_t parent=resolve_parent(path,n83,e83); if (!parent) return 0;
    fat32_dirent_t* de=find_dirent_in_chain(parent,n83,e83);
    if (!de) { if (!fat32_create(path,0)) return 0; de=find_dirent_in_chain(parent,n83,e83); if (!de) return 0; }
    uint32_t clus=((uint32_t)de->fst_clus_hi<<16)|de->fst_clus_lo;
    if (!clus||clus<2) { clus=fat_alloc(); if (!clus) return 0; de->fst_clus_hi=(uint16_t)(clus>>16); de->fst_clus_lo=(uint16_t)(clus&0xFFFF); }
    int clus_sz=FAT32_SECTOR_SZ*FAT32_SECTS_PER_CLUS, written=0;
    uint32_t cur=clus;
    while (written<len) {
        int chunk=len-written; if (chunk>clus_sz) chunk=clus_sz;
        uint8_t* buf=fat32_cluster(cur); f32_memcpy(buf,data+written,chunk); written+=chunk;
        if (written<len) {
            uint32_t next=fat_read(cur);
            if (next>=0x0FFFFFF8||next<2) { next=fat_alloc(); if (!next) break; fat_write(cur,next); }
            cur=next;
        }
    }
    fat_write(cur,FAT32_EOC); de->file_size=(uint32_t)len;
    return written;
}

int fat32_read(const char* path, char* buf, int maxlen) {
    char n83[8],e83[3];
    uint32_t parent=resolve_parent(path,n83,e83); if (!parent) return -1;
    fat32_dirent_t* de=find_dirent_in_chain(parent,n83,e83); if (!de) return -1;
    uint32_t clus=((uint32_t)de->fst_clus_hi<<16)|de->fst_clus_lo;
    int total=(int)de->file_size; if (total>maxlen) total=maxlen;
    int rd=0, clus_sz=FAT32_SECTOR_SZ*FAT32_SECTS_PER_CLUS;
    while (clus<0x0FFFFFF8&&clus>=2&&rd<total) {
        int chunk=total-rd; if (chunk>clus_sz) chunk=clus_sz;
        f32_memcpy(buf+rd,fat32_cluster(clus),chunk); rd+=chunk; clus=fat_read(clus);
    }
    return rd;
}

int fat32_readdir(const char* dirpath, char names[][13], int maxn) {
    uint32_t dir_clus;
    if (dirpath[0]=='/'&&dirpath[1]=='\0') { dir_clus=FAT32_ROOT_CLUS; }
    else {
        char n83[8],e83[3];
        uint32_t parent=resolve_parent(dirpath,n83,e83); if (!parent) return 0;
        fat32_dirent_t* de=find_dirent_in_chain(parent,n83,e83);
        if (!de||!(de->attr&FAT32_ATTR_DIR)) return 0;
        dir_clus=((uint32_t)de->fst_clus_hi<<16)|de->fst_clus_lo;
    }
    int epc=(FAT32_SECTOR_SZ*FAT32_SECTS_PER_CLUS)/(int)sizeof(fat32_dirent_t), count=0;
    uint32_t clus=dir_clus;
    while (clus<0x0FFFFFF8&&clus>=2&&count<maxn) {
        fat32_dirent_t* dir=(fat32_dirent_t*)fat32_cluster(clus);
        for (int i=0; i<epc&&count<maxn; i++) {
            if ((uint8_t)dir[i].name[0]==0x00) goto done;
            if ((uint8_t)dir[i].name[0]==0xE5) continue;
            unpack_83(dir[i].name,dir[i].ext,names[count++]);
        }
        clus=fat_read(clus);
    }
done: return count;
}

int fat32_exists(const char* path) {
    char n83[8],e83[3];
    uint32_t parent=resolve_parent(path,n83,e83); if (!parent) return 0;
    return find_dirent_in_chain(parent,n83,e83)!=0;
}

int fat32_delete(const char* path) {
    char n83[8],e83[3];
    uint32_t parent=resolve_parent(path,n83,e83); if (!parent) return 0;
    fat32_dirent_t* de=find_dirent_in_chain(parent,n83,e83); if (!de) return 0;
    uint32_t clus=((uint32_t)de->fst_clus_hi<<16)|de->fst_clus_lo;
    while (clus<0x0FFFFFF8&&clus>=2) { uint32_t next=fat_read(clus); fat_write(clus,FAT32_FREE); clus=next; }
    de->name[0]=(char)0xE5;
    return 1;
}

void fat32_stat(const char* path, int* out_size, int* out_isdir) {
    char n83[8],e83[3];
    uint32_t parent=resolve_parent(path,n83,e83);
    if (!parent) { *out_size=-1; *out_isdir=0; return; }
    fat32_dirent_t* de=find_dirent_in_chain(parent,n83,e83);
    if (!de) { *out_size=-1; *out_isdir=0; return; }
    *out_size=(int)de->file_size;
    *out_isdir=(de->attr&FAT32_ATTR_DIR)?1:0;
}
