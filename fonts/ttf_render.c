/*
 * ttf_render.c – Minimal TrueType font rasterizer for BlexOS
 * Copyright (C) 2026 Blex – BOSL License
 *
 * Implements a compact TrueType parser + scanline rasterizer
 * suitable for a freestanding x86 kernel environment.
 * No libc, no heap allocations beyond a fixed static scratch buffer.
 */

#include "ttf_render.h"
#include <stdint.h>

/* ── Freestanding helpers ──────────────────────────────── */
#ifndef NULL
#define NULL ((void*)0)
#endif

static int32_t ttf_abs(int32_t x) { return x < 0 ? -x : x; }
static int32_t ttf_min(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t ttf_max(int32_t a, int32_t b) { return a > b ? a : b; }

/* Fixed-point scale: 16.16 */
typedef int32_t fixed16_t;
#define F16(x)        ((fixed16_t)((x) * 65536))
#define F16_MUL(a,b)  ((fixed16_t)(((int64_t)(a)*(b)) >> 16))
#define F16_DIV(a,b)  ((fixed16_t)(((int64_t)(a) << 16) / (b)))
#define F16_FLOOR(x)  ((x) >> 16)
#define F16_CEIL(x)   (((x) + 0xFFFF) >> 16)

/* ── TrueType table tags ──────────────────────────────── */
#define TAG(a,b,c,d) (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(d))
#define TAG_cmap TAG('c','m','a','p')
#define TAG_glyf TAG('g','l','y','f')
#define TAG_head TAG('h','e','a','d')
#define TAG_hhea TAG('h','h','e','a')
#define TAG_hmtx TAG('h','m','t','x')
#define TAG_loca TAG('l','o','c','a')
#define TAG_maxp TAG('m','a','x','p')

/* ── Big-endian read helpers ─────────────────────────── */
static uint16_t ru16(const uint8_t *p) {
    return (uint16_t)(((uint32_t)p[0]<<8)|p[1]);
}
static int16_t ri16(const uint8_t *p) {
    return (int16_t)ru16(p);
}
static uint32_t ru32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|(uint32_t)p[3];
}

/* ── Glyph outline scratch buffer ────────────────────── */
#define MAX_POINTS   1024
#define MAX_CONTOURS  64

typedef struct {
    int32_t x, y;   /* in font units */
    uint8_t on;     /* 1 = on-curve, 0 = off-curve */
} ttf_point_t;

static ttf_point_t g_pts[MAX_POINTS];
static int         g_npts;
static uint16_t    g_ends[MAX_CONTOURS];
static int         g_ncont;

/* ── Scanline rasterizer scratch ─────────────────────── */
/* Stores x-intercepts per scanline row */
#define MAX_ISECTS   64
static int16_t  g_isects[TTF_MAX_GLYPH_H][MAX_ISECTS];
static uint8_t  g_nisects[TTF_MAX_GLYPH_H];

/* Output bitmap: 1 bit per pixel stored as bytes */
static uint8_t  g_bitmap[TTF_MAX_GLYPH_H][TTF_MAX_GLYPH_W];

/* ── Font state ──────────────────────────────────────── */
static const uint8_t *g_font = NULL;
static uint32_t       g_font_size = 0;

/* Table offsets */
static uint32_t g_off_cmap = 0;
static uint32_t g_off_glyf = 0;
static uint32_t g_off_head = 0;
static uint32_t g_off_hhea = 0;
static uint32_t g_off_hmtx = 0;
static uint32_t g_off_loca = 0;

/* Derived metrics */
static int32_t  g_units_per_em  = 1000;
static int32_t  g_ascender      = 800;
static int32_t  g_descender     = -200;
static int32_t  g_line_gap      = 0;
static int16_t  g_loca_format   = 0;   /* 0=short, 1=long */
static uint16_t g_num_glyphs    = 0;
static uint16_t g_num_hmetrics  = 0;

