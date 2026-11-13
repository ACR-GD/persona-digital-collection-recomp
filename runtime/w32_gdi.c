// GDI32 emulation for an 8-bit palettized 640x480 display.
//
// Every drawing surface lives in guest memory: the screen (one 8-bit buffer whose pixel values
// are hardware palette indices), device-dependent bitmaps (8-bit like the screen, or 1-bit) and
// DIB sections (1/4/8/16/24/32 bpp with their own colour table). Blits run per pixel in the
// destination's value domain (palette indices for 8-bit, RGB for true colour, bits for mono),
// with full ternary raster operations, so the classic sprite tricks (SRCAND/SRCPAINT masks,
// SRCINVERT, palette-index ROP arithmetic) behave like on a 1998 256-colour display.
#include "gdi.h"
#include "font6x8.h"
#include <SDL3/SDL.h>
#include <strings.h>

uint8_t screen_px[SCREEN_W * SCREEN_H];   // host mirror (unused: the screen lives in guest memory)
uint32_t sys_pal[256];
volatile int screen_dirty;
static uint32_t sys_pal_gen = 1;

enum { T_DC = 0x31, T_BMP, T_PAL, T_BRUSH, T_PEN, T_FONT, T_RGN };
#define H(t, i) (((uint32_t)(t) << 24) | (uint32_t)(i))
#define HT(h) ((h) >> 24)
#define HI(h) ((h) & 0xffffffu)

// ---------------------------------------------------------------- objects
struct bmp { int used, w, h, bpp, pitch, topdown, palidx; uint32_t bits; int own, ncol; uint32_t ctab[256]; int is_dib; int sel; };
struct pal { int used, n; uint32_t e[256]; };   // 0xFFBBGGRR with peFlags in the top byte (PALETTEENTRY order r,g,b,f)
struct brush { int used, style; uint32_t color; int hatch; int pbmp; uint8_t pat[8 * 8]; int pat_is_index; uint32_t patc[64]; };
struct pen { int used, style, width; uint32_t color; };
struct font { int used, height, width, weight, italic, underline, charset; char face[32]; };
struct rgn { int used, n, cap; struct rect *r; };
#define NBMP 2048
#define NPAL 64
#define NBR 512
#define NPEN 256
#define NFONT 128
#define NRGN 1024
static struct bmp B[NBMP]; static struct pal P[NPAL]; static struct brush BR[NBR]; static struct pen PN[NPEN];
static struct font FN[NFONT]; static struct rgn RG[NRGN];

struct dcst {
    int bmp; uint32_t hwnd; int client;
    int ox, oy;                    // device origin on the surface (window client position)
    int vpx, vpy, wox, woy;        // viewport / window origins
    uint32_t brush, pen, font, pal, hbmp;
    uint32_t text, bk; int bkmode, rop2, align, stretch, fill;
    int curx, cury, brx, bry;
    int clip;                      // region index (device coords relative to the DC origin) or 0
};
struct dc { int used; struct dcst s; struct dcst stack[16]; int sp; int is_mem; int is_screen; };
#define NDC 256
static struct dc D[NDC];

#define SCREEN_BMP 1
#define MONO1_BMP 2
static uint32_t stock[20];
static int realized_pal;           // palette whose colours are in the hardware palette
static uint32_t screen_addr;

static struct dc *dcget(uint32_t h) { return (HT(h) == T_DC && HI(h) < NDC && D[HI(h)].used) ? &D[HI(h)] : 0; }
static struct bmp *bmpget(uint32_t h) { return (HT(h) == T_BMP && HI(h) < NBMP && B[HI(h)].used) ? &B[HI(h)] : 0; }
static struct pal *palget(uint32_t h) { return (HT(h) == T_PAL && HI(h) < NPAL && P[HI(h)].used) ? &P[HI(h)] : 0; }
static struct brush *brget(uint32_t h) { return (HT(h) == T_BRUSH && HI(h) < NBR && BR[HI(h)].used) ? &BR[HI(h)] : 0; }
static struct pen *penget(uint32_t h) { return (HT(h) == T_PEN && HI(h) < NPEN && PN[HI(h)].used) ? &PN[HI(h)] : 0; }
static struct font *fontget(uint32_t h) { return (HT(h) == T_FONT && HI(h) < NFONT && FN[HI(h)].used) ? &FN[HI(h)] : 0; }
static struct rgn *rgnget(uint32_t h) { return (HT(h) == T_RGN && HI(h) < NRGN && RG[HI(h)].used) ? &RG[HI(h)] : 0; }
#define ALLOC(arr, n, first) ({ int k_ = -1; for (int i_ = (first); i_ < (n); i_++) if (!arr[i_].used) { memset(&arr[i_], 0, sizeof arr[i_]); arr[i_].used = 1; k_ = i_; break; } if (k_ < 0) rt_fatal("out of GDI objects (" #arr ")"); k_; })

// ---------------------------------------------------------------- regions (lists of disjoint rectangles)
static void rg_set(struct rgn *g, const struct rect *r, int n) {
    if (n > g->cap) { g->cap = n + 16; g->r = realloc(g->r, sizeof *g->r * g->cap); }
    memcpy(g->r, r, sizeof *r * n); g->n = n;
}
static int new_rgn(void) { int k = ALLOC(RG, NRGN, 1); RG[k].n = 0; return k; }
static void rg_rect(struct rgn *g, struct rect r) { if (rect_empty(r)) g->n = 0; else rg_set(g, &r, 1); }
static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
// intervals of g covering the band [y0,y1) (rects either cover the band fully or not at all)
static int band_iv(const struct rgn *g, int y0, int y1, int *iv) {
    int n = 0;
    for (int k = 0; k < g->n; k++) if (g->r[k].t <= y0 && g->r[k].b >= y1) { iv[2 * n] = g->r[k].l; iv[2 * n + 1] = g->r[k].r; n++; }
    // sort + merge
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && iv[2 * j] < iv[2 * j - 2]; j--) { int a = iv[2 * j], b = iv[2 * j + 1]; iv[2 * j] = iv[2 * j - 2]; iv[2 * j + 1] = iv[2 * j - 1]; iv[2 * j - 2] = a; iv[2 * j - 1] = b; }
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (m && iv[2 * i] <= iv[2 * m - 1]) { if (iv[2 * i + 1] > iv[2 * m - 1]) iv[2 * m - 1] = iv[2 * i + 1]; }
        else { iv[2 * m] = iv[2 * i]; iv[2 * m + 1] = iv[2 * i + 1]; m++; }
    }
    return m;
}
static int inside_iv(const int *iv, int n, int x) { for (int i = 0; i < n; i++) if (x >= iv[2 * i] && x < iv[2 * i + 1]) return 1; return 0; }
// mode: 1 AND 2 OR 3 XOR 4 DIFF 5 COPY
static void rg_combine(struct rgn *dst, const struct rgn *a, const struct rgn *b, int mode) {
    if (mode == 5) { rg_set(dst, a->r, a->n); return; }
    int ny = 0, *ys = malloc(sizeof(int) * (2 * (a->n + b->n) + 2));
    for (int k = 0; k < a->n; k++) { ys[ny++] = a->r[k].t; ys[ny++] = a->r[k].b; }
    for (int k = 0; k < b->n; k++) { ys[ny++] = b->r[k].t; ys[ny++] = b->r[k].b; }
    qsort(ys, (size_t)ny, sizeof(int), cmp_int);
    int cap = 64, n = 0; struct rect *out = malloc(sizeof *out * cap);
    int *ia = malloc(sizeof(int) * (2 * a->n + 2)), *ib = malloc(sizeof(int) * (2 * b->n + 2));
    int *xs = malloc(sizeof(int) * (2 * (a->n + b->n) + 2));
    for (int i = 0; i + 1 < ny; i++) {
        int y0 = ys[i], y1 = ys[i + 1]; if (y0 == y1) continue;
        int na = band_iv(a, y0, y1, ia), nb = band_iv(b, y0, y1, ib);
        int nx = 0; for (int k = 0; k < 2 * na; k++) xs[nx++] = ia[k]; for (int k = 0; k < 2 * nb; k++) xs[nx++] = ib[k];
        qsort(xs, (size_t)nx, sizeof(int), cmp_int);
        int open = -1;
        for (int j = 0; j < nx; j++) {
            int x = xs[j];
            int ina = inside_iv(ia, na, x), inb = inside_iv(ib, nb, x);
            int in = mode == 1 ? ina && inb : mode == 2 ? ina || inb : mode == 3 ? ina != inb : ina && !inb;
            if (in && open < 0) open = x;
            if (!in && open >= 0) {
                if (x > open) {
                    // merge with the rectangle right above when it has the same span
                    int merged = 0;
                    for (int q = n - 1; q >= 0 && out[q].b >= y0; q--) if (out[q].b == y0 && out[q].l == open && out[q].r == x) { out[q].b = y1; merged = 1; break; }
                    if (!merged) { if (n == cap) { cap *= 2; out = realloc(out, sizeof *out * cap); } out[n++] = (struct rect){ open, y0, x, y1 }; }
                }
                open = -1;
            }
        }
    }
    rg_set(dst, out, n);
    free(ys); free(out); free(ia); free(ib); free(xs);
}
static struct rect rg_box(const struct rgn *g) { struct rect b = { 0, 0, 0, 0 }; for (int k = 0; k < g->n; k++) b = rect_union(b, g->r[k]); return b; }

// ---------------------------------------------------------------- colours
static uint32_t pal_rgb(uint32_t e) { return e & 0xffffffu; }    // PALETTEENTRY as stored: 0x00BBGGRR
static int nearest_in(const uint32_t *tab, int n, uint32_t cr) {   // cr: COLORREF 0x00BBGGRR
    int r = cr & 255, g = (cr >> 8) & 255, b = (cr >> 16) & 255, best = 0; long bd = 1L << 40;
    for (int i = 0; i < n; i++) {
        uint32_t c = tab[i];
        int dr = (int)(c & 255) - r, dg = (int)((c >> 8) & 255) - g, db = (int)((c >> 16) & 255) - b;
        long d = 3L * dr * dr + 4L * dg * dg + 2L * db * db;
        if (d < bd) { bd = d; best = i; if (!d) break; }
    }
    return best;
}
static uint32_t syspal_cr[256];     // hardware palette as COLORREFs
static void syspal_changed(void) {
    for (int i = 0; i < 256; i++) { uint32_t c = syspal_cr[i]; sys_pal[i] = ((c & 255) << 16) | (c & 0xff00) | ((c >> 16) & 255); }
    sys_pal_gen++; screen_dirty = 1;
}
// table of COLORREFs for an 8-bit-or-less surface
static const uint32_t *surf_ctab(struct bmp *b, int *n) {
    if (b->bpp == 8 && !b->is_dib) { *n = 256; return syspal_cr; }
    if (b->palidx) { *n = 256; return syspal_cr; }
    *n = b->ncol ? b->ncol : (1 << b->bpp); return b->ctab;
}
// COLORREF (possibly PALETTEINDEX / DIBINDEX) -> pixel value of surface b, through dc's palette
static uint32_t color_value(struct bmp *b, struct dc *d, uint32_t cr) {
    if (b->bpp == 1) {
        if ((cr >> 24) == 1) { struct pal *p = d ? palget(d->s.pal) : 0; cr = p ? pal_rgb(p->e[(cr & 0xffff) % (uint32_t)p->n]) : 0; }
        if ((cr >> 24) == 0x10) return cr & 1;
        // GDI: a colour matching the background... for mono targets: closer to white -> 1
        int s = (int)(cr & 255) + (int)((cr >> 8) & 255) + (int)((cr >> 16) & 255);
        return s >= 384 ? 1 : 0;
    }
    if (b->bpp <= 8) {
        if ((cr >> 24) == 0x10) return cr & 0xff;                 // DIBINDEX
        if ((cr >> 24) == 1) {                                    // PALETTEINDEX: logical -> system index (identity mapping)
            if (b->bpp == 8 && (!b->is_dib || b->palidx)) return cr & 0xff;
            struct pal *p = d ? palget(d->s.pal) : 0; cr = p ? pal_rgb(p->e[(cr & 0xffff) % (uint32_t)p->n]) : 0;
        }
        int n; const uint32_t *t = surf_ctab(b, &n);
        if ((cr >> 24) == 2) cr &= 0xffffff;                      // PALETTERGB
        return (uint32_t)nearest_in(t, n, cr & 0xffffff);
    }
    if ((cr >> 24) == 1) { struct pal *p = d ? palget(d->s.pal) : 0; cr = p ? pal_rgb(p->e[(cr & 0xffff) % (uint32_t)p->n]) : 0; }
    uint32_t r = cr & 255, g = (cr >> 8) & 255, bl = (cr >> 16) & 255;
    if (b->bpp == 16) return ((r >> 3) << 10) | ((g >> 3) << 5) | (bl >> 3);
    return (r << 16) | (g << 8) | bl;
}
// pixel value of surface b -> COLORREF
static uint32_t value_rgb(struct bmp *b, uint32_t v) {
    if (b->bpp <= 8) { int n; const uint32_t *t = surf_ctab(b, &n); return t[v % (uint32_t)n] & 0xffffff; }
    if (b->bpp == 16) { uint32_t r = (v >> 10) & 31, g = (v >> 5) & 31, bl = v & 31; return (r << 3 | r >> 2) | (g << 3 | g >> 2) << 8 | (bl << 3 | bl >> 2) << 16; }
    return ((v >> 16) & 255) | (v & 0xff00) | ((v & 255) << 16);
}

