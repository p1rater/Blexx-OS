/*
 * stb_image_impl.c – stb_image implementation for BlexOS (freestanding)
 *
 * Compiled as its own translation unit so that the bump allocator and
 * mem shims are self-contained and don't pollute command_logic.c.
 *
 * Add to Makefile OBJS:
 *   OBJS := boot.o kernel.o fb.o commands/command_logic.o \
 *           commands/stb_image_impl.o sata.o es1.o
 *
 * And add the build rule:
 *   commands/stb_image_impl.o: commands/stb_image_impl.c
 *       $(CC_LOG)
 *       @$(CC) $(CFLAGS) -c commands/stb_image_impl.c \
 *           -o commands/stb_image_impl.o 2>.build_err \
 *           || (cat .build_err && exit 1)
 */

#include <stdint.h>
#include <stddef.h>

/* ── Bump allocator ─────────────────────────────────────────
 * 512 KB static arena shared across all stb_image calls.
 * A single fetch call decodes one small PNG then frees it;
 * the arena is reset to base after each top-level alloc cycle
 * by calling stbi_bump_reset() (declared below).
 * ──────────────────────────────────────────────────────────*/
#define BUMP_SIZE (512 * 1024)
static uint8_t  bump_arena[BUMP_SIZE];
static uint32_t bump_pos = 0;

/* Called by fetch_draw_png() after stbi_image_free() to reclaim all
 * arena memory at once — safe because stb_image frees everything
 * before returning from stbi_load_from_memory. */
void stbi_bump_reset(void) { bump_pos = 0; }

static void* bump_alloc(uint32_t sz) {
    /* 8-byte align */
    sz = (sz + 7u) & ~7u;
    if (bump_pos + sz > BUMP_SIZE) return (void*)0;
    void *p = bump_arena + bump_pos;
    bump_pos += sz;
    return p;
}

/* stb_image calls free() on pointers it allocated; with a bump
 * allocator we cannot free individual blocks — stbi_bump_reset()
 * reclaims everything after the decode is complete. */
static void bump_free(void *p) { (void)p; }

static void* bump_realloc(void *old, uint32_t oldsz, uint32_t newsz) {
    void *n = bump_alloc(newsz);
    if (!n) return (void*)0;
    if (old && oldsz) {
        uint8_t *s = (uint8_t*)old, *d = (uint8_t*)n;
        uint32_t copy = oldsz < newsz ? oldsz : newsz;
        for (uint32_t i = 0; i < copy; i++) d[i] = s[i];
    }
    return n;
}

/* ── mem shims (no libc) ────────────────────────────────── */
void* memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}

void* memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void* memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i-1] = s[i-1];
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p = (const uint8_t*)a, *q = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++) {
        if (p[i] < q[i]) return -1;
        if (p[i] > q[i]) return  1;
    }
    return 0;
}

/* ── stb_image configuration ────────────────────────────── */
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ASSERT(x)   /* no-op: no assert in kernel */

#define STBI_MALLOC(sz)              bump_alloc((uint32_t)(sz))
#define STBI_FREE(p)                 bump_free(p)
#define STBI_REALLOC_SIZED(p,os,ns)  bump_realloc(p,(uint32_t)(os),(uint32_t)(ns))
#define STBI_REALLOC(p,ns)           bump_realloc(p,0,(uint32_t)(ns))

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
