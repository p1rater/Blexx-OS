/*
 * fb.h – Framebuffer TTY driver for BNU/Blex
 * 1920x1080x32 VESA framebuffer, Multiboot2
 *
 * TTY mode: direct full-screen text output with scrolling.
 * No window manager – the framebuffer is treated as a single
 * terminal (like a Linux virtual console / TTY).
 *
 * FAT32 in-memory filesystem (512 KB ramdisk):
 *   Standard BPB, dual FAT tables, 8.3 directory entries.
 */
#ifndef FB_H
#define FB_H

#include <stdint.h>

/* ── Framebuffer state ──────────────────────────────── */
typedef struct {
    uint32_t* addr;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;
    uint8_t   bpp;
} fb_info_t;

extern fb_info_t fb;

/* ── Colour helpers ─────────────────────────────────── */
#define RGB(r,g,b)  ((uint32_t)(((r)<<16)|((g)<<8)|(b)))

/* Catppuccin Mocha palette */
#define COL_DESKTOP      RGB(17,  17,  27)
#define COL_TASKBAR      RGB(24,  24,  37)
#define COL_TASKBAR_SEP  RGB(49,  50,  68)

#define COL_TITLE_ACT_L  RGB(137, 180, 250)
#define COL_TITLE_ACT_R  RGB(203, 166, 247)
#define COL_TITLE_INACT  RGB(49,  50,  68)

#define COL_BORD_ACT     RGB(137, 180, 250)
#define COL_BORD_INACT   RGB(49,  50,  68)

#define COL_WIN_BODY     RGB(18,  18,  28)
#define COL_SHELL_BG     RGB(10,  14,  22)

#define COL_TEXT_FG      RGB(205, 214, 244)
#define COL_TEXT_DIM     RGB(108, 112, 134)

#define COL_BTN_CLOSE    RGB(243, 139, 168)
#define COL_BTN_MIN      RGB(249, 226, 175)
#define COL_BTN_MAX      RGB(166, 227, 161)

#define COL_GREEN        RGB(166, 227, 161)
#define COL_CYAN         RGB(137, 220, 235)
#define COL_YELLOW       RGB(249, 226, 175)
#define COL_RED          RGB(243, 139, 168)
#define COL_WHITE        RGB(205, 214, 244)
#define COL_CURSOR       RGB(137, 180, 250)

#define COL_DESK_TL      RGB(17,  17,  27)
#define COL_DESK_BR      RGB(30,  20,  45)

/* ── TTY layout ─────────────────────────────────────── */
/* Font is 12×12 px (rendered from 16×16 bitmap, FONT_W/H used for advance) */
#define FONT_W    8   /* character cell width  */
#define FONT_H   16   /* character cell height */
#define TTY_PAD_X 8   /* left/right margin px  */
#define TTY_PAD_Y 4   /* top margin px         */

/* Framebuffer API */
void fb_init           (uint32_t magic, void* mb2_info);
void fb_put_pixel      (int x, int y, uint32_t col);
void fb_fill_rect      (int x, int y, int w, int h, uint32_t col);
void fb_draw_char      (int x, int y, char c, uint32_t fg, uint32_t bg);
void fb_draw_str       (int x, int y, const char* s, uint32_t fg, uint32_t bg);
void fb_draw_str_len   (int x, int y, const char* s, int len, uint32_t fg, uint32_t bg);

/* ── TTY API (replaces WM API) ───────────────────────── */
void tty_clear        (void);
void tty_putchar      (char c);
void tty_print        (const char* s);
void tty_print_color  (const char* s, uint32_t col);
void tty_print_int    (int n);
void tty_print_prompt (const char* user, const char* host);

/* Shims so command_logic.c compiles without changes */
#define wm_putchar      tty_putchar
#define wm_print        tty_print
#define wm_print_color  tty_print_color
#define wm_print_int    tty_print_int
#define wm_print_prompt tty_print_prompt
#define wm_clear_body(id) tty_clear()
#define wm_draw_all()   /* nop */
#define wm_draw_taskbar() /* nop */
#define wm_draw_desktop() /* nop */

/* Dummy window handle (no WM) */
static inline int wm_open_window(int x, int y, int w, int h,
                                  const char* t, uint32_t bg) {
    (void)x; (void)y; (void)w; (void)h; (void)t; (void)bg;
    return 0;
}

/* focused_win shim – TTY has no windows; keep it at 0 so checks pass */
extern int focused_win;

/* ════════════════════════════════════════════════════════
 * FAT32 IN-MEMORY FILESYSTEM
 * ════════════════════════════════════════════════════════ */
#define FAT32_SECTOR_SZ      512
#define FAT32_TOTAL_SECTS    1024
#define FAT32_DISK_SIZE      (FAT32_TOTAL_SECTS * FAT32_SECTOR_SZ)

#define FAT32_SECTS_PER_CLUS 1
#define FAT32_NUM_FATS       2
#define FAT32_FAT_SZ_SECTS   32
#define FAT32_RSVD_SECTS     1
#define FAT32_ROOT_CLUS      2

#define FAT32_FAT1_START     FAT32_RSVD_SECTS
#define FAT32_FAT2_START     (FAT32_FAT1_START + FAT32_FAT_SZ_SECTS)
#define FAT32_DATA_START     (FAT32_FAT2_START + FAT32_FAT_SZ_SECTS)

#define FAT32_EOC   0x0FFFFFF8u
#define FAT32_FREE  0x00000000u
#define FAT32_BAD   0x0FFFFFF7u

typedef struct __attribute__((packed)) {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  ntres;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} fat32_dirent_t;

#define FAT32_ATTR_RDONLY  0x01
#define FAT32_ATTR_DIR     0x10
#define FAT32_ATTR_ARCHIVE 0x20

extern uint8_t fat32_disk[FAT32_DISK_SIZE];

void fat32_format  (void);
int  fat32_create  (const char* path, int is_dir);
int  fat32_write   (const char* path, const char* data, int len);
int  fat32_read    (const char* path, char* buf, int maxlen);
int  fat32_readdir (const char* dirpath, char names[][13], int maxn);
int  fat32_exists  (const char* path);
int  fat32_delete  (const char* path);
void fat32_stat    (const char* path, int* out_size, int* out_isdir);

/* ── Legacy VFS shim ────────────────────────────────── */
#include "kernel_utils.h"
extern vfs_node_t ram_disk[32];

#endif /* FB_H */