// ---------------------------------------------------------------- surface pixel access
static inline uint32_t row_addr(struct bmp *b, int y) { return b->bits + (uint32_t)((b->topdown ? y : b->h - 1 - y) * b->pitch); }
static inline uint32_t px_get(struct bmp *b, int x, int y) {
    uint32_t a = row_addr(b, y);
    switch (b->bpp) {
    case 8: return MEM[(a + (uint32_t)x) & MEM_MASK];
    case 1: return (RD8(a + (uint32_t)(x >> 3)) >> (7 - (x & 7))) & 1;
    case 4: { uint8_t v = RD8(a + (uint32_t)(x >> 1)); return (x & 1) ? (v & 15u) : (uint32_t)(v >> 4); }
    case 16: return RD16(a + 2u * (uint32_t)x);
    case 24: { uint32_t p = a + 3u * (uint32_t)x; return RD8(p) | RD8(p + 1) << 8 | (uint32_t)RD8(p + 2) << 16; }
    default: return RD32(a + 4u * (uint32_t)x) & 0xffffff;
    }
}
static inline void px_set(struct bmp *b, int x, int y, uint32_t v) {
    uint32_t a = row_addr(b, y);
    switch (b->bpp) {
    case 8: MEM[(a + (uint32_t)x) & MEM_MASK] = (uint8_t)v; return;
    case 1: { uint32_t p = a + (uint32_t)(x >> 3); uint8_t m = (uint8_t)(0x80 >> (x & 7)); WR8(p, (v & 1) ? (RD8(p) | m) : (RD8(p) & ~m)); return; }
    case 4: { uint32_t p = a + (uint32_t)(x >> 1); uint8_t o = RD8(p); WR8(p, (x & 1) ? ((o & 0xf0) | (v & 15)) : ((o & 0x0f) | ((v & 15) << 4))); return; }
    case 16: WR16(a + 2u * (uint32_t)x, v); return;
    case 24: { uint32_t p = a + 3u * (uint32_t)x; WR8(p, v); WR8(p + 1, v >> 8); WR8(p + 2, v >> 16); return; }
    default: WR32(a + 4u * (uint32_t)x, v); return;
    }
}
static inline uint32_t vmask(int bpp) { return bpp >= 32 ? 0xffffffu : bpp == 24 ? 0xffffffu : (1u << bpp) - 1; }
static inline uint32_t rop3(uint32_t rop, uint32_t P_, uint32_t S, uint32_t D_) {
    switch (rop) {
    case 0xCC: return S; case 0xF0: return P_; case 0x88: return S & D_; case 0xEE: return S | D_; case 0x66: return S ^ D_;
    case 0x00: return 0; case 0xFF: return ~0u; case 0x55: return ~D_; case 0x5A: return P_ ^ D_; case 0x33: return ~S;
    case 0xC0: return P_ & S; case 0x11: return ~(S | D_); case 0x44: return S & ~D_; case 0xBB: return ~S | D_;
    case 0xFB: return P_ | ~S | D_; case 0xAA: return D_; case 0x22: return ~S & D_; case 0xB8: return (P_ & ~S) | (S & D_);
    case 0xE2: return (D_ & ~S) | (P_ & S);
    }
    uint32_t r = 0;
    for (int k = 0; k < 8; k++) if (rop & (1u << k)) {
        uint32_t t = ((k & 4) ? P_ : ~P_) & ((k & 2) ? S : ~S) & ((k & 1) ? D_ : ~D_);
        r |= t;
    }
    return r;
}
static inline uint32_t rop2(int code, uint32_t P_, uint32_t D_) {
    int t = code - 1; uint32_t r = 0;
    if (t & 1) r |= ~P_ & ~D_; if (t & 2) r |= ~P_ & D_; if (t & 4) r |= P_ & ~D_; if (t & 8) r |= P_ & D_;
    return r;
}

// ---------------------------------------------------------------- DC geometry and clipping
static struct bmp *dc_surface(struct dc *d) { return &B[d->s.bmp]; }
static void dc_refresh_origin(struct dc *d) {
    if (d->s.hwnd || d->is_screen) {
        int x, y, w, h; if (user_window_origin(d->s.hwnd, d->s.client, &x, &y, &w, &h)) { d->s.ox = x; d->s.oy = y; }
    }
}
// effective clip rectangles in surface coordinates
static int dc_clip(struct dc *d, struct rect *out, int cap) {
    struct bmp *b = dc_surface(d);
    struct rect lim = { 0, 0, b->w, b->h };
    if (d->s.hwnd || d->is_screen) {
        int x, y, w, h;
        if (user_window_origin(d->s.hwnd, d->s.client, &x, &y, &w, &h)) { d->s.ox = x; d->s.oy = y; lim = rect_isect(lim, (struct rect){ x, y, x + w, y + h }); }
    }
    if (!d->s.clip) { if (rect_empty(lim)) return 0; out[0] = lim; return 1; }
    struct rgn *g = &RG[d->s.clip]; int n = 0;
    for (int k = 0; k < g->n && n < cap; k++) {
        struct rect r = g->r[k]; r.l += d->s.ox; r.r += d->s.ox; r.t += d->s.oy; r.b += d->s.oy;
        r = rect_isect(r, lim); if (!rect_empty(r)) out[n++] = r;
    }
    return n;
}
#define LX(d, x) ((x) - (d)->s.wox + (d)->s.vpx + (d)->s.ox)
#define LY(d, y) ((y) - (d)->s.woy + (d)->s.vpy + (d)->s.oy)
static void mark_dirty(struct dc *d) { if (d->s.bmp == SCREEN_BMP) screen_dirty = 1; }

// brush pattern value at surface pixel (x, y)
struct pat { int kind; uint32_t solid; uint32_t v[64]; int none; };
static void brush_prepare(struct dc *d, struct bmp *b, uint32_t hbr, struct pat *pt) {
    struct brush *br = brget(hbr);
    memset(pt, 0, sizeof *pt);
    if (!br || br->style == 1) { pt->none = 1; return; }
    if (br->style == 0) { pt->solid = color_value(b, d, br->color); return; }
    pt->kind = 1;
    for (int k = 0; k < 64; k++) {
        if (br->style == 2) {   // hatch: foreground = brush colour, background = bk colour
            int x = k & 7, y = k >> 3, on = 0;
            switch (br->hatch) { case 0: on = y == 4; break; case 1: on = x == 4; break; case 2: on = x == y; break; case 3: on = x == 7 - y; break; case 4: on = x == 4 || y == 4; break; default: on = x == y || x == 7 - y; }
            pt->v[k] = color_value(b, d, on ? br->color : d->s.bk);
        } else if (br->pat_is_index) pt->v[k] = br->pat[k];
        else pt->v[k] = color_value(b, d, br->patc[k]);
    }
}
static inline uint32_t pat_at(struct dc *d, struct pat *pt, int x, int y) {
    if (!pt->kind) return pt->solid;
    return pt->v[(((y - d->s.bry) & 7) << 3) | ((x - d->s.brx) & 7)];
}