/* Scale (font units -> pixels) as fixed16 */
static fixed16_t g_scale = 0;
static int32_t   g_px_height = 16;

/* Cached cmap: format-4 subtable pointer */
static const uint8_t *g_cmap4  = NULL;

/* ── Table lookup ────────────────────────────────────── */
static uint32_t ttf_find_table(uint32_t tag) {
    if (!g_font || g_font_size < 12) return 0;
    uint16_t ntbl = ru16(g_font + 4);
    for (uint16_t i = 0; i < ntbl; i++) {
        const uint8_t *rec = g_font + 12 + i * 16;
        if (ru32(rec) == tag) return ru32(rec + 8);
    }
    return 0;
}

/* ── cmap: codepoint -> glyph index ─────────────────── */
static uint16_t ttf_cmap_lookup(uint32_t cp) {
    if (!g_cmap4) return 0;
    const uint8_t *p = g_cmap4;
    uint16_t seg2 = ru16(p + 6);
    uint16_t nseg = (uint16_t)(seg2 / 2);
    const uint8_t *endCodes   = p + 14;
    const uint8_t *startCodes = endCodes + 2 + nseg * 2;
    const uint8_t *idDeltas   = startCodes + nseg * 2;
    const uint8_t *idRangeOff = idDeltas   + nseg * 2;
    const uint8_t *glyphIds   = idRangeOff + nseg * 2;

    for (uint16_t i = 0; i < nseg; i++) {
        uint16_t ec = ru16(endCodes   + i * 2);
        uint16_t sc = ru16(startCodes + i * 2);
        if (cp > ec) continue;
        if (cp < sc) return 0;
        uint16_t ro = ru16(idRangeOff + i * 2);
        if (ro == 0) {
            int16_t delta = ri16(idDeltas + i * 2);
            return (uint16_t)((cp + delta) & 0xFFFF);
        } else {
            const uint8_t *addr = idRangeOff + i * 2 + ro;
            addr += (cp - sc) * 2;
            if (addr + 2 > g_font + g_font_size) return 0;
            uint16_t gid = ru16(addr);
            if (gid == 0) return 0;
            int16_t delta = ri16(idDeltas + i * 2);
            return (uint16_t)((gid + delta) & 0xFFFF);
        }
    }
    return 0;
}

/* ── loca: glyph index -> glyf offset ───────────────── */
static uint32_t ttf_loca(uint16_t gid) {
    if (!g_off_loca) return 0;
    if (g_loca_format == 0) {
        return (uint32_t)ru16(g_font + g_off_loca + gid * 2) * 2;
    } else {
        return ru32(g_font + g_off_loca + gid * 4);
    }
}

/* ── hmtx: advance width ─────────────────────────────── */
static uint16_t ttf_advance_width(uint16_t gid) {
    if (!g_off_hmtx || g_num_hmetrics == 0) return (uint16_t)g_units_per_em;
    if (gid < g_num_hmetrics) {
        return ru16(g_font + g_off_hmtx + gid * 4);
    } else {
        return ru16(g_font + g_off_hmtx + (g_num_hmetrics - 1) * 4);
    }
}

/* ── Glyph outline loader ─────────────────────────────── */
/* Flags for simple glyph points */
#define FL_ON_CURVE     0x01
#define FL_X_SHORT      0x02
#define FL_Y_SHORT      0x04
#define FL_REPEAT       0x08
#define FL_X_SAME_POS   0x10
#define FL_Y_SAME_POS   0x20

