/*
 * ttf_render.h – Minimal TrueType font rasterizer for BlexOS
 * Copyright (C) 2026 Blex – BOSL License
 *
 * Parses TrueType glyph outlines (glyf table) and rasterizes them
 * into a 1-bpp bitmap using a simple scanline fill.  No heap is used;
 * all working memory is drawn from a fixed static scratch buffer.
 *
 * Usage:
 *   1. Call ttf_init(font_data, font_size) once.
 *   2. Call ttf_set_size(pixel_height) to choose a point size.
 *   3. Call ttf_draw_char(fb_addr, pitch, x, y, codepoint, fg, bg)
 *      to render a single character into the framebuffer.
 *   4. Call ttf_char_width(codepoint) to get the advance width.
 *
 * Supports:
 *   - Simple glyphs (no compound glyph recursion > 1 level)
 *   - On-curve and off-curve (quadratic Bezier) contours
 *   - Horizontal advance width (hmtx)
 *   - ASCII + Latin Extended (codepoints 0x20..0x17E)
 *
 * NOT supported (falls back to bitmap font stub):
 *   - Hinting, kerning, ligatures
 *   - Compound glyphs with more than one level of indirection
 *   - CFF/OTF outlines
 */

#ifndef TTF_RENDER_H
#define TTF_RENDER_H

#include <stdint.h>

/* Maximum glyph bitmap size (cells larger than this fall back to bitmap font) */
#define TTF_MAX_GLYPH_W   64
#define TTF_MAX_GLYPH_H   64

/* ── Public API ──────────────────────────────────────────── */

/*
 * Initialise the TTF engine from a raw TTF file in memory.
 * Returns 1 on success, 0 if the data does not look like a valid TTF.
 */
int ttf_init(const uint8_t *data, uint32_t size);

/*
 * Set the desired pixel height for subsequent glyph rendering.
 * Must be called after ttf_init().
 */
void ttf_set_size(int pixel_height);

/*
 * Render codepoint at (px, py) into the 32bpp framebuffer.
 * pitch is the row stride in bytes.
 * Returns the advance width in pixels.
 */
int ttf_draw_char(uint32_t *fb, uint32_t pitch, int px, int py,
                  uint32_t codepoint, uint32_t fg, uint32_t bg);

/*
 * Return the advance width of codepoint in pixels (current size).
 */
int ttf_char_width(uint32_t codepoint);

/*
 * Return the line height (ascender + descender + line gap) in pixels.
 */
int ttf_line_height(void);

/*
 * Return the ascender in pixels (distance from baseline to top of cell).
 */
int ttf_ascender(void);

#endif /* TTF_RENDER_H */