// ---------------------------------------------------------------- blit core
// translation of source pixel values into the destination's value domain
struct xlat { int kind; uint8_t t8[256]; uint32_t fg, bg; uint32_t srcbk; };
static void xlat_prepare(struct xlat *x, struct bmp *db, struct dc *dd, struct bmp *sb, struct dc *sd) {
    memset(x, 0, sizeof *x);
    if (sb->bpp == 1 && db->bpp != 1) {   // mono -> colour: 0 = text colour, 1 = background colour
        x->kind = 2; x->fg = color_value(db, dd, dd->s.text); x->bg = color_value(db, dd, dd->s.bk); return;
    }
    if (db->bpp == 1 && sb->bpp != 1) {   // colour -> mono: background colour -> 1
        x->kind = 3; x->srcbk = sd ? color_value(sb, sd, sd->s.bk) : value_rgb(sb, 0); return;
    }
    if (sb->bpp == db->bpp && sb->bpp > 8) { x->kind = 0; return; }
    if (sb->bpp <= 8 && db->bpp <= 8) {
        int ns, nd; const uint32_t *ts = surf_ctab(sb, &ns), *td = surf_ctab(db, &nd);
        if ((ts == td && sb->bpp == db->bpp) || (sb->bpp == 8 && db->bpp == 8 && sb->palidx)) { x->kind = 0; return; }
        x->kind = 1;
        int same = ns == nd && !memcmp(ts, td, sizeof(uint32_t) * (size_t)ns);
        for (int i = 0; i < 256; i++) x->t8[i] = same ? (uint8_t)i : (uint8_t)nearest_in(td, nd, ts[i % ns] & 0xffffff);
        if (same && sb->bpp == db->bpp) x->kind = 0;
        return;
    }
    x->kind = 4;    // via RGB
}
static inline uint32_t xlat_px(struct xlat *x, struct bmp *db, struct dc *dd, struct bmp *sb, uint32_t v) {
    switch (x->kind) {
    case 0: return v;
    case 1: return x->t8[v & 255];
    case 2: return v ? x->bg : x->fg;
    case 3: return v == x->srcbk ? 1u : 0u;
    default: return color_value(db, dd, value_rgb(sb, v));
    }
}
static int rop_uses_src(uint32_t r) { return ((r >> 2) & 0x33) != (r & 0x33); }
static int rop_uses_pat(uint32_t r) { return ((r >> 4) & 0x0f) != (r & 0x0f); }
// dst rect (surface coords, w x h) from src rect (sw x sh, src surface coords); src may be NULL
static void blit(struct dc *dd, int dx, int dy, int dw, int dh, struct bmp *sb, struct dc *sd, int sx, int sy, int sw, int sh, uint32_t rop) {
    struct bmp *db = dc_surface(dd);
    uint32_t r = (rop >> 16) & 0xff;
    int mirror_x = 0, mirror_y = 0;
    if (dw < 0) { dx += dw; dw = -dw; mirror_x ^= 1; }
    if (dh < 0) { dy += dh; dh = -dh; mirror_y ^= 1; }
    if (sw < 0) { sx += sw; sw = -sw; mirror_x ^= 1; }
    if (sh < 0) { sy += sh; sh = -sh; mirror_y ^= 1; }
    if (!dw || !dh) return;
    if (!rop_uses_src(r)) sb = 0;
    if (sb && (!sw || !sh)) return;
    struct rect cl[64]; int ncl = dc_clip(dd, cl, 64);
    struct xlat xl; if (sb) xlat_prepare(&xl, db, dd, sb, sd);
    struct pat pt; int upat = rop_uses_pat(r);
    if (upat) { brush_prepare(dd, db, dd->s.brush, &pt); if (pt.none) { pt.kind = 0; pt.solid = 0; } }
    uint32_t m = vmask(db->bpp);
    int stretch = sb && (sw != dw || sh != dh);
    for (int c = 0; c < ncl; c++) {
        struct rect a = rect_isect(cl[c], (struct rect){ dx, dy, dx + dw, dy + dh });
        if (rect_empty(a)) continue;
        // fast path: plain 8-bit copy
        if (sb && r == 0xCC && !stretch && !mirror_x && !mirror_y && xl.kind == 0 && sb->bpp == 8 && db->bpp == 8) {
            for (int y = a.t; y < a.b; y++) {
                int syy = sy + (y - dy); if (syy < 0 || syy >= sb->h) continue;
                int x0 = a.l, x1 = a.r, sx0 = sx + (x0 - dx);
                if (sx0 < 0) { x0 -= sx0; sx0 = 0; }
                if (sx0 + (x1 - x0) > sb->w) x1 = x0 + (sb->w - sx0);
                if (x1 <= x0) continue;
                memmove(MEM + ((row_addr(db, y) + (uint32_t)x0) & MEM_MASK), MEM + ((row_addr(sb, syy) + (uint32_t)sx0) & MEM_MASK), (size_t)(x1 - x0));
            }
            continue;
        }
        for (int y = a.t; y < a.b; y++) {
            int ry = y - dy; if (mirror_y) ry = dh - 1 - ry;
            int syy = sb ? sy + (stretch ? (int)((int64_t)ry * sh / dh) : ry) : 0;
            if (sb && (syy < 0 || syy >= sb->h)) continue;
            for (int x = a.l; x < a.r; x++) {
                int rx = x - dx; if (mirror_x) rx = dw - 1 - rx;
                uint32_t S = 0;
                if (sb) {
                    int sxx = sx + (stretch ? (int)((int64_t)rx * sw / dw) : rx);
                    if (sxx < 0 || sxx >= sb->w) continue;
                    S = xlat_px(&xl, db, dd, sb, px_get(sb, sxx, syy));
                }
                uint32_t Dv = (r == 0xCC || r == 0xF0 || r == 0x00 || r == 0xFF || r == 0x33) ? 0 : px_get(db, x, y);
                uint32_t Pv = upat ? pat_at(dd, &pt, x, y) : 0;
                px_set(db, x, y, rop3(r, Pv, S, Dv) & m);
            }
        }
    }
    mark_dirty(dd);
}
// fill a surface rectangle (already in surface coords) with the DC's brush or a given brush, ROP2 aware
static void fill_rect_surf(struct dc *d, struct rect a, uint32_t hbr, int rop2code) {
    struct bmp *b = dc_surface(d);
    struct pat pt; brush_prepare(d, b, hbr, &pt); if (pt.none) return;
    struct rect cl[64]; int n = dc_clip(d, cl, 64);
    uint32_t m = vmask(b->bpp);
    for (int c = 0; c < n; c++) {
        struct rect q = rect_isect(cl[c], a); if (rect_empty(q)) continue;
        if (!pt.kind && rop2code == 13 && b->bpp == 8) {
            for (int y = q.t; y < q.b; y++) memset(MEM + ((row_addr(b, y) + (uint32_t)q.l) & MEM_MASK), (int)pt.solid, (size_t)(q.r - q.l));
            continue;
        }
        for (int y = q.t; y < q.b; y++) for (int x = q.l; x < q.r; x++) {
            uint32_t Pv = pat_at(d, &pt, x, y);
            px_set(b, x, y, (rop2code == 13 ? Pv : rop2(rop2code, Pv, px_get(b, x, y))) & m);
        }
    }
    mark_dirty(d);
}
static void plot(struct dc *d, struct rect *cl, int ncl, int x, int y, uint32_t v) {
    struct bmp *b = dc_surface(d);
    for (int c = 0; c < ncl; c++) if (x >= cl[c].l && x < cl[c].r && y >= cl[c].t && y < cl[c].b) {
        px_set(b, x, y, (d->s.rop2 == 13 ? v : rop2(d->s.rop2, v, px_get(b, x, y))) & vmask(b->bpp));
        return;
    }
}
static void line_surf(struct dc *d, int x0, int y0, int x1, int y1) {
    struct pen *p = penget(d->s.pen); if (!p || p->style == 5) return;
    struct bmp *b = dc_surface(d);
    uint32_t v = color_value(b, d, p->color);
    struct rect cl[64]; int n = dc_clip(d, cl, 64);
    int w = p->width > 1 ? p->width : 1;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        if (x0 == x1 && y0 == y1) break;     // the last point is not drawn
        for (int a = 0; a < w; a++) for (int c = 0; c < w; c++) plot(d, cl, n, x0 - w / 2 + a, y0 - w / 2 + c, v);
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    mark_dirty(d);
}
// scanline polygon fill (surface coords)
static void poly_fill(struct dc *d, const int *pts, const int *counts, int npoly, uint32_t hbr) {
    int ymin = 1 << 30, ymax = -(1 << 30), tot = 0;
    for (int k = 0; k < npoly; k++) tot += counts[k];
    for (int k = 0; k < tot; k++) { if (pts[2 * k + 1] < ymin) ymin = pts[2 * k + 1]; if (pts[2 * k + 1] > ymax) ymax = pts[2 * k + 1]; }
    int *xs = malloc(sizeof(int) * (size_t)(tot + 2));
    for (int y = ymin; y < ymax; y++) {
        int nx = 0, base = 0; double yc = y + 0.5;
        for (int pp = 0; pp < npoly; pp++) {
            for (int k = 0; k < counts[pp]; k++) {
                int a = base + k, b = base + (k + 1) % counts[pp];
                double x0 = pts[2 * a], y0 = pts[2 * a + 1], x1 = pts[2 * b], y1 = pts[2 * b + 1];
                if ((y0 <= yc && y1 > yc) || (y1 <= yc && y0 > yc)) xs[nx++] = (int)(x0 + (yc - y0) * (x1 - x0) / (y1 - y0) + 0.5);
            }
            base += counts[pp];
        }
        qsort(xs, (size_t)nx, sizeof(int), cmp_int);
        for (int k = 0; k + 1 < nx; k += 2) fill_rect_surf(d, (struct rect){ xs[k], y, xs[k + 1], y + 1 }, hbr, d->s.rop2);
    }
    free(xs);
}
static void ellipse_surf(struct dc *d, struct rect r, int fill, int frame) {
    double cx = (r.l + r.r) / 2.0, cy = (r.t + r.b) / 2.0, rx = (r.r - r.l) / 2.0, ry = (r.b - r.t) / 2.0;
    if (rx <= 0 || ry <= 0) return;
    for (int y = r.t; y < r.b; y++) {
        double t = (y + 0.5 - cy) / ry; if (fabs(t) > 1) continue;
        double hw = rx * sqrt(1 - t * t);
        int x0 = (int)floor(cx - hw + 0.5), x1 = (int)floor(cx + hw + 0.5);
        if (fill) fill_rect_surf(d, (struct rect){ x0, y, x1, y + 1 }, d->s.brush, d->s.rop2);
        if (frame) {
            struct pen *p = penget(d->s.pen);
            if (p && p->style != 5) {
                struct rect cl[64]; int n = dc_clip(d, cl, 64); uint32_t v = color_value(dc_surface(d), d, p->color);
                plot(d, cl, n, x0, y, v); plot(d, cl, n, x1 - 1, y, v);
            }
        }
    }
    mark_dirty(d);
}

// ---------------------------------------------------------------- bitmaps / DIBs
static int new_bmp(int w, int h, int bpp, int topdown, uint32_t bits) {
    int k = ALLOC(B, NBMP, 3);
    struct bmp *b = &B[k];
    b->w = w; b->h = h; b->bpp = bpp; b->topdown = topdown;
    b->pitch = topdown ? (((w * bpp + 15) / 16) * 2) : (((w * bpp + 31) / 32) * 4);
    if (bits) b->bits = bits; else { b->bits = gmem_alloc((uint32_t)(b->pitch * (h ? h : 1)) + 4); b->own = 1; }
    return k;
}
static void read_ctab(struct bmp *b, uint32_t bmi, uint32_t usage, struct dc *d) {
    uint32_t hs = RD32(bmi); int bpp = RD16(bmi + 14);
    int n = (int)RD32(bmi + 32); if (!n && bpp <= 8) n = 1 << bpp;
    if (bpp > 8) n = 0;
    b->ncol = n;
    if (usage == 1) {   // DIB_PAL_COLORS: WORD indices into the DC's logical palette (== system indices here)
        b->palidx = bpp == 8;
        struct pal *p = d ? palget(d->s.pal) : 0;
        for (int i = 0; i < n; i++) { uint16_t ix = RD16(bmi + hs + 2u * (uint32_t)i); b->ctab[i] = p ? pal_rgb(p->e[ix % (uint32_t)p->n]) : syspal_cr[ix & 255]; }
        if (b->palidx) { int ident = 1; for (int i = 0; i < n; i++) if (RD16(bmi + hs + 2u * (uint32_t)i) != i) ident = 0; b->palidx = ident; }
    } else {
        for (int i = 0; i < n; i++) { uint32_t q = RD32(bmi + hs + 4u * (uint32_t)i); b->ctab[i] = ((q >> 16) & 255) | (q & 0xff00) | ((q & 255) << 16); }
    }
}
// a temporary bitmap view over a packed/separate DIB (no copy)
static int dib_view(uint32_t bmi, uint32_t bits, uint32_t usage, struct dc *d) {
    int w = (int32_t)RD32(bmi + 4), h = (int32_t)RD32(bmi + 8), bpp = RD16(bmi + 14);
    int k = new_bmp(w, h < 0 ? -h : h, bpp, 0, bits ? bits : 1);
    struct bmp *b = &B[k];
    b->pitch = ((w * bpp + 31) / 32) * 4; b->topdown = h < 0; b->is_dib = 1; b->own = 0;
    read_ctab(b, bmi, usage, d);
    if (!bits) { uint32_t hs = RD32(bmi); uint32_t nc = (uint32_t)b->ncol; b->bits = bmi + hs + nc * (usage == 1 ? 2u : 4u); if (RD32(bmi + 16) == 3) b->bits += 12; }
    return k;
}
static void free_bmp(int k) { if (B[k].own) gmem_free(B[k].bits); B[k].used = 0; }
uint32_t gdi_bitmap_from_dib(uint32_t bmi, uint32_t bits) {
    int v = dib_view(bmi, bits, 0, 0);
    int k = new_bmp(B[v].w, B[v].h, B[v].bpp == 1 ? 1 : 8, 1, 0);
    struct dc tmp; memset(&tmp, 0, sizeof tmp); tmp.used = 1; tmp.s.bmp = k; tmp.s.text = 0; tmp.s.bk = 0xffffff; tmp.s.rop2 = 13;
    blit(&tmp, 0, 0, B[v].w, B[v].h, &B[v], 0, 0, 0, B[v].w, B[v].h, 0xCC0020);
    free_bmp(v);
    return H(T_BMP, k);
}