static int ttf_load_simple_glyph(const uint8_t *p, uint16_t ncontours) {
    /* End-points of contours */
    uint16_t last_end = 0;
    for (int c = 0; c < ncontours && c < MAX_CONTOURS; c++) {
        g_ends[c] = ru16(p + c * 2);
        last_end  = g_ends[c];
        g_ncont   = c + 1;
    }
    g_npts = (int)(last_end + 1);
    if (g_npts > MAX_POINTS) g_npts = MAX_POINTS;

    p += ncontours * 2;
    uint16_t instr_len = ru16(p);
    p += 2 + instr_len;

    /* Read flags (with repeat) */
    uint8_t flags[MAX_POINTS];
    for (int i = 0; i < g_npts; ) {
        uint8_t f = *p++;
        flags[i++] = f;
        if (f & FL_REPEAT) {
            uint8_t rep = *p++;
            while (rep-- && i < g_npts) flags[i++] = f;
        }
    }
    /* X coordinates */
    int32_t x = 0;
    for (int i = 0; i < g_npts; i++) {
        if (flags[i] & FL_X_SHORT) {
            int16_t d = (int16_t)(*p++);
            x += (flags[i] & FL_X_SAME_POS) ? d : -d;
        } else if (flags[i] & FL_X_SAME_POS) {
            /* same as previous */
        } else {
            x += ri16(p); p += 2;
        }
        g_pts[i].x  = x;
        g_pts[i].on = (flags[i] & FL_ON_CURVE) ? 1 : 0;
    }
    /* Y coordinates */
    int32_t y = 0;
    for (int i = 0; i < g_npts; i++) {
        if (flags[i] & FL_Y_SHORT) {
            int16_t d = (int16_t)(*p++);
            y += (flags[i] & FL_Y_SAME_POS) ? d : -d;
        } else if (flags[i] & FL_Y_SAME_POS) {
            /* same as previous */
        } else {
            y += ri16(p); p += 2;
        }
        g_pts[i].y = y;
    }
    return 1;
}

/* ── Scale font unit to pixel (returns integer pixels) ── */
static inline int32_t scl(int32_t fu) {
    return F16_FLOOR(F16_MUL(g_scale, (fixed16_t)(fu << 16)) + (1 << 15));
}

/* ── Scanline rasterizer ─────────────────────────────── */
static void rast_add_isect(int y, int16_t x) {
    if ((unsigned)y >= (unsigned)TTF_MAX_GLYPH_H) return;
    uint8_t n = g_nisects[y];
    if (n >= MAX_ISECTS) return;
    g_isects[y][n] = x;
    g_nisects[y] = (uint8_t)(n + 1);
}

static void rast_line(int x0, int y0, int x1, int y1,
                      int bx, int by, int bw, int bh) {
    /* Scanline intersections for edges crossing integer y */
    if (y0 == y1) return;
    if (y0 > y1) {
        int tmp;
        tmp = x0; x0 = x1; x1 = tmp;
        tmp = y0; y0 = y1; y1 = tmp;
    }
    int dy = y1 - y0;
    int dx = x1 - x0;
    int ymin = ttf_max(y0, by);
    int ymax = ttf_min(y1 - 1, by + bh - 1);
    for (int y = ymin; y <= ymax; y++) {
        /* x at scanline y + 0.5 */
        int32_t t_num = (2 * (y - y0) + 1);
        int32_t t_den = (2 * dy);
        int32_t xi = x0 + (dx * t_num) / t_den;
        rast_add_isect(y - by, (int16_t)(xi - bx));
    }
}

/* Quadratic Bezier subdivide until flat */
static void rast_bezier(int x0, int y0, int x1, int y1, int x2, int y2,
                        int bx, int by, int bw, int bh, int depth) {
    /* Flatness test: max deviation from line */
    int mx = (x0 + x2) / 2;
    int my = (y0 + y2) / 2;
    int dx = ttf_abs(x1 - mx);
    int dy = ttf_abs(y1 - my);
    if ((dx + dy) <= 1 || depth >= 8) {
        rast_line(x0, y0, x2, y2, bx, by, bw, bh);
        return;
    }
    int ax = (x0 + x1) / 2, ay = (y0 + y1) / 2;
    int bx2= (x1 + x2) / 2, by2= (y1 + y2) / 2;
    int cx = (ax + bx2) / 2, cy = (ay + by2) / 2;
    rast_bezier(x0, y0, ax, ay, cx, cy, bx, by, bw, bh, depth + 1);
    rast_bezier(cx, cy, bx2, by2, x2, y2, bx, by, bw, bh, depth + 1);
}

