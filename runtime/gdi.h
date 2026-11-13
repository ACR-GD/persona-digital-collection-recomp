// Internal interface between USER32 (w32_user.c) and GDI32 (w32_gdi.c).
#pragma once
#include "win32.h"

struct rect { int l, t, r, b; };
static inline int rect_empty(struct rect a) { return a.r <= a.l || a.b <= a.t; }
static inline struct rect rect_isect(struct rect a, struct rect b) {
    struct rect r = { a.l > b.l ? a.l : b.l, a.t > b.t ? a.t : b.t, a.r < b.r ? a.r : b.r, a.b < b.b ? a.b : b.b };
    if (rect_empty(r)) r = (struct rect){ 0, 0, 0, 0 };
    return r;
}
static inline struct rect rect_union(struct rect a, struct rect b) {
    if (rect_empty(a)) return b;
    if (rect_empty(b)) return a;
    return (struct rect){ a.l < b.l ? a.l : b.l, a.t < b.t ? a.t : b.t, a.r > b.r ? a.r : b.r, a.b > b.b ? a.b : b.b };
}
static inline struct rect rect_rd(uint32_t p) { return (struct rect){ (int32_t)RD32(p), (int32_t)RD32(p + 4), (int32_t)RD32(p + 8), (int32_t)RD32(p + 12) }; }
static inline void rect_wr(uint32_t p, struct rect r) { WR32(p, (uint32_t)r.l); WR32(p + 4, (uint32_t)r.t); WR32(p + 8, (uint32_t)r.r); WR32(p + 12, (uint32_t)r.b); }

uint32_t gdi_window_dc(uint32_t hwnd, int client);     // GetDC / GetWindowDC / BeginPaint
void gdi_release_dc(uint32_t hdc);
void gdi_fill_rect(uint32_t hdc, struct rect r, uint32_t hbrush);
void gdi_frame_rect(uint32_t hdc, struct rect r, uint32_t hbrush);
void gdi_invert_rect(uint32_t hdc, struct rect r);
int gdi_draw_text(uint32_t hdc, const char *s, int n, struct rect *r, uint32_t fmt);
uint32_t gdi_bitmap_from_dib(uint32_t bmi, uint32_t bits);   // packed DIB -> HBITMAP (LoadBitmap)
uint32_t gdi_sys_color_brush(int idx);
uint32_t gdi_region_from_rect(struct rect r);
void gdi_region_get_box(uint32_t hrgn, struct rect *r);
uint32_t sys_color(int idx);
// cursors (w32_user.c draws them over the presented frame)
struct cursor { int used, w, h, hx, hy; uint8_t and_[32 * 32], xor_[32 * 32]; uint32_t color[32 * 32]; int has_color; };
struct cursor *user_cursor_get(uint32_t h);
uint32_t user_current_cursor(void);
int user_cursor_shown(void);