// ---------------------------------------------------------------- DCs
static int new_dc(void) {
    int k = ALLOC(D, NDC, 1);
    struct dc *d = &D[k];
    d->s.text = 0; d->s.bk = 0xffffff; d->s.bkmode = 2; d->s.rop2 = 13; d->s.stretch = 1; d->s.fill = 1;
    d->s.brush = stock[0]; d->s.pen = stock[7]; d->s.font = stock[13]; d->s.pal = stock[15];
    return k;
}
uint32_t gdi_window_dc(uint32_t hwnd, int client) {
    int k = new_dc(); struct dc *d = &D[k];
    d->s.bmp = SCREEN_BMP; d->s.hwnd = hwnd; d->s.client = client; d->is_screen = 1;
    dc_refresh_origin(d);
    return H(T_DC, k);
}
void gdi_release_dc(uint32_t h) {
    struct dc *d = dcget(h); if (!d || !d->is_screen) return;
    if (d->s.clip) RG[d->s.clip].used = 0;
    d->used = 0;
}
static uint32_t g_CreateCompatibleDC(void) {
    int k = new_dc(); struct dc *d = &D[k];
    d->s.bmp = MONO1_BMP; d->s.hbmp = H(T_BMP, MONO1_BMP); d->is_mem = 1;
    return H(T_DC, k);
}
static uint32_t g_CreateICA(void) { uint32_t h = gdi_window_dc(0, 1); return h; }
static uint32_t g_DeleteDC(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    if (d->s.hbmp && bmpget(d->s.hbmp)) B[HI(d->s.hbmp)].sel = 0;
    if (d->s.clip) RG[d->s.clip].used = 0;
    d->used = 0; return 1;
}
static uint32_t g_SaveDC(void) {
    struct dc *d = dcget(ARG(0)); if (!d || d->sp >= 16) return 0;
    d->stack[d->sp] = d->s;
    if (d->s.clip) { int k = new_rgn(); rg_set(&RG[k], RG[d->s.clip].r, RG[d->s.clip].n); d->stack[d->sp].clip = k; }
    return (uint32_t)++d->sp;
}
static uint32_t g_RestoreDC(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    int lvl = (int32_t)ARG(1); int to = lvl < 0 ? d->sp + lvl : lvl - 1;
    if (to < 0 || to >= d->sp) return 0;
    if (d->s.clip) RG[d->s.clip].used = 0;
    for (int k = to + 1; k < d->sp; k++) if (d->stack[k].clip) RG[d->stack[k].clip].used = 0;
    d->s = d->stack[to]; d->sp = to;
    if (bmpget(d->s.hbmp)) d->s.bmp = (int)HI(d->s.hbmp);
    return 1;
}
static uint32_t g_SelectObject(void) {
    struct dc *d = dcget(ARG(0)); uint32_t h = ARG(1); if (!d) return 0;
    uint32_t old = 0;
    switch (HT(h)) {
    case T_BRUSH: old = d->s.brush; d->s.brush = h; break;
    case T_PEN: old = d->s.pen; d->s.pen = h; break;
    case T_FONT: old = d->s.font; d->s.font = h; break;
    case T_BMP: {
        if (!d->is_mem || !bmpget(h)) return 0;
        old = d->s.hbmp; if (bmpget(old)) B[HI(old)].sel = 0;
        d->s.hbmp = h; d->s.bmp = (int)HI(h); B[HI(h)].sel = 1;
        break;
    }
    case T_RGN: {
        struct rgn *g = rgnget(h); if (!g) return 0;
        if (!d->s.clip) d->s.clip = new_rgn();
        rg_set(&RG[d->s.clip], g->r, g->n);
        return g->n > 1 ? 3 : g->n ? 2 : 1;
    }
    default: return 0;
    }
    return old;
}
static uint32_t g_GetStockObject(void) { uint32_t i = ARG(0); return i < 20 ? stock[i] : 0; }
static uint32_t g_DeleteObject(void) {
    uint32_t h = ARG(0); int i = (int)HI(h);
    for (int k = 0; k < 20; k++) if (stock[k] == h) return 1;
    switch (HT(h)) {
    case T_BMP: if (bmpget(h) && i > MONO1_BMP && !B[i].sel) free_bmp(i); break;
    case T_PAL: if (palget(h)) { P[i].used = 0; if (realized_pal == i) realized_pal = 0; } break;
    case T_BRUSH: if (brget(h)) BR[i].used = 0; break;
    case T_PEN: if (penget(h)) PN[i].used = 0; break;
    case T_FONT: if (fontget(h)) FN[i].used = 0; break;
    case T_RGN: if (rgnget(h)) RG[i].used = 0; break;
    }
    return 1;
}
static uint32_t g_GetObjectA(void) {
    uint32_t h = ARG(0), n = ARG(1), p = ARG(2);
    switch (HT(h)) {
    case T_BMP: {
        struct bmp *b = bmpget(h); if (!b) return 0;
        if (!p) return b->is_dib ? 84 : 24;
        uint8_t tmp[84]; memset(tmp, 0, sizeof tmp);
        uint32_t v[6] = { 0, (uint32_t)b->w, (uint32_t)b->h, (uint32_t)b->pitch, 0, b->is_dib ? b->bits : 0 };
        memcpy(tmp, v, 16); tmp[16] = 1; tmp[18] = (uint8_t)b->bpp; memcpy(tmp + 20, &v[5], 4);
        if (b->is_dib) {
            uint32_t bih[10] = { 40, (uint32_t)b->w, (uint32_t)(b->topdown ? -b->h : b->h), (uint32_t)(1 | (b->bpp << 16)), 0, (uint32_t)(b->pitch * b->h), 0, 0, (uint32_t)b->ncol, 0 };
            memcpy(tmp + 24, bih, 40);
        }
        uint32_t c = n < 84 ? n : 84; memcpy(GP(p), tmp, c); return c;
    }
    case T_PAL: { struct pal *pl = palget(h); if (!pl) return 0; if (p && n >= 2) WR16(p, (uint32_t)pl->n); return 2; }
    case T_BRUSH: { struct brush *br = brget(h); if (!br) return 0; if (p && n >= 12) { WR32(p, (uint32_t)br->style); WR32(p + 4, br->color); WR32(p + 8, (uint32_t)br->hatch); } return 12; }
    case T_PEN: { struct pen *pn = penget(h); if (!pn) return 0; if (p && n >= 16) { WR32(p, (uint32_t)pn->style); WR32(p + 4, (uint32_t)pn->width); WR32(p + 8, 0); WR32(p + 12, pn->color); } return 16; }
    case T_FONT: { struct font *f = fontget(h); if (!f) return 0; if (p && n >= 60) { memset(GP(p), 0, 60); WR32(p, (uint32_t)f->height); WR32(p + 4, (uint32_t)f->width); WR32(p + 16, (uint32_t)f->weight); WR8(p + 20, (uint32_t)f->italic); WR8(p + 23, (uint32_t)f->charset); gstr_put(p + 28, f->face); } return 60; }
    }
    return 0;
}
static uint32_t g_CreateCompatibleBitmap(void) {
    struct dc *d = dcget(ARG(0));
    int w = (int32_t)ARG(1), h = (int32_t)ARG(2);
    int bpp = (d && d->is_mem && B[d->s.bmp].bpp == 1) ? 1 : 8;
    if (d && d->is_mem && B[d->s.bmp].is_dib) bpp = B[d->s.bmp].bpp == 1 ? 1 : 8;
    return H(T_BMP, new_bmp(w > 0 ? w : 1, h > 0 ? h : 1, bpp, 1, 0));
}
static uint32_t g_CreateBitmap(void) {
    int w = (int32_t)ARG(0), h = (int32_t)ARG(1), bpp = (int)(ARG(2) * ARG(3));
    if (bpp != 1) bpp = 8;
    int k = new_bmp(w > 0 ? w : 1, h > 0 ? h : 1, bpp, 1, 0);
    if (ARG(4)) memcpy(GP(B[k].bits), GP(ARG(4)), (size_t)(B[k].pitch * B[k].h));
    return H(T_BMP, k);
}
static uint32_t g_CreateDIBSection(void) {
    struct dc *d = dcget(ARG(0)); uint32_t bmi = ARG(1), usage = ARG(2);
    int w = (int32_t)RD32(bmi + 4), h = (int32_t)RD32(bmi + 8), bpp = RD16(bmi + 14);
    int k = new_bmp(w, h < 0 ? -h : h, bpp, 0, 0);
    struct bmp *b = &B[k];
    gmem_free(b->bits);
    b->pitch = ((w * bpp + 31) / 32) * 4; b->topdown = h < 0; b->is_dib = 1;
    b->bits = gmem_alloc((uint32_t)(b->pitch * b->h) + 4); b->own = 1;
    read_ctab(b, bmi, usage, d);
    if (ARG(3)) WR32(ARG(3), b->bits);
    LOG("CreateDIBSection %dx%d %dbpp usage %u -> bits %08x palidx %d\n", w, h, bpp, usage, b->bits, b->palidx);
    return H(T_BMP, k);
}
static uint32_t g_SetDIBColorTable(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    struct bmp *b = dc_surface(d); uint32_t s = ARG(1), n = ARG(2), p = ARG(3);
    for (uint32_t i = 0; i < n && s + i < 256; i++) { uint32_t q = RD32(p + 4 * i); b->ctab[s + i] = ((q >> 16) & 255) | (q & 0xff00) | ((q & 255) << 16); }
    b->palidx = 0;
    return n;
}
static uint32_t g_GetDIBColorTable(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    struct bmp *b = dc_surface(d); int nn; const uint32_t *t = surf_ctab(b, &nn);
    uint32_t s = ARG(1), n = ARG(2), p = ARG(3);
    for (uint32_t i = 0; i < n && s + i < (uint32_t)nn; i++) { uint32_t c = t[s + i]; WR32(p + 4 * i, ((c & 255) << 16) | (c & 0xff00) | ((c >> 16) & 255)); }
    return n;
}
// GetDIBits(hdc, hbm, start, lines, bits, bmi, usage)
static uint32_t g_GetDIBits(void) {
    struct dc *d = dcget(ARG(0)); struct bmp *b = bmpget(ARG(1)); if (!b) return 0;
    uint32_t bmi = ARG(5);
    if (!RD16(bmi + 14) || !ARG(4)) {
        int bpp = RD16(bmi + 14) ? RD16(bmi + 14) : (b->bpp == 1 ? 1 : 8);
        WR32(bmi + 4, (uint32_t)b->w); WR32(bmi + 8, (uint32_t)b->h); WR16(bmi + 12, 1); WR16(bmi + 14, (uint32_t)bpp);
        WR32(bmi + 16, 0); WR32(bmi + 20, (uint32_t)(((b->w * bpp + 31) / 32) * 4 * b->h)); WR32(bmi + 32, 0);
        if (!ARG(4)) return (uint32_t)b->h;
    }
    int v = dib_view(bmi, ARG(4), ARG(6), d);
    struct bmp *vb = &B[v];
    // the colour table of the output is the source's
    if (vb->bpp <= 8) {
        int nn; const uint32_t *t = surf_ctab(b, &nn); uint32_t hs = RD32(bmi);
        for (int i = 0; i < (1 << vb->bpp) && i < 256; i++) { uint32_t c = t[i % nn]; if (ARG(6) == 1) WR16(bmi + hs + 2u * i, (uint32_t)i); else WR32(bmi + hs + 4u * i, ((c & 255) << 16) | (c & 0xff00) | ((c >> 16) & 255)); vb->ctab[i] = c; }
        vb->ncol = 1 << vb->bpp; vb->palidx = b->bpp == 8 && ARG(6) == 1;
    }
    int start = (int)ARG(2), lines = (int)ARG(3);
    struct dc tmp; memset(&tmp, 0, sizeof tmp); tmp.used = 1; tmp.s.bmp = v; tmp.s.bk = 0xffffff; tmp.s.rop2 = 13;
    // rows [start, start+lines) counted from the bottom of the DIB
    int top = vb->h - start - lines;
    int bs = b->h - start - lines;
    blit(&tmp, 0, top, b->w, lines, b, 0, 0, bs, b->w, lines, 0xCC0020);
    free_bmp(v);
    return (uint32_t)lines;
}
// SetDIBits(hdc, hbm, start, lines, bits, bmi, usage)
static uint32_t g_SetDIBits(void) {
    struct dc *d = dcget(ARG(0)); struct bmp *b = bmpget(ARG(1)); if (!b) return 0;
    int v = dib_view(ARG(5), ARG(4), ARG(6), d);
    int start = (int)ARG(2), lines = (int)ARG(3);
    struct dc tmp; memset(&tmp, 0, sizeof tmp); tmp.used = 1; tmp.s.bmp = (int)HI(ARG(1)); tmp.s.text = 0; tmp.s.bk = 0xffffff; tmp.s.rop2 = 13; tmp.s.pal = d ? d->s.pal : 0;
    B[v].h = lines;   // the buffer holds `lines` scans starting at `start` from the bottom
    blit(&tmp, 0, b->h - start - lines, b->w, lines, &B[v], 0, 0, 0, b->w, lines, 0xCC0020);
    free_bmp(v);
    return (uint32_t)lines;
}
static uint32_t g_CreateDIBitmap(void) {
    struct dc *d = dcget(ARG(0)); uint32_t bih = ARG(1);
    int w = (int32_t)RD32(bih + 4), h = (int32_t)RD32(bih + 8); if (h < 0) h = -h;
    int bpp = RD16(bih + 14) == 1 ? 1 : 8;
    int k = new_bmp(w, h, bpp, 1, 0);
    if (ARG(2) & 4) {
        int v = dib_view(ARG(4), ARG(3), ARG(5), d);
        struct dc tmp; memset(&tmp, 0, sizeof tmp); tmp.used = 1; tmp.s.bmp = k; tmp.s.bk = 0xffffff; tmp.s.rop2 = 13; tmp.s.pal = d ? d->s.pal : 0;
        blit(&tmp, 0, 0, w, h, &B[v], 0, 0, 0, w, h, 0xCC0020);
        free_bmp(v);
    }
    return H(T_BMP, k);
}
static uint32_t g_GetBitmapBits(void) { struct bmp *b = bmpget(ARG(0)); if (!b) return 0; uint32_t n = ARG(1), t = (uint32_t)(b->pitch * b->h); if (n > t) n = t; memcpy(GP(ARG(2)), GP(b->bits), n); return n; }
static uint32_t g_SetBitmapBits(void) { struct bmp *b = bmpget(ARG(0)); if (!b) return 0; uint32_t n = ARG(1), t = (uint32_t)(b->pitch * b->h); if (n > t) n = t; memcpy(GP(b->bits), GP(ARG(2)), n); return n; }