/* Sort scanline intersections (insertion sort – short arrays) */
static void sort_isects(int16_t *arr, int n) {
    for (int i = 1; i < n; i++) {
        int16_t key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) { arr[j+1] = arr[j]; j--; }
        arr[j+1] = key;
    }
}

/* Rasterize currently loaded glyph outline into g_bitmap.
   bx,by = glyph origin in font units; bw,bh = bitmap size in pixels. */
static void rasterize_glyph(int32_t bx_fu, int32_t by_fu, int bw, int bh) {
    /* Clear */
    for (int r = 0; r < bh; r++) {
        g_nisects[r] = 0;
        for (int c = 0; c < bw; c++) g_bitmap[r][c] = 0;
    }

    int cont_start = 0;
    for (int c = 0; c < g_ncont; c++) {
        int cont_end = (int)g_ends[c];

        /* Walk the contour: convert to pixel space, emit edges */
        int npt = cont_end - cont_start + 1;
        if (npt < 2) { cont_start = cont_end + 1; continue; }

        /* Convert to pixel coords relative to bitmap origin */
        /* pixel_x = scl(font_x - bx_fu), pixel_y = bh - 1 - scl(font_y - by_fu) */
        #define PX(i) ( scl(g_pts[cont_start + (i)].x - bx_fu) )
        #define PY(i) ( bh - 1 - scl(g_pts[cont_start + (i)].y - by_fu) )
        #define ON(i) ( g_pts[cont_start + (i)].on )

        /* Expand implicit on-curve points and emit Bezier/line edges */
        int i = 0;
        int prev_x = PX(npt - 1);
        int prev_y = PY(npt - 1);
        int prev_on = ON(npt - 1);

        while (i < npt) {
            int cur_x = PX(i);
            int cur_y = PY(i);
            int cur_on = ON(i);

            if (prev_on && cur_on) {
                rast_line(prev_x, prev_y, cur_x, cur_y,
                          0, 0, bw, bh);
            } else if (!prev_on && cur_on) {
                /* Bezier: need a previous on-curve */
                /* find the on-curve before prev */
                int bi = (i - 2 + npt) % npt;
                int bxi = PX(bi), byi = PY(bi);
                (void)bxi; (void)byi;
                /* already emitted in prior iteration */
            } else if (prev_on && !cur_on) {
                /* start of Bezier: wait for next */
            } else {
                /* two off-curve: midpoint is implicit on-curve */
                int mid_x = (prev_x + cur_x) / 2;
                int mid_y = (prev_y + cur_y) / 2;
                /* emit Bezier prev_prev_on -> prev_off -> mid_on */
                /* find prev on-curve */
                int pi2 = (i - 2 + npt) % npt;
                rast_bezier(PX(pi2), PY(pi2), prev_x, prev_y, mid_x, mid_y,
                            0, 0, bw, bh, 0);
            }

            /* Full Bezier emission: emit when we have on-off-on */
            if (prev_on && !cur_on) {
                int next_i = (i + 1) % npt;
                int next_x = PX(next_i);
                int next_y = PY(next_i);
                int next_on = ON(next_i);
                if (next_on) {
                    rast_bezier(prev_x, prev_y, cur_x, cur_y, next_x, next_y,
                                0, 0, bw, bh, 0);
                } else {
                    /* implicit on-curve midpoint */
                    int mid_x = (cur_x + next_x) / 2;
                    int mid_y = (cur_y + next_y) / 2;
                    rast_bezier(prev_x, prev_y, cur_x, cur_y, mid_x, mid_y,
                                0, 0, bw, bh, 0);
                }
            }

            prev_x  = cur_x;
            prev_y  = cur_y;
            prev_on = cur_on;
            i++;
        }
        #undef PX
        #undef PY
        #undef ON

        cont_start = cont_end + 1;
    }

    /* Fill: sort intersections per row, fill between pairs */
    for (int row = 0; row < bh; row++) {
        int n = (int)g_nisects[row];
        if (n < 2) continue;
        sort_isects(g_isects[row], n);
        for (int k = 0; k + 1 < n; k += 2) {
            int xa = (int)g_isects[row][k];
            int xb = (int)g_isects[row][k+1];
            if (xa < 0) xa = 0;
            if (xb > bw - 1) xb = bw - 1;
            for (int x = xa; x <= xb && x < bw; x++)
                g_bitmap[row][x] = 1;
        }
    }
}