// ---------------------------------------------------------------- blits (API)
static uint32_t g_BitBlt(void) {
    struct dc *d = dcget(ARG(0)), *s = dcget(ARG(5)); if (!d) return 0;
    if (s) dc_refresh_origin(s);
    blit(d, LX(d, (int32_t)ARG(1)), LY(d, (int32_t)ARG(2)), (int32_t)ARG(3), (int32_t)ARG(4),
         s ? dc_surface(s) : 0, s, s ? LX(s, (int32_t)ARG(6)) : 0, s ? LY(s, (int32_t)ARG(7)) : 0, (int32_t)ARG(3), (int32_t)ARG(4), ARG(8));
    return 1;
}
static uint32_t g_StretchBlt(void) {
    struct dc *d = dcget(ARG(0)), *s = dcget(ARG(5)); if (!d) return 0;
    if (s) dc_refresh_origin(s);
    blit(d, LX(d, (int32_t)ARG(1)), LY(d, (int32_t)ARG(2)), (int32_t)ARG(3), (int32_t)ARG(4),
         s ? dc_surface(s) : 0, s, s ? LX(s, (int32_t)ARG(6)) : 0, s ? LY(s, (int32_t)ARG(7)) : 0, (int32_t)ARG(8), (int32_t)ARG(9), ARG(10));
    return 1;
}
static uint32_t g_PatBlt(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    blit(d, LX(d, (int32_t)ARG(1)), LY(d, (int32_t)ARG(2)), (int32_t)ARG(3), (int32_t)ARG(4), 0, 0, 0, 0, 0, 0, ARG(5));
    return 1;
}
// StretchDIBits(hdc, xd, yd, wd, hd, xs, ys, ws, hs, bits, bmi, usage, rop)
static uint32_t g_StretchDIBits(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    int v = dib_view(ARG(10), ARG(9), ARG(11), d);
    struct bmp *vb = &B[v];
    int xs = (int32_t)ARG(5), ys = (int32_t)ARG(6), ws = (int32_t)ARG(7), hs = (int32_t)ARG(8);
    if (!vb->topdown) ys = vb->h - ys - hs;    // bottom-up DIB: source origin is the lower-left corner
    blit(d, LX(d, (int32_t)ARG(1)), LY(d, (int32_t)ARG(2)), (int32_t)ARG(3), (int32_t)ARG(4), vb, 0, xs, ys, ws, hs, ARG(12));
    free_bmp(v);
    return (uint32_t)hs;
}
// SetDIBitsToDevice(hdc, xd, yd, w, h, xs, ys, startscan, nscans, bits, bmi, usage)
static uint32_t g_SetDIBitsToDevice(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    int v = dib_view(ARG(10), ARG(9), ARG(11), d);
    struct bmp *vb = &B[v];
    int w = (int32_t)ARG(3), h = (int32_t)ARG(4), xs = (int32_t)ARG(5), ys = (int32_t)ARG(6);
    int start = (int32_t)ARG(7), ns = (int32_t)ARG(8);
    int sy;
    if (vb->topdown) sy = ys - start;
    else { vb->h = ns; sy = start + ns - ys - h; }
    blit(d, LX(d, (int32_t)ARG(1)), LY(d, (int32_t)ARG(2)), w, h, vb, 0, xs, sy, w, h, 0xCC0020);
    free_bmp(v);
    return (uint32_t)ns;
}

// ---------------------------------------------------------------- palettes
static uint32_t g_CreatePalette(void) {
    uint32_t lp = ARG(0); int n = RD16(lp + 2); if (n > 256) n = 256;
    int k = ALLOC(P, NPAL, 1); P[k].n = n;
    for (int i = 0; i < n; i++) P[k].e[i] = RD32(lp + 4 + 4u * (uint32_t)i);
    return H(T_PAL, k);
}
static uint32_t g_SetPaletteEntries(void) {
    struct pal *p = palget(ARG(0)); if (!p) return 0;
    uint32_t s = ARG(1), n = ARG(2);
    for (uint32_t i = 0; i < n && s + i < 256; i++) p->e[s + i] = RD32(ARG(3) + 4 * i);
    if (s + n > (uint32_t)p->n) p->n = (int)(s + n > 256 ? 256 : s + n);
    return n;
}
static uint32_t g_GetPaletteEntries(void) {
    struct pal *p = palget(ARG(0)); if (!p) return 0;
    uint32_t s = ARG(1), n = ARG(2);
    if (!ARG(3)) return (uint32_t)p->n;
    for (uint32_t i = 0; i < n && s + i < 256; i++) WR32(ARG(3) + 4 * i, p->e[s + i]);
    return n;
}
static uint32_t g_AnimatePalette(void) {
    struct pal *p = palget(ARG(0)); if (!p) return 0;
    uint32_t s = ARG(1), n = ARG(2);
    for (uint32_t i = 0; i < n && s + i < 256; i++) {
        uint32_t e = RD32(ARG(3) + 4 * i);
        p->e[s + i] = e;
        if (realized_pal == (int)HI(ARG(0))) syspal_cr[s + i] = e & 0xffffff;
    }
    if (realized_pal == (int)HI(ARG(0))) syspal_changed();
    return 1;
}
static uint32_t g_SelectPalette(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    uint32_t o = d->s.pal; d->s.pal = ARG(1); return o;
}
static uint32_t g_RealizePalette(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    struct pal *p = palget(d->s.pal); if (!p) return 0;
    if (HI(d->s.pal) == HI(stock[15])) return 0;
    if (!d->is_screen) return (uint32_t)p->n;
    int changed = 0;
    for (int i = 0; i < p->n; i++) {
        uint32_t f = p->e[i] >> 24;
        uint32_t c = (f & 2) ? syspal_cr[p->e[i] & 0xff] : (p->e[i] & 0xffffff);   // PC_EXPLICIT
        if (syspal_cr[i] != c) { syspal_cr[i] = c; changed++; }
    }
    realized_pal = (int)HI(d->s.pal);
    if (changed) syspal_changed();
    LOG("RealizePalette %x: %d entries changed\n", d->s.pal, changed);
    return (uint32_t)changed;
}
static uint32_t g_GetSystemPaletteEntries(void) {
    uint32_t s = ARG(1), n = ARG(2), p = ARG(3);
    if (!p) return 256;
    for (uint32_t k = 0; k < n && s + k < 256; k++) WR32(p + 4 * k, syspal_cr[s + k]);
    return n;
}
static uint32_t sys_pal_use = 1;
static uint32_t g_SetSystemPaletteUse(void) { uint32_t o = sys_pal_use; sys_pal_use = ARG(1); return o; }
static uint32_t g_GetNearestColor(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return ARG(1);
    struct bmp *b = dc_surface(d);
    return value_rgb(b, color_value(b, d, ARG(1)));
}
static uint32_t g_UnrealizeObject(void) { return 1; }

// ---------------------------------------------------------------- brushes / pens
static uint32_t new_brush(int style, uint32_t color, int hatch) { int k = ALLOC(BR, NBR, 1); BR[k].style = style; BR[k].color = color; BR[k].hatch = hatch; return H(T_BRUSH, k); }
static uint32_t g_CreateSolidBrush(void) { return new_brush(0, ARG(0), 0); }
static uint32_t g_CreateBrushIndirect(void) { uint32_t p = ARG(0); return new_brush((int)RD32(p), RD32(p + 4), (int)RD32(p + 8)); }
static void brush_from_bmp(struct brush *br, struct bmp *b) {
    br->style = 3;
    if (b->bpp == 8 && (!b->is_dib || b->palidx)) { br->pat_is_index = 1; for (int k = 0; k < 64; k++) br->pat[k] = (uint8_t)px_get(b, (k & 7) % b->w, (k >> 3) % b->h); }
    else if (b->bpp == 1) { for (int k = 0; k < 64; k++) br->patc[k] = px_get(b, (k & 7) % b->w, (k >> 3) % b->h) ? 0xffffff : 0; }
    else for (int k = 0; k < 64; k++) br->patc[k] = value_rgb(b, px_get(b, (k & 7) % b->w, (k >> 3) % b->h));
}
static uint32_t g_CreatePatternBrush(void) {
    struct bmp *b = bmpget(ARG(0)); uint32_t h = new_brush(3, 0, 0);
    if (b) brush_from_bmp(&BR[HI(h)], b);
    return h;
}
static uint32_t g_CreateDIBPatternBrush(void) {
    uint32_t h = new_brush(3, 0, 0);
    int v = dib_view(ARG(0), 0, ARG(1), 0);
    brush_from_bmp(&BR[HI(h)], &B[v]);
    free_bmp(v);
    return h;
}
static uint32_t g_CreatePen(void) { int k = ALLOC(PN, NPEN, 1); PN[k].style = (int)ARG(0); PN[k].width = (int)ARG(1); PN[k].color = ARG(2); return H(T_PEN, k); }
static uint32_t g_SetBrushOrgEx(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    if (ARG(3)) { WR32(ARG(3), (uint32_t)d->s.brx); WR32(ARG(3) + 4, (uint32_t)d->s.bry); }
    d->s.brx = (int32_t)ARG(1); d->s.bry = (int32_t)ARG(2); return 1;
}

// ---------------------------------------------------------------- drawing
static uint32_t g_MoveToEx(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    if (ARG(3)) { WR32(ARG(3), (uint32_t)d->s.curx); WR32(ARG(3) + 4, (uint32_t)d->s.cury); }
    d->s.curx = (int32_t)ARG(1); d->s.cury = (int32_t)ARG(2); return 1;
}
static uint32_t g_GetCurrentPositionEx(void) { struct dc *d = dcget(ARG(0)); if (!d) return 0; WR32(ARG(1), (uint32_t)d->s.curx); WR32(ARG(1) + 4, (uint32_t)d->s.cury); return 1; }
static uint32_t g_LineTo(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    int x = (int32_t)ARG(1), y = (int32_t)ARG(2);
    line_surf(d, LX(d, d->s.curx), LY(d, d->s.cury), LX(d, x), LY(d, y));
    d->s.curx = x; d->s.cury = y; return 1;
}
static uint32_t g_Polyline(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    for (uint32_t k = 0; k + 1 < ARG(2); k++) {
        uint32_t p = ARG(1) + 8 * k;
        line_surf(d, LX(d, (int32_t)RD32(p)), LY(d, (int32_t)RD32(p + 4)), LX(d, (int32_t)RD32(p + 8)), LY(d, (int32_t)RD32(p + 12)));
    }
    return 1;
}
static void outline_rect(struct dc *d, struct rect r) {
    struct pen *p = penget(d->s.pen); if (!p || p->style == 5) return;
    int w = p->width > 1 ? p->width : 1;
    uint32_t hb = new_brush(0, p->color, 0);
    fill_rect_surf(d, (struct rect){ r.l, r.t, r.r, r.t + w }, hb, d->s.rop2);
    fill_rect_surf(d, (struct rect){ r.l, r.b - w, r.r, r.b }, hb, d->s.rop2);
    fill_rect_surf(d, (struct rect){ r.l, r.t + w, r.l + w, r.b - w }, hb, d->s.rop2);
    fill_rect_surf(d, (struct rect){ r.r - w, r.t + w, r.r, r.b - w }, hb, d->s.rop2);
    BR[HI(hb)].used = 0;
}
static uint32_t g_Rectangle(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    struct rect r = { LX(d, (int32_t)ARG(1)), LY(d, (int32_t)ARG(2)), LX(d, (int32_t)ARG(3)), LY(d, (int32_t)ARG(4)) };
    struct pen *p = penget(d->s.pen); int inset = (p && p->style != 5) ? 1 : 0;
    fill_rect_surf(d, (struct rect){ r.l + inset, r.t + inset, r.r - inset, r.b - inset }, d->s.brush, d->s.rop2);
    outline_rect(d, r);
    return 1;
}
static uint32_t g_RoundRect(void) { return g_Rectangle(); }
static uint32_t g_Ellipse(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    struct rect r = { LX(d, (int32_t)ARG(1)), LY(d, (int32_t)ARG(2)), LX(d, (int32_t)ARG(3)), LY(d, (int32_t)ARG(4)) };
    ellipse_surf(d, r, 1, 1);
    return 1;
}
static uint32_t do_polygons(struct dc *d, uint32_t pts, const int *counts, int npoly) {
    int tot = 0; for (int k = 0; k < npoly; k++) tot += counts[k];
    int *xy = malloc(sizeof(int) * 2 * (size_t)(tot + 1));
    for (int k = 0; k < tot; k++) { xy[2 * k] = LX(d, (int32_t)RD32(pts + 8u * k)); xy[2 * k + 1] = LY(d, (int32_t)RD32(pts + 8u * k + 4)); }
    poly_fill(d, xy, counts, npoly, d->s.brush);
    int base = 0;
    for (int pp = 0; pp < npoly; pp++) {
        for (int k = 0; k < counts[pp]; k++) { int a = base + k, b = base + (k + 1) % counts[pp]; line_surf(d, xy[2 * a], xy[2 * a + 1], xy[2 * b], xy[2 * b + 1]); }
        base += counts[pp];
    }
    free(xy);
    return 1;
}
static uint32_t g_Polygon(void) { struct dc *d = dcget(ARG(0)); if (!d) return 0; int c = (int)ARG(2); return do_polygons(d, ARG(1), &c, 1); }
static uint32_t g_PolyPolygon(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    int n = (int)ARG(3); int *c = malloc(sizeof(int) * (size_t)(n + 1));
    for (int k = 0; k < n; k++) c[k] = (int)RD32(ARG(2) + 4u * k);
    do_polygons(d, ARG(1), c, n); free(c); return 1;
}
void gdi_fill_rect(uint32_t hdc, struct rect r, uint32_t hbr) {
    struct dc *d = dcget(hdc); if (!d) return;
    if (hbr && hbr <= 31) hbr = gdi_sys_color_brush((int)hbr - 1);
    fill_rect_surf(d, (struct rect){ LX(d, r.l), LY(d, r.t), LX(d, r.r), LY(d, r.b) }, hbr, 13);
}
void gdi_frame_rect(uint32_t hdc, struct rect r, uint32_t hbr) {
    struct dc *d = dcget(hdc); if (!d) return;
    struct rect a = { LX(d, r.l), LY(d, r.t), LX(d, r.r), LY(d, r.b) };
    fill_rect_surf(d, (struct rect){ a.l, a.t, a.r, a.t + 1 }, hbr, 13); fill_rect_surf(d, (struct rect){ a.l, a.b - 1, a.r, a.b }, hbr, 13);
    fill_rect_surf(d, (struct rect){ a.l, a.t, a.l + 1, a.b }, hbr, 13); fill_rect_surf(d, (struct rect){ a.r - 1, a.t, a.r, a.b }, hbr, 13);
}
uint32_t gdi_sys_color_brush(int idx) {
    static uint32_t cache[32];
    if (idx < 0 || idx > 31) idx = 0;
    if (!cache[idx]) cache[idx] = new_brush(0, sys_color(idx), 0);
    return cache[idx];
}

// ---------------------------------------------------------------- regions (API)
static uint32_t new_rgn_h(struct rect r) { int k = new_rgn(); rg_rect(&RG[k], r); return H(T_RGN, k); }
uint32_t gdi_region_from_rect(struct rect r) { return new_rgn_h(r); }
void gdi_region_get_box(uint32_t h, struct rect *r) { struct rgn *g = rgnget(h); *r = g ? rg_box(g) : (struct rect){ 0, 0, 0, 0 }; }
void gdi_region_set_rect(uint32_t h, struct rect r) { struct rgn *g = rgnget(h); if (g) rg_rect(g, r); }
static uint32_t g_CreateRectRgn(void) { return new_rgn_h((struct rect){ (int32_t)ARG(0), (int32_t)ARG(1), (int32_t)ARG(2), (int32_t)ARG(3) }); }
static uint32_t g_CreateRectRgnIndirect(void) { return new_rgn_h(rect_rd(ARG(0))); }
static uint32_t g_SetRectRgn(void) { struct rgn *g = rgnget(ARG(0)); if (!g) return 0; rg_rect(g, (struct rect){ (int32_t)ARG(1), (int32_t)ARG(2), (int32_t)ARG(3), (int32_t)ARG(4) }); return 1; }
static uint32_t rgn_type(struct rgn *g) { return g->n > 1 ? 3 : g->n ? 2 : 1; }
static uint32_t g_CombineRgn(void) {
    struct rgn *d = rgnget(ARG(0)), *a = rgnget(ARG(1)), *b = rgnget(ARG(2)); if (!d || !a) return 0;
    struct rgn tmp = { 1, 0, 0, 0 }; struct rgn empty = { 1, 0, 0, 0 };
    rg_combine(&tmp, a, b ? b : &empty, (int)ARG(3));
    rg_set(d, tmp.r, tmp.n); free(tmp.r);
    return rgn_type(d);
}
static uint32_t g_OffsetRgn(void) {
    struct rgn *g = rgnget(ARG(0)); if (!g) return 0;
    int dx = (int32_t)ARG(1), dy = (int32_t)ARG(2);
    for (int k = 0; k < g->n; k++) { g->r[k].l += dx; g->r[k].r += dx; g->r[k].t += dy; g->r[k].b += dy; }
    return rgn_type(g);
}
static uint32_t g_GetRgnBox(void) { struct rgn *g = rgnget(ARG(0)); if (!g) return 0; rect_wr(ARG(1), rg_box(g)); return rgn_type(g); }
static uint32_t g_PtInRegion(void) {
    struct rgn *g = rgnget(ARG(0)); if (!g) return 0; int x = (int32_t)ARG(1), y = (int32_t)ARG(2);
    for (int k = 0; k < g->n; k++) if (x >= g->r[k].l && x < g->r[k].r && y >= g->r[k].t && y < g->r[k].b) return 1;
    return 0;
}
static uint32_t g_EqualRgn(void) {
    struct rgn *a = rgnget(ARG(0)), *b = rgnget(ARG(1)); if (!a || !b) return 0;
    struct rgn x = { 1, 0, 0, 0 }; rg_combine(&x, a, b, 3); int e = x.n == 0; free(x.r); return (uint32_t)e;
}
static void rgn_scanlines(struct rgn *g, const int *xy, const int *counts, int npoly, int ellipse, struct rect er) {
    int cap = 256, n = 0; struct rect *out = malloc(sizeof *out * cap);
    if (ellipse) {
        double cx = (er.l + er.r) / 2.0, cy = (er.t + er.b) / 2.0, rx = (er.r - er.l) / 2.0, ry = (er.b - er.t) / 2.0;
        for (int y = er.t; y < er.b; y++) {
            double t = (y + 0.5 - cy) / ry; if (fabs(t) > 1) continue;
            double hw = rx * sqrt(1 - t * t);
            if (n == cap) { cap *= 2; out = realloc(out, sizeof *out * cap); }
            out[n++] = (struct rect){ (int)floor(cx - hw + 0.5), y, (int)floor(cx + hw + 0.5), y + 1 };
        }
    } else {
        int tot = 0, ymin = 1 << 30, ymax = -(1 << 30);
        for (int k = 0; k < npoly; k++) tot += counts[k];
        for (int k = 0; k < tot; k++) { if (xy[2 * k + 1] < ymin) ymin = xy[2 * k + 1]; if (xy[2 * k + 1] > ymax) ymax = xy[2 * k + 1]; }
        int *xs = malloc(sizeof(int) * (size_t)(tot + 2));
        for (int y = ymin; y < ymax; y++) {
            int nx = 0, base = 0; double yc = y + 0.5;
            for (int pp = 0; pp < npoly; pp++) {
                for (int k = 0; k < counts[pp]; k++) {
                    int a = base + k, b = base + (k + 1) % counts[pp];
                    double x0 = xy[2 * a], y0 = xy[2 * a + 1], x1 = xy[2 * b], y1 = xy[2 * b + 1];
                    if ((y0 <= yc && y1 > yc) || (y1 <= yc && y0 > yc)) xs[nx++] = (int)(x0 + (yc - y0) * (x1 - x0) / (y1 - y0) + 0.5);
                }
                base += counts[pp];
            }
            qsort(xs, (size_t)nx, sizeof(int), cmp_int);
            for (int k = 0; k + 1 < nx; k += 2) { if (n == cap) { cap *= 2; out = realloc(out, sizeof *out * cap); } out[n++] = (struct rect){ xs[k], y, xs[k + 1], y + 1 }; }
        }
        free(xs);
    }
    // normalise through a union with nothing (merges rows, removes overlap)
    struct rgn raw = { 1, n, cap, out }, empty = { 1, 0, 0, 0 };
    struct rgn tmp = { 1, 0, 0, 0 };
    // rows can overlap horizontally only within a row: union pairwise via OR with itself
    rg_combine(&tmp, &raw, &empty, 2);
    rg_set(g, tmp.r, tmp.n); free(tmp.r); free(out);
}
static uint32_t g_CreatePolygonRgn(void) {
    int n = (int)ARG(1); int *xy = malloc(sizeof(int) * 2 * (size_t)(n + 1));
    for (int k = 0; k < n; k++) { xy[2 * k] = (int32_t)RD32(ARG(0) + 8u * k); xy[2 * k + 1] = (int32_t)RD32(ARG(0) + 8u * k + 4); }
    int k = new_rgn(); rgn_scanlines(&RG[k], xy, &n, 1, 0, (struct rect){ 0, 0, 0, 0 }); free(xy);
    return H(T_RGN, k);
}
static uint32_t g_CreateEllipticRgnIndirect(void) { int k = new_rgn(); rgn_scanlines(&RG[k], 0, 0, 0, 1, rect_rd(ARG(0))); return H(T_RGN, k); }
static uint32_t g_CreateRoundRectRgn(void) { return new_rgn_h((struct rect){ (int32_t)ARG(0), (int32_t)ARG(1), (int32_t)ARG(2), (int32_t)ARG(3) }); }
static void fill_rgn(struct dc *d, struct rgn *g, uint32_t hbr, int rop) {
    for (int k = 0; k < g->n; k++) {
        struct rect r = g->r[k];
        fill_rect_surf(d, (struct rect){ LX(d, r.l), LY(d, r.t), LX(d, r.r), LY(d, r.b) }, hbr, rop);
    }
}
static uint32_t g_FillRgn(void) { struct dc *d = dcget(ARG(0)); struct rgn *g = rgnget(ARG(1)); if (!d || !g) return 0; fill_rgn(d, g, ARG(2), 13); return 1; }
static uint32_t g_PaintRgn(void) { struct dc *d = dcget(ARG(0)); struct rgn *g = rgnget(ARG(1)); if (!d || !g) return 0; fill_rgn(d, g, d->s.brush, d->s.rop2); return 1; }
static uint32_t g_InvertRgn(void) {
    struct dc *d = dcget(ARG(0)); struct rgn *g = rgnget(ARG(1)); if (!d || !g) return 0;
    for (int k = 0; k < g->n; k++) { struct rect r = g->r[k]; blit(d, LX(d, r.l), LY(d, r.t), r.r - r.l, r.b - r.t, 0, 0, 0, 0, 0, 0, 0x550009); }
    return 1;
}
static uint32_t g_FrameRgn(void) {
    struct dc *d = dcget(ARG(0)); struct rgn *g = rgnget(ARG(1)); if (!d || !g) return 0;
    struct rect b = rg_box(g); gdi_frame_rect(ARG(0), b, ARG(2)); return 1;
}
void gdi_invert_rect(uint32_t hdc, struct rect r) { struct dc *d = dcget(hdc); if (d) blit(d, LX(d, r.l), LY(d, r.t), r.r - r.l, r.b - r.t, 0, 0, 0, 0, 0, 0, 0x550009); }