/* ── Public API ──────────────────────────────────────────── */

int ttf_init(const uint8_t *data, uint32_t size) {
    if (!data || size < 12) return 0;
    /* Check sfVersion == 0x00010000 or 'true' */
    uint32_t ver = ru32(data);
    if (ver != 0x00010000 && ver != 0x74727565) return 0;

    g_font      = data;
    g_font_size = size;

    g_off_cmap = ttf_find_table(TAG_cmap);
    g_off_glyf = ttf_find_table(TAG_glyf);
    g_off_head = ttf_find_table(TAG_head);
    g_off_hhea = ttf_find_table(TAG_hhea);
    g_off_hmtx = ttf_find_table(TAG_hmtx);
    g_off_loca = ttf_find_table(TAG_loca);

    if (!g_off_glyf || !g_off_head || !g_off_loca) return 0;

    /* head */
    g_units_per_em = (int32_t)ru16(g_font + g_off_head + 18);
    g_loca_format  = ri16(g_font + g_off_head + 50);

    /* maxp */
    uint32_t off_maxp = ttf_find_table(TAG_maxp);
    if (off_maxp) g_num_glyphs = ru16(g_font + off_maxp + 4);

    /* hhea */
    if (g_off_hhea) {
        g_ascender      = (int32_t)ri16(g_font + g_off_hhea + 4);
        g_descender     = (int32_t)ri16(g_font + g_off_hhea + 6);
        g_line_gap      = (int32_t)ri16(g_font + g_off_hhea + 8);
        g_num_hmetrics  = ru16(g_font + g_off_hhea + 34);
    }

    /* cmap: find format-4 subtable for platform 3 (Windows) enc 1 or platform 0 */
    if (g_off_cmap) {
        uint16_t nsubt = ru16(g_font + g_off_cmap + 2);
        const uint8_t *rec = g_font + g_off_cmap + 4;
        uint32_t best_off = 0;
        int      best_pri = -1;
        for (uint16_t i = 0; i < nsubt; i++, rec += 8) {
            uint16_t pid = ru16(rec);
            uint16_t eid = ru16(rec + 2);
            uint32_t off = ru32(rec + 4) + g_off_cmap;
            if (off + 2 > g_font_size) continue;
            uint16_t fmt = ru16(g_font + off);
            if (fmt != 4) continue;
            int pri = -1;
            if      (pid == 3 && eid == 1) pri = 3;
            else if (pid == 0)             pri = 2;
            else if (pid == 3 && eid == 0) pri = 1;
            else if (pid == 1)             pri = 0;
            if (pri > best_pri) { best_pri = pri; best_off = off; }
        }
        if (best_off) g_cmap4 = g_font + best_off;
    }

    ttf_set_size(16);
    return 1;
}

void ttf_set_size(int pixel_height) {
    g_px_height = pixel_height;
    /* scale = pixel_height / (ascender - descender) */
    int32_t cell = g_ascender - g_descender;
    if (cell <= 0) cell = g_units_per_em;
    g_scale = F16_DIV(F16((int32_t)pixel_height), F16((int32_t)cell));
}

int ttf_line_height(void) {
    int32_t cell = g_ascender - g_descender + g_line_gap;
    return F16_CEIL(F16_MUL(g_scale, F16(cell)));
}