// clipping API
static void dc_set_clip_rect_list(struct dc *d, struct rgn *src) {
    if (!d->s.clip) d->s.clip = new_rgn();
    rg_set(&RG[d->s.clip], src->r, src->n);
}
static uint32_t g_SelectClipRgn(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    struct rgn *g = rgnget(ARG(1));
    if (!g) { if (d->s.clip) { RG[d->s.clip].used = 0; d->s.clip = 0; } return 2; }
    dc_set_clip_rect_list(d, g);
    return rgn_type(g);
}
static struct rect dc_extent(struct dc *d) {   // device-relative extent of the DC surface / window
    if (d->s.hwnd || d->is_screen) { int x, y, w, h; user_window_origin(d->s.hwnd, d->s.client, &x, &y, &w, &h); return (struct rect){ 0, 0, w, h }; }
    struct bmp *b = dc_surface(d); return (struct rect){ 0, 0, b->w, b->h };
}
static uint32_t g_IntersectClipRect(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    struct rect r = { (int32_t)ARG(1) - d->s.wox + d->s.vpx, (int32_t)ARG(2) - d->s.woy + d->s.vpy, (int32_t)ARG(3) - d->s.wox + d->s.vpx, (int32_t)ARG(4) - d->s.woy + d->s.vpy };
    struct rgn rr = { 1, 0, 0, 0 }; rg_rect(&rr, r);
    if (!d->s.clip) { d->s.clip = new_rgn(); rg_rect(&RG[d->s.clip], dc_extent(d)); }
    struct rgn tmp = { 1, 0, 0, 0 }; rg_combine(&tmp, &RG[d->s.clip], &rr, 1);
    rg_set(&RG[d->s.clip], tmp.r, tmp.n); free(tmp.r); free(rr.r);
    return rgn_type(&RG[d->s.clip]);
}
static uint32_t g_GetClipBox(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    struct rect b = d->s.clip ? rect_isect(rg_box(&RG[d->s.clip]), dc_extent(d)) : dc_extent(d);
    b.l += d->s.wox - d->s.vpx; b.r += d->s.wox - d->s.vpx; b.t += d->s.woy - d->s.vpy; b.b += d->s.woy - d->s.vpy;
    rect_wr(ARG(1), b);
    return rect_empty(b) ? 1 : (d->s.clip && RG[d->s.clip].n > 1) ? 3 : 2;
}
static uint32_t g_GetClipRgn(void) {
    struct dc *d = dcget(ARG(0)); struct rgn *g = rgnget(ARG(1)); if (!d || !g) return (uint32_t)-1;
    if (!d->s.clip) return 0;
    rg_set(g, RG[d->s.clip].r, RG[d->s.clip].n); return 1;
}

// ---------------------------------------------------------------- DC attributes
#define GETSET(name, field) \
    static uint32_t g_Set##name(void) { struct dc *d = dcget(ARG(0)); if (!d) return 0; uint32_t o = (uint32_t)d->s.field; d->s.field = (__typeof__(d->s.field))ARG(1); return o; } \
    static uint32_t g_Get##name(void) { struct dc *d = dcget(ARG(0)); return d ? (uint32_t)d->s.field : 0; }
GETSET(TextColor, text) GETSET(BkColor, bk) GETSET(BkMode, bkmode) GETSET(ROP2, rop2) GETSET(TextAlign, align) GETSET(StretchBltMode, stretch)
static uint32_t g_SetPolyFillMode(void) { struct dc *d = dcget(ARG(0)); if (!d) return 0; uint32_t o = (uint32_t)d->s.fill; d->s.fill = (int)ARG(1); return o; }
static uint32_t g_SetViewportOrgEx(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    if (ARG(3)) { WR32(ARG(3), (uint32_t)d->s.vpx); WR32(ARG(3) + 4, (uint32_t)d->s.vpy); }
    d->s.vpx = (int32_t)ARG(1); d->s.vpy = (int32_t)ARG(2); return 1;
}
static uint32_t g_SetWindowOrgEx(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    if (ARG(3)) { WR32(ARG(3), (uint32_t)d->s.wox); WR32(ARG(3) + 4, (uint32_t)d->s.woy); }
    d->s.wox = (int32_t)ARG(1); d->s.woy = (int32_t)ARG(2); return 1;
}
static uint32_t g_GetDCOrgEx(void) { struct dc *d = dcget(ARG(0)); if (!d) return 0; dc_refresh_origin(d); WR32(ARG(1), (uint32_t)d->s.ox); WR32(ARG(1) + 4, (uint32_t)d->s.oy); return 1; }
static uint32_t g_DPtoLP(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    for (uint32_t k = 0; k < ARG(2); k++) { uint32_t p = ARG(1) + 8 * k; WR32(p, RD32(p) + (uint32_t)(d->s.wox - d->s.vpx)); WR32(p + 4, RD32(p + 4) + (uint32_t)(d->s.woy - d->s.vpy)); }
    return 1;
}
static uint32_t g_LPtoDP(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    for (uint32_t k = 0; k < ARG(2); k++) { uint32_t p = ARG(1) + 8 * k; WR32(p, RD32(p) - (uint32_t)(d->s.wox - d->s.vpx)); WR32(p + 4, RD32(p + 4) - (uint32_t)(d->s.woy - d->s.vpy)); }
    return 1;
}
static uint32_t g_GetDeviceCaps(void) {
    struct dc *d = dcget(ARG(0));
    int bpp = d ? dc_surface(d)->bpp : 8;
    switch (ARG(1)) {
    case 0: return 0x400;                 // DRIVERVERSION
    case 2: return 1;                     // TECHNOLOGY: raster display
    case 4: return 208; case 6: return 156;          // HORZSIZE / VERTSIZE (mm)
    case 8: return SCREEN_W; case 10: return SCREEN_H;
    case 12: return (uint32_t)bpp; case 14: return 1;      // BITSPIXEL / PLANES
    case 16: return 1; case 18: return 1; case 20: return 1; // NUMBRUSHES/PENS/MARKERS
    case 22: return 0; case 24: return bpp == 8 ? 20 : (uint32_t)-1;    // NUMFONTS / NUMCOLORS
    case 26: return 0; case 28: return 0;
    case 32: return 0x00ff; case 34: return 0x00ff; case 36: return 0x00ff; case 38: return 0x7e99 | (bpp == 8 ? 0x100 : 0);   // CURVE/LINE/POLYGONAL/RASTERCAPS
    case 40: return 36; case 42: return 36; case 44: return 51;  // ASPECTX/Y/XY
    case 88: case 90: return 96;          // LOGPIXELSX/Y
    case 104: return bpp == 8 ? 256 : 0; case 106: return bpp == 8 ? 20 : 0; case 108: return 18;  // SIZEPALETTE NUMRESERVED COLORRES
    case 116: return 60; case 118: return SCREEN_W; case 120: return SCREEN_H;
    default: return 0;
    }
}
static uint32_t g_SetStretch(void) { return 1; }

// ---------------------------------------------------------------- fonts and text (6x8 bitmap font, scaled to the requested height)
static uint32_t g_CreateFontIndirectA(void) {
    uint32_t p = ARG(0); int k = ALLOC(FN, NFONT, 1);
    FN[k].height = (int32_t)RD32(p); FN[k].width = (int32_t)RD32(p + 4); FN[k].weight = (int)RD32(p + 16);
    FN[k].italic = RD8(p + 20); FN[k].underline = RD8(p + 21); FN[k].charset = RD8(p + 23);
    gstr_get(p + 28, FN[k].face, 32);
    return H(T_FONT, k);
}
static uint32_t g_CreateFontA(void) {
    int k = ALLOC(FN, NFONT, 1);
    FN[k].height = (int32_t)ARG(0); FN[k].width = (int32_t)ARG(1); FN[k].weight = (int)ARG(4); FN[k].italic = (int)ARG(5); FN[k].charset = (int)ARG(8);
    gstr_get(ARG(13), FN[k].face, 32);
    return H(T_FONT, k);
}
static int font_px(struct dc *d) { struct font *f = fontget(d->s.font); int h = f ? f->height : 12; if (h < 0) h = -h; if (!h) h = 12; return h; }
static int char_w(int h) { return (h * 6 + 4) / 8 > 4 ? (h * 6 + 4) / 8 - 1 : 4; }
static int text_width(struct dc *d, const char *s, int n) { (void)s; return n * char_w(font_px(d)); }
static void draw_glyphs(struct dc *d, int x, int y, const char *s, int n) {
    int h = font_px(d), cw = char_w(h);
    struct bmp *b = dc_surface(d);
    uint32_t fg = color_value(b, d, d->s.text);
    struct rect cl[64]; int ncl = dc_clip(d, cl, 64);
    int save = d->s.rop2; d->s.rop2 = 13;
    for (int i = 0; i < n; i++) {
        unsigned c = (unsigned char)s[i];
        if (c >= 0x81 && (c <= 0x9f || c >= 0xe0)) { i++; c = '?'; }
        if (c < 32 || c > 126) c = '?';
        for (int yy = 0; yy < h; yy++) for (int xx = 0; xx < cw; xx++) {
            int fy = yy * 8 / h, fx = xx * 8 / (cw + 1);
            if (FONT6X8[c - 32][fy] & (0x80 >> fx)) plot(d, cl, ncl, x + i * cw + xx, y + yy, fg);
        }
    }
    d->s.rop2 = save;
    mark_dirty(d);
}
static void text_out(struct dc *d, int x, int y, const char *s, int n, struct rect *opaque) {
    int h = font_px(d), w = text_width(d, s, n);
    if (d->s.align & 1) { x = d->s.curx; y = d->s.cury; }
    if ((d->s.align & 6) == 2) x -= w; else if ((d->s.align & 6) == 6) x -= w / 2;
    if ((d->s.align & 24) == 8) y -= h; else if ((d->s.align & 24) == 24) y -= h * 4 / 5;
    int sx = LX(d, x), sy = LY(d, y);
    if (opaque) { uint32_t hb = new_brush(0, d->s.bk, 0); fill_rect_surf(d, (struct rect){ LX(d, opaque->l), LY(d, opaque->t), LX(d, opaque->r), LY(d, opaque->b) }, hb, 13); BR[HI(hb)].used = 0; }
    else if (d->s.bkmode == 2) { uint32_t hb = new_brush(0, d->s.bk, 0); fill_rect_surf(d, (struct rect){ sx, sy, sx + w, sy + h }, hb, 13); BR[HI(hb)].used = 0; }
    draw_glyphs(d, sx, sy, s, n);
    if (d->s.align & 1) d->s.curx = x + w;
}
static uint32_t g_TextOutA(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    char s[1024]; int n = (int)ARG(4); if (n > 1023) n = 1023; if (n < 0) n = 0; memcpy(s, GP(ARG(3)), (size_t)n); s[n] = 0;
    text_out(d, (int32_t)ARG(1), (int32_t)ARG(2), s, n, 0); return 1;
}
static uint32_t g_ExtTextOutA(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    char s[1024]; int n = (int)ARG(6); if (n > 1023) n = 1023; if (n < 0) n = 0; if (ARG(5)) memcpy(s, GP(ARG(5)), (size_t)n); else n = 0; s[n] = 0;
    struct rect r; int op = (ARG(3) & 2) && ARG(4); if (op) r = rect_rd(ARG(4));
    if (!n && op) { uint32_t hb = new_brush(0, d->s.bk, 0); fill_rect_surf(d, (struct rect){ LX(d, r.l), LY(d, r.t), LX(d, r.r), LY(d, r.b) }, hb, 13); BR[HI(hb)].used = 0; return 1; }
    text_out(d, (int32_t)ARG(1), (int32_t)ARG(2), s, n, op ? &r : 0); return 1;
}
int gdi_draw_text(uint32_t hdc, const char *s, int n, struct rect *r, uint32_t fmt) {
    struct dc *d = dcget(hdc); if (!d) return 0;
    int h = font_px(d), w = text_width(d, s, n);
    if (fmt & 0x400) { r->r = r->l + w; r->b = r->t + h; return h; }
    int x = r->l, y = r->t;
    if (fmt & 1) x = (r->l + r->r - w) / 2; else if (fmt & 2) x = r->r - w;
    if (fmt & 4) y = (r->t + r->b - h) / 2;
    if (d->s.bkmode == 2) { uint32_t hb = new_brush(0, d->s.bk, 0); fill_rect_surf(d, (struct rect){ LX(d, x), LY(d, y), LX(d, x + w), LY(d, y + h) }, hb, 13); BR[HI(hb)].used = 0; }
    draw_glyphs(d, LX(d, x), LY(d, y), s, n);
    return h;
}
static uint32_t g_GetTextExtentPointA(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    WR32(ARG(3), (uint32_t)text_width(d, (char *)GP(ARG(1)), (int)ARG(2))); WR32(ARG(3) + 4, (uint32_t)font_px(d)); return 1;
}
static uint32_t g_GetTextMetricsA(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0;
    int h = font_px(d); uint32_t p = ARG(1); memset(GP(p), 0, 53);   // TEXTMETRICA is 53 bytes (56 with padding)
    WR32(p, (uint32_t)h); WR32(p + 4, (uint32_t)(h * 4 / 5)); WR32(p + 8, (uint32_t)(h - h * 4 / 5));
    WR32(p + 20, (uint32_t)char_w(h)); WR32(p + 24, (uint32_t)char_w(h)); WR32(p + 28, 400);
    WR32(p + 36, 96); WR32(p + 40, 96);
    WR8(p + 44, 32); WR8(p + 45, 255); WR8(p + 46, '?'); WR8(p + 47, ' ');
    WR8(p + 51, 0x31); WR8(p + 52, 128);
    return 1;
}
static uint32_t g_GetTextFaceA(void) {
    struct dc *d = dcget(ARG(0)); if (!d) return 0; struct font *f = fontget(d->s.font);
    const char *n = f && f->face[0] ? f->face : "System";
    if (ARG(2)) { size_t l = strlen(n); if (l >= ARG(1)) l = ARG(1) - 1; memcpy(GP(ARG(2)), n, l); WR8(ARG(2) + (uint32_t)l, 0); }
    return (uint32_t)strlen(n);
}
// EnumFontsA(hdc, face, proc, data): offer one Japanese gothic font
static uint32_t g_EnumFontsA(void) {
    uint32_t lf = gmem_alloc(60), tm = gmem_alloc(60);
    WR32(lf, (uint32_t)-12); WR8(lf + 23, 128); WR8(lf + 27, 0x31);
    static const char gothic[] = "\x82\x6c\x82\x72 \x83\x53\x83\x56\x83\x62\x83\x4e";   // MS Gothic in SJIS
    gstr_put(lf + 28, ARG(1) ? (char *)GP(ARG(1)) : gothic);
    WR32(tm, 12); WR32(tm + 4, 10); WR32(tm + 8, 2); WR32(tm + 20, 6); WR32(tm + 24, 12); WR32(tm + 28, 400); WR8(tm + 51, 0x31); WR8(tm + 52, 128);
    uint32_t a[4] = { lf, tm, 4 /*TRUETYPE*/, ARG(3) };
    uint32_t r = ARG(2) ? guest_call(ARG(2), 4, a) : 1;
    gmem_free(lf); gmem_free(tm);
    return r;
}
static uint32_t g_one(void) { return 1; }
static uint32_t g_zero(void) { return 0; }

const struct api API_GDI32[] = {
    A(FillRgn, g_FillRgn, 12), A(CreatePolygonRgn, g_CreatePolygonRgn, 12), A(OffsetRgn, g_OffsetRgn, 12), A(GetTextFaceA, g_GetTextFaceA, 12),
    A(GetTextMetricsA, g_GetTextMetricsA, 8), A(SelectObject, g_SelectObject, 8), A(CreateFontIndirectA, g_CreateFontIndirectA, 4),
    A(GetStockObject, g_GetStockObject, 4), A(AddFontResourceA, g_one, 4), A(RemoveFontResourceA, g_one, 4), A(DeleteObject, g_DeleteObject, 4),
    A(CreatePalette, g_CreatePalette, 4), A(SetPaletteEntries, g_SetPaletteEntries, 16), A(AnimatePalette, g_AnimatePalette, 16),
    A(GetObjectA, g_GetObjectA, 12), A(GetDeviceCaps, g_GetDeviceCaps, 8), A(CreateCompatibleBitmap, g_CreateCompatibleBitmap, 12),
    A(CreateCompatibleDC, g_CreateCompatibleDC, 4), A(SetDIBColorTable, g_SetDIBColorTable, 16), A(DeleteDC, g_DeleteDC, 4),
    A(SetTextAlign, g_SetTextAlign, 8), A(GetTextAlign, g_GetTextAlign, 4), A(SelectClipRgn, g_SelectClipRgn, 8), A(CreateRectRgn, g_CreateRectRgn, 16),
    A(IntersectClipRect, g_IntersectClipRect, 20), A(GetClipBox, g_GetClipBox, 8), A(SetTextColor, g_SetTextColor, 8), A(SetBkColor, g_SetBkColor, 8),
    A(ExtTextOutA, g_ExtTextOutA, 32), A(GetClipRgn, g_GetClipRgn, 8), A(GetDCOrgEx, g_GetDCOrgEx, 8), A(CreateBitmap, g_CreateBitmap, 20),
    A(TextOutA, g_TextOutA, 20), A(CreateSolidBrush, g_CreateSolidBrush, 4), A(MoveToEx, g_MoveToEx, 16), A(GetCurrentPositionEx, g_GetCurrentPositionEx, 8),
    A(CreateBrushIndirect, g_CreateBrushIndirect, 4), A(SetStretchBltMode, g_SetStretchBltMode, 8), A(GetStretchBltMode, g_GetStretchBltMode, 4),
    A(GetTextColor, g_GetTextColor, 4), A(GetBkColor, g_GetBkColor, 4), A(SetROP2, g_SetROP2, 8), A(GetROP2, g_GetROP2, 4), A(SetBkMode, g_SetBkMode, 8),
    A(GetBkMode, g_GetBkMode, 4), A(SetViewportOrgEx, g_SetViewportOrgEx, 16), A(BitBlt, g_BitBlt, 36), A(StretchBlt, g_StretchBlt, 44),
    A(SetDIBits, g_SetDIBits, 28), A(GetBitmapBits, g_GetBitmapBits, 12), A(GetSystemPaletteEntries, g_GetSystemPaletteEntries, 16),
    A(StretchDIBits, g_StretchDIBits, 52), A(GetDIBits, g_GetDIBits, 28), A(CreateRectRgnIndirect, g_CreateRectRgnIndirect, 4), A(Escape, g_zero, 20),
    A(CreateICA, g_CreateICA, 16), A(StartDocA, g_zero, 8), A(SetAbortProc, g_zero, 8), A(EndDoc, g_zero, 4), A(AbortDoc, g_zero, 4), A(DPtoLP, g_DPtoLP, 12),
    A(StartPage, g_zero, 4), A(EndPage, g_zero, 4), A(Polyline, g_Polyline, 12), A(CreateDIBSection, g_CreateDIBSection, 24),
    A(SelectPalette, g_SelectPalette, 12), A(CreateDIBitmap, g_CreateDIBitmap, 24), A(SetDIBitsToDevice, g_SetDIBitsToDevice, 48),
    A(CreatePatternBrush, g_CreatePatternBrush, 4), A(SetBitmapBits, g_SetBitmapBits, 12), A(CreateDIBPatternBrush, g_CreateDIBPatternBrush, 8),
    A(UnrealizeObject, g_UnrealizeObject, 4), A(SetBrushOrgEx, g_SetBrushOrgEx, 16), A(LPtoDP, g_LPtoDP, 12), A(GetPaletteEntries, g_GetPaletteEntries, 16),
    A(GetDIBColorTable, g_GetDIBColorTable, 16), A(SetWindowOrgEx, g_SetWindowOrgEx, 16), A(PaintRgn, g_PaintRgn, 8), A(Rectangle, g_Rectangle, 20),
    A(PatBlt, g_PatBlt, 24), A(RestoreDC, g_RestoreDC, 8), A(SaveDC, g_SaveDC, 4), A(CombineRgn, g_CombineRgn, 16), A(SetPolyFillMode, g_SetPolyFillMode, 8),
    A(RoundRect, g_RoundRect, 28), A(CreatePen, g_CreatePen, 12), A(CreateFontA, g_CreateFontA, 56), A(SetTextJustification, g_one, 12),
    A(CreateRoundRectRgn, g_CreateRoundRectRgn, 24), A(FrameRgn, g_FrameRgn, 20), A(InvertRgn, g_InvertRgn, 8), A(GetTextExtentPointA, g_GetTextExtentPointA, 16),
    A(PtInRegion, g_PtInRegion, 12), A(EnumFontsA, g_EnumFontsA, 16), A(Ellipse, g_Ellipse, 20), A(EqualRgn, g_EqualRgn, 8), A(SetRectRgn, g_SetRectRgn, 20),
    A(GetRgnBox, g_GetRgnBox, 8), A(LineTo, g_LineTo, 12), A(PolyPolygon, g_PolyPolygon, 16), A(SetSystemPaletteUse, g_SetSystemPaletteUse, 8),
    A(RealizePalette, g_RealizePalette, 4), A(CreateEllipticRgnIndirect, g_CreateEllipticRgnIndirect, 4), A(GetNearestColor, g_GetNearestColor, 8),
    A(GdiSetBatchLimit, g_SetStretch, 4), A(Polygon, g_Polygon, 12),
};
const int N_GDI32 = sizeof API_GDI32 / sizeof API_GDI32[0];

// ---------------------------------------------------------------- init / presentation
uint32_t gdi_screen_bits(void) { return screen_addr; }
void gdi_init(void) {
    // the default hardware palette: the 20 Windows static colours, black elsewhere
    static const uint8_t sc[20][3] = { {0,0,0},{128,0,0},{0,128,0},{128,128,0},{0,0,128},{128,0,128},{0,128,128},{192,192,192},{192,220,192},{166,202,240},
                                       {255,251,240},{160,160,164},{128,128,128},{255,0,0},{0,255,0},{255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255} };
    for (int i = 0; i < 256; i++) syspal_cr[i] = 0;
    for (int i = 0; i < 20; i++) { int j = i < 10 ? i : 236 + i; syspal_cr[j] = sc[i][0] | sc[i][1] << 8 | (uint32_t)sc[i][2] << 16; }
    syspal_changed();
    screen_addr = gmem_alloc(SCREEN_W * SCREEN_H);
    B[SCREEN_BMP] = (struct bmp){ 1, SCREEN_W, SCREEN_H, 8, SCREEN_W, 1, 0, screen_addr, 0, 0, {0}, 0, 1 };
    B[MONO1_BMP] = (struct bmp){ 1, 1, 1, 1, 2, 1, 0, gmem_alloc(4), 1, 0, {0}, 0, 0 };
    // stock objects
    stock[0] = new_brush(0, 0xffffff, 0); stock[1] = new_brush(0, 0xc0c0c0, 0); stock[2] = new_brush(0, 0x808080, 0);
    stock[3] = new_brush(0, 0x404040, 0); stock[4] = new_brush(0, 0, 0); stock[5] = new_brush(1, 0, 0);
    { int k = ALLOC(PN, NPEN, 1); PN[k].style = 0; PN[k].width = 1; PN[k].color = 0xffffff; stock[6] = H(T_PEN, k); }
    { int k = ALLOC(PN, NPEN, 1); PN[k].style = 0; PN[k].width = 1; PN[k].color = 0; stock[7] = H(T_PEN, k); }
    { int k = ALLOC(PN, NPEN, 1); PN[k].style = 5; stock[8] = H(T_PEN, k); }
    for (int i = 10; i <= 17; i++) if (i != 15) { int k = ALLOC(FN, NFONT, 1); FN[k].height = 12; snprintf(FN[k].face, 32, "System"); stock[i] = H(T_FONT, k); }
    { int k = ALLOC(P, NPAL, 1); P[k].n = 20; for (int i = 0; i < 20; i++) P[k].e[i] = sc[i][0] | sc[i][1] << 8 | (uint32_t)sc[i][2] << 16; stock[15] = H(T_PAL, k); }
    stock[18] = stock[0]; stock[19] = stock[7];
}