int ttf_ascender(void) {
    return F16_CEIL(F16_MUL(g_scale, F16(g_ascender)));
}

int ttf_char_width(uint32_t codepoint) {
    if (!g_font) return g_px_height / 2;
    uint16_t gid = ttf_cmap_lookup(codepoint);
    int32_t aw = (int32_t)ttf_advance_width(gid);
    return F16_CEIL(F16_MUL(g_scale, F16(aw)));
}

int ttf_draw_char(uint32_t *fb_addr, uint32_t pitch,
                  int px, int py,
                  uint32_t codepoint, uint32_t fg, uint32_t bg) {
    if (!g_font) return g_px_height / 2;

    uint16_t gid = ttf_cmap_lookup(codepoint);
    int32_t aw   = (int32_t)ttf_advance_width(gid);
    int     adv  = F16_CEIL(F16_MUL(g_scale, F16(aw)));

    uint32_t glyf_off = ttf_loca(gid);
    uint32_t next_off = ttf_loca((uint16_t)(gid + 1));
    if (glyf_off == next_off) {
        /* Empty glyph (space etc.) – fill background rectangle */
        int lh = ttf_line_height();
        uint32_t pitch32 = pitch / 4;
        for (int row = 0; row < lh; row++) {
            uint32_t *dst = fb_addr + (py + row) * pitch32 + px;
            for (int col = 0; col < adv; col++) dst[col] = bg;
        }
        return adv;
    }

    const uint8_t *gp = g_font + g_off_glyf + glyf_off;
    if (gp + 10 > g_font + g_font_size) return adv;

    int16_t ncontours = ri16(gp);
    if (ncontours < 0) {
        /* Compound glyph: not fully supported; return advance */
        return adv;
    }

    /* Bounding box in font units */
    int32_t xMin = (int32_t)ri16(gp + 2);
    int32_t yMin = (int32_t)ri16(gp + 4);
    int32_t xMax = (int32_t)ri16(gp + 6);
    int32_t yMax = (int32_t)ri16(gp + 8);

    g_npts = 0; g_ncont = 0;
    if (!ttf_load_simple_glyph(gp + 10, (uint16_t)ncontours)) return adv;

    /* Pixel bounding box */
    int bw = F16_CEIL(F16_MUL(g_scale, F16(xMax - xMin))) + 1;
    int bh = F16_CEIL(F16_MUL(g_scale, F16(yMax - yMin))) + 1;
    if (bw > TTF_MAX_GLYPH_W) bw = TTF_MAX_GLYPH_W;
    if (bh > TTF_MAX_GLYPH_H) bh = TTF_MAX_GLYPH_H;

    rasterize_glyph(xMin, yMin, bw, bh);

    /* Blit bitmap into framebuffer */
    int asc    = ttf_ascender();
    int lh     = ttf_line_height();
    int blit_y = py + asc - F16_CEIL(F16_MUL(g_scale, F16(yMax)));
    int blit_x = px + F16_FLOOR(F16_MUL(g_scale, F16(xMin)));

    uint32_t pitch32 = pitch / 4;

    /* Fill the entire glyph cell with bg first */
    for (int row = 0; row < lh; row++) {
        uint32_t *dst = fb_addr + (py + row) * pitch32 + px;
        for (int col = 0; col < adv; col++) dst[col] = bg;
    }

    /* Then blit glyph pixels */
    for (int row = 0; row < bh; row++) {
        int dst_row = blit_y + row;
        if (dst_row < py || dst_row >= py + lh) continue;
        uint32_t *dst = fb_addr + dst_row * pitch32;
        for (int col = 0; col < bw; col++) {
            int dst_col = blit_x + col;
            if (dst_col < px || dst_col >= px + adv) continue;
            if (g_bitmap[row][col]) dst[dst_col] = fg;
        }
    }

    return adv;
}
