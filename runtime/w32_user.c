// USER32 emulation: window classes and a window tree, the message queue (posted messages plus
// synthesized WM_PAINT / WM_TIMER / input), focus and capture, cursors, resources, string helpers.
// The desktop is the 640x480 8-bit screen of w32_gdi.c; windows have no real non-client drawing.
#include "gdi.h"
#include <SDL3/SDL.h>
#include <strings.h>
#include <ctype.h>

int mouse_x = 320, mouse_y = 240;
static uint8_t keys[256];             // VK state (0x80 down, 0x01 toggled)
static uint32_t t0_ms;
#define DESKTOP 0x0010u

uint32_t w32_tick_ms(void) { return (uint32_t)SDL_GetTicks() - t0_ms + 100000; }
void host_sleep_ms(uint32_t ms) { SDL_Delay(ms); }

// ---------------------------------------------------------------- classes
struct wclass { int used; char name[64]; uint32_t atom, proc, style, cls_extra, wnd_extra, inst, icon, cursor, bg, menu, name_ptr; };
#define NCLS 64
static struct wclass C[NCLS];
static uint32_t builtin_proc;     // magic address of the host procedure for system classes
static int find_class(uint32_t name) {
    char n[64];
    if (name < 0x10000) { for (int k = 0; k < NCLS; k++) if (C[k].used && C[k].atom == name) return k; return -1; }
    gstr_get(name, n, sizeof n);
    for (int k = 0; k < NCLS; k++) if (C[k].used && !strcasecmp(C[k].name, n)) return k;
    return -1;
}
static int add_class(const char *name, uint32_t proc) {
    for (int k = 0; k < NCLS; k++) if (!C[k].used) {
        memset(&C[k], 0, sizeof C[k]); C[k].used = 1; snprintf(C[k].name, sizeof C[k].name, "%s", name);
        C[k].atom = 0xc100u + k; C[k].proc = proc; return k;
    }
    return -1;
}
// RegisterClassA(WNDCLASSA*)
static uint32_t u_RegisterClassA(void) {
    uint32_t p = ARG(0); char n[64]; uint32_t np = RD32(p + 36);
    if (np < 0x10000) snprintf(n, sizeof n, "#%u", np); else gstr_get(np, n, sizeof n);
    int k = find_class(np >= 0x10000 ? np : 0);
    if (k < 0) k = add_class(n, 0);
    if (k < 0) return 0;
    struct wclass *c = &C[k];
    c->style = RD32(p); c->proc = RD32(p + 4); c->cls_extra = RD32(p + 8); c->wnd_extra = RD32(p + 12);
    c->inst = RD32(p + 16); c->icon = RD32(p + 20); c->cursor = RD32(p + 24); c->bg = RD32(p + 28); c->menu = RD32(p + 32);
    LOG("RegisterClass %s proc=%08x extra=%u bg=%x\n", n, c->proc, c->wnd_extra, c->bg);
    return c->atom;
}
// GetClassInfoA(inst, name, WNDCLASSA*)
static uint32_t u_GetClassInfoA(void) {
    int k = find_class(ARG(1)); if (k < 0) return 0;
    struct wclass *c = &C[k]; uint32_t p = ARG(2);
    WR32(p, c->style); WR32(p + 4, c->proc); WR32(p + 8, c->cls_extra); WR32(p + 12, c->wnd_extra); WR32(p + 16, c->inst);
    WR32(p + 20, c->icon); WR32(p + 24, c->cursor); WR32(p + 28, c->bg); WR32(p + 32, c->menu); WR32(p + 36, ARG(1));
    return 1;
}

// ---------------------------------------------------------------- windows
struct prop { uint32_t key; char name[32]; uint32_t val; };
struct wnd {
    int used, cls;
    uint32_t hwnd, proc, parent, owner, style, exstyle, id, menu, userdata, extra, inst;
    int x, y, w, h;          // window rect, relative to the parent's client area (screen for top level)
    int visible, enabled, destroyed;
    char text[256];
    struct rect inval; int erase;
    struct prop props[16];
};
#define NW 128
static struct wnd W[NW];
static uint32_t focus_hwnd, active_hwnd, capture_hwnd;
static struct wnd *wget(uint32_t h) {
    if (h < 0x400 || ((h - 0x400) & 7)) return 0;
    uint32_t k = (h - 0x400) / 8;
    return (k < NW && W[k].used) ? &W[k] : 0;
}
static void nc_insets(struct wnd *w, int *l, int *t, int *r, int *b) {
    int bl = 0, bt = 0;
    uint32_t s = w->style;
    if ((s & 0x00C00000u) == 0x00C00000u) bt += 18;                 // WS_CAPTION
    if (s & 0x00040000u) { bl += 4; } else if ((s & 0x00C00000u) == 0x00C00000u || (s & 0x00400000u)) bl += 3;   // THICKFRAME / DLGFRAME
    else if (s & 0x00800000u) bl += 1;                              // WS_BORDER
    if (w->menu && !(s & 0x40000000u)) bt += 19;
    *l = bl; *r = bl; *t = bl + bt; *b = bl;
}
// screen position of the window (client=0) or its client area (client=1)
int user_window_origin(uint32_t hwnd, int client, int *x, int *y, int *w, int *h) {
    if (hwnd == DESKTOP || hwnd == 0) { *x = 0; *y = 0; *w = SCREEN_W; *h = SCREEN_H; return 1; }
    struct wnd *wn = wget(hwnd); if (!wn) return 0;
    int px = 0, py = 0;
    if (wn->parent && (wn->style & 0x40000000u)) { int pw, ph; user_window_origin(wn->parent, 1, &px, &py, &pw, &ph); }
    *x = px + wn->x; *y = py + wn->y; *w = wn->w; *h = wn->h;
    if (client) { int l, t, r, b; nc_insets(wn, &l, &t, &r, &b); *x += l; *y += t; *w -= l + r; *h -= t + b; if (*w < 0) *w = 0; if (*h < 0) *h = 0; }
    return 1;
}
static int is_visible(struct wnd *w) {
    for (; w; w = (w->style & 0x40000000u) ? wget(w->parent) : 0) if (!w->visible) return 0;
    return 1;
}
static uint32_t call_proc(uint32_t proc, uint32_t hwnd, uint32_t m, uint32_t wp, uint32_t lp) {
    if (!proc) return 0;
    uint32_t a[4] = { hwnd, m, wp, lp };
    return guest_call(proc, 4, a);
}
uint32_t w32_send_message(uint32_t hwnd, uint32_t m, uint32_t wp, uint32_t lp) {
    struct wnd *w = wget(hwnd); if (!w) return 0;
    return call_proc(w->proc, hwnd, m, wp, lp);
}
static uint32_t send(uint32_t hwnd, uint32_t m, uint32_t wp, uint32_t lp) { return w32_send_message(hwnd, m, wp, lp); }

static void invalidate(struct wnd *w, struct rect r, int erase) {
    w->inval = rect_union(w->inval, r); if (erase) w->erase = 1;
}
static void invalidate_all(struct wnd *w, int erase) {
    int x, y, ww, hh; user_window_origin(w->hwnd, 1, &x, &y, &ww, &hh);
    invalidate(w, (struct rect){ 0, 0, ww, hh }, erase);
}
static void invalidate_tree(struct wnd *w) {
    invalidate_all(w, 1);
    for (int k = 0; k < NW; k++) if (W[k].used && W[k].parent == w->hwnd && (W[k].style & 0x40000000u)) invalidate_tree(&W[k]);
}

// CreateWindowExA(ex, class, name, style, x, y, w, h, parent, menu, inst, param)
static uint32_t u_CreateWindowExA(void) {
    uint32_t ex = ARG(0), clsn = ARG(1), name = ARG(2), style = ARG(3);
    int32_t x = (int32_t)ARG(4), y = (int32_t)ARG(5), w = (int32_t)ARG(6), h = (int32_t)ARG(7);
    uint32_t parent = ARG(8), menu = ARG(9), inst = ARG(10), param = ARG(11);
    int ci = find_class(clsn);
    char cname[64] = "?"; if (clsn >= 0x10000) gstr_get(clsn, cname, sizeof cname);
    if (ci < 0) { ci = add_class(cname, builtin_proc); }
    if (ci < 0) return 0;
    // the MacroMix helper window lives at the end of the table so it does not shift the handles
    // of the projector's own windows (keeps runs with and without audio comparable)
    int k0 = !strcasecmp(C[ci].name, "Bud") ? NW - 8 : 0;
    int k; for (k = k0; k < NW && W[k].used; k++);
    if (k == NW) return 0;
    struct wnd *wn = &W[k];
    memset(wn, 0, sizeof *wn);
    wn->used = 1; wn->cls = ci; wn->hwnd = 0x400u + 8u * k; wn->proc = C[ci].proc;
    wn->style = style; wn->exstyle = ex; wn->inst = inst;
    if (style & 0x40000000u) { wn->parent = parent == DESKTOP ? 0 : parent; wn->id = menu; }
    else { wn->owner = parent; wn->menu = menu; }
    if (x == (int32_t)0x80000000) x = 0, y = 0;
    if (w == (int32_t)0x80000000) w = SCREEN_W, h = SCREEN_H;
    wn->x = x; wn->y = y; wn->w = w; wn->h = h; wn->enabled = !(style & 0x08000000u);
    if (name) gstr_get(name, wn->text, sizeof wn->text);
    if (C[ci].wnd_extra) wn->extra = gmem_alloc(C[ci].wnd_extra);
    LOG("CreateWindow '%s' class %s style=%08x ex=%x (%d,%d %dx%d) parent=%x -> %x\n", wn->text, C[ci].name, style, ex, x, y, w, h, parent, wn->hwnd);
    uint32_t cs = gmem_alloc(48);
    WR32(cs + 0x00, param); WR32(cs + 0x04, inst); WR32(cs + 0x08, menu); WR32(cs + 0x0c, parent);
    WR32(cs + 0x10, (uint32_t)h); WR32(cs + 0x14, (uint32_t)w); WR32(cs + 0x18, (uint32_t)y); WR32(cs + 0x1c, (uint32_t)x);
    WR32(cs + 0x20, style); WR32(cs + 0x24, name); WR32(cs + 0x28, clsn); WR32(cs + 0x2c, ex);
    uint32_t hw = wn->hwnd;
    if (!send(hw, 0x81 /*WM_NCCREATE*/, 0, cs)) { LOG("WM_NCCREATE refused\n"); wn->used = 0; return 0; }
    if ((int32_t)send(hw, 0x01 /*WM_CREATE*/, 0, cs) == -1) { LOG("WM_CREATE refused\n"); wn->used = 0; return 0; }
    gmem_free(cs);
    int cx, cy, cw, ch; user_window_origin(hw, 1, &cx, &cy, &cw, &ch);
    send(hw, 0x05 /*WM_SIZE*/, 0, (uint32_t)(cw & 0xffff) | ((uint32_t)ch << 16));
    send(hw, 0x03 /*WM_MOVE*/, 0, 0);
    if (style & 0x10000000u) {   // WS_VISIBLE
        wn->visible = 1; send(hw, 0x18, 1, 0); invalidate_all(wn, 1);
        if (!(style & 0x40000000u) && !active_hwnd) { active_hwnd = hw; focus_hwnd = hw; }
    }
    return hw;
}
static void destroy(struct wnd *w) {
    uint32_t h = w->hwnd;
    for (int k = 0; k < NW; k++) if (W[k].used && (W[k].parent == h || W[k].owner == h)) destroy(&W[k]);
    if (!w->used) return;
    send(h, 0x02 /*WM_DESTROY*/, 0, 0);
    send(h, 0x82 /*WM_NCDESTROY*/, 0, 0);
    if (w->extra) gmem_free(w->extra);
    w->used = 0;
    if (focus_hwnd == h) focus_hwnd = 0;
    if (active_hwnd == h) active_hwnd = 0;
    if (capture_hwnd == h) capture_hwnd = 0;
}
static uint32_t u_DestroyWindow(void) { struct wnd *w = wget(ARG(0)); if (w) destroy(w); return 1; }
static void set_pos(struct wnd *w, int x, int y, int cw, int ch, int move, int size) {
    int moved = move && (x != w->x || y != w->y), sized = size && (cw != w->w || ch != w->h);
    if (move) { w->x = x; w->y = y; }
    if (size) { w->w = cw; w->h = ch; }
    if (moved || sized) {
        uint32_t wp = gmem_alloc(28);
        WR32(wp, w->hwnd); WR32(wp + 8, (uint32_t)w->x); WR32(wp + 12, (uint32_t)w->y); WR32(wp + 16, (uint32_t)w->w); WR32(wp + 20, (uint32_t)w->h);
        send(w->hwnd, 0x47 /*WM_WINDOWPOSCHANGED*/, 0, wp);
        gmem_free(wp);
        invalidate_tree(w);
        screen_dirty = 1;
    }
}
static uint32_t u_MoveWindow(void) {
    struct wnd *w = wget(ARG(0)); if (!w) return 0;
    LOG("MoveWindow %x (%d,%d %dx%d)\n", ARG(0), (int32_t)ARG(1), (int32_t)ARG(2), (int32_t)ARG(3), (int32_t)ARG(4));
    set_pos(w, (int32_t)ARG(1), (int32_t)ARG(2), (int32_t)ARG(3), (int32_t)ARG(4), 1, 1);
    return 1;
}
static uint32_t u_SetWindowPos(void) {
    struct wnd *w = wget(ARG(0)); if (!w) return 0;
    uint32_t fl = ARG(6);
    LOG("SetWindowPos %x (%d,%d %dx%d) flags %x\n", ARG(0), (int32_t)ARG(2), (int32_t)ARG(3), (int32_t)ARG(4), (int32_t)ARG(5), fl);
    set_pos(w, (int32_t)ARG(2), (int32_t)ARG(3), (int32_t)ARG(4), (int32_t)ARG(5), !(fl & 2), !(fl & 1));
    if (fl & 0x40) { if (!w->visible) { w->visible = 1; send(w->hwnd, 0x18, 1, 0); invalidate_tree(w); } }
    if (fl & 0x80) { if (w->visible) { w->visible = 0; send(w->hwnd, 0x18, 0, 0); } }
    return 1;
}
static uint32_t u_ShowWindow(void) {
    struct wnd *w = wget(ARG(0)); if (!w) return 0;
    int cmd = (int)ARG(1), was = w->visible;
    int show = cmd != 0 && cmd != 6 /*SW_MINIMIZE*/ && cmd != 2;
    LOG("ShowWindow %x %d\n", w->hwnd, cmd);
    if (show != was) { w->visible = show; send(w->hwnd, 0x18 /*WM_SHOWWINDOW*/, (uint32_t)show, 0); }
    if (cmd == 3 /*SW_MAXIMIZE*/) set_pos(w, 0, 0, SCREEN_W, SCREEN_H, 1, 1);
    if (show) {
        invalidate_tree(w);
        if (!(w->style & 0x40000000u) && cmd != 4 && cmd != 8 && active_hwnd != w->hwnd) {
            uint32_t old = active_hwnd; active_hwnd = w->hwnd;
            send(w->hwnd, 0x86 /*WM_NCACTIVATE*/, 1, 0);
            if (!old) send(w->hwnd, 0x1c /*WM_ACTIVATEAPP*/, 1, 0);
            send(w->hwnd, 0x06 /*WM_ACTIVATE*/, 1, old);
            if (!focus_hwnd || !wget(focus_hwnd)) { focus_hwnd = w->hwnd; send(w->hwnd, 0x07 /*WM_SETFOCUS*/, 0, 0); }
        }
    }
    screen_dirty = 1;
    return (uint32_t)was;
}
static uint32_t u_IsWindow(void) { return wget(ARG(0)) != 0 || ARG(0) == DESKTOP; }
static uint32_t u_IsWindowVisible(void) { struct wnd *w = wget(ARG(0)); return w && is_visible(w); }
int user_window_visible(uint32_t hwnd) { struct wnd *w = wget(hwnd); return w && is_visible(w); }
static uint32_t u_IsWindowEnabled(void) { struct wnd *w = wget(ARG(0)); return w && w->enabled; }
static uint32_t u_EnableWindow(void) { struct wnd *w = wget(ARG(0)); if (!w) return 0; int was = !w->enabled; w->enabled = ARG(1) != 0; return (uint32_t)was; }
static uint32_t u_GetParent(void) { struct wnd *w = wget(ARG(0)); return w ? ((w->style & 0x40000000u) ? w->parent : w->owner) : 0; }
static uint32_t u_IsChild(void) {
    struct wnd *w = wget(ARG(1));
    while (w && (w->style & 0x40000000u)) { if (w->parent == ARG(0)) return 1; w = wget(w->parent); }
    return 0;
}
static uint32_t u_GetDesktopWindow(void) { return DESKTOP; }
// GetWindow(hwnd, cmd): z-order = creation order among siblings
static uint32_t u_GetWindow(void) {
    struct wnd *w = wget(ARG(0)); uint32_t cmd = ARG(1);
    if (cmd == 5) { for (int k = 0; k < NW; k++) if (W[k].used && W[k].parent == ARG(0) && (W[k].style & 0x40000000u)) return W[k].hwnd; return 0; }
    if (!w) return 0;
    if (cmd == 4) return w->owner;
    uint32_t par = (w->style & 0x40000000u) ? w->parent : 0;
    int me = (int)((w->hwnd - 0x400) / 8);
    if (cmd == 2) { for (int k = me + 1; k < NW; k++) if (W[k].used && ((W[k].style & 0x40000000u) ? W[k].parent : 0) == par) return W[k].hwnd; return 0; }
    if (cmd == 3) { for (int k = me - 1; k >= 0; k--) if (W[k].used && ((W[k].style & 0x40000000u) ? W[k].parent : 0) == par) return W[k].hwnd; return 0; }
    if (cmd == 0) { for (int k = 0; k < NW; k++) if (W[k].used && ((W[k].style & 0x40000000u) ? W[k].parent : 0) == par) return W[k].hwnd; return 0; }
    return 0;
}
static uint32_t u_GetTopWindow(void) {
    uint32_t p = ARG(0);
    for (int k = 0; k < NW; k++) if (W[k].used && (p ? (W[k].parent == p && (W[k].style & 0x40000000u)) : !(W[k].style & 0x40000000u))) return W[k].hwnd;
    return 0;
}
static uint32_t u_GetWindowRect(void) {
    int x, y, w, h;
    if (!user_window_origin(ARG(0), 0, &x, &y, &w, &h)) return 0;
    rect_wr(ARG(1), (struct rect){ x, y, x + w, y + h });
    return 1;
}
static uint32_t u_GetClientRect(void) {
    int x, y, w, h;
    if (!user_window_origin(ARG(0), 1, &x, &y, &w, &h)) return 0;
    rect_wr(ARG(1), (struct rect){ 0, 0, w, h });
    return 1;
}
static uint32_t u_ClientToScreen(void) {
    int x, y, w, h; if (!user_window_origin(ARG(0), 1, &x, &y, &w, &h)) return 0;
    WR32(ARG(1), RD32(ARG(1)) + (uint32_t)x); WR32(ARG(1) + 4, RD32(ARG(1) + 4) + (uint32_t)y); return 1;
}
static uint32_t u_ScreenToClient(void) {
    int x, y, w, h; if (!user_window_origin(ARG(0), 1, &x, &y, &w, &h)) return 0;
    static int nlog; if (getenv("PERSONA_DEBUG_MOUSE") && nlog++ < 20) fprintf(stderr, "ScreenToClient(%x) origin %d,%d pt %d,%d\n", ARG(0), x, y, RD32(ARG(1)), RD32(ARG(1) + 4));
    WR32(ARG(1), RD32(ARG(1)) - (uint32_t)x); WR32(ARG(1) + 4, RD32(ARG(1) + 4) - (uint32_t)y); return 1;
}
static uint32_t u_MapWindowPoints(void) {
    int fx = 0, fy = 0, tx = 0, ty = 0, w, h;
    if (ARG(0)) user_window_origin(ARG(0), 1, &fx, &fy, &w, &h);
    if (ARG(1)) user_window_origin(ARG(1), 1, &tx, &ty, &w, &h);
    for (uint32_t k = 0; k < ARG(3); k++) {
        uint32_t p = ARG(2) + 8 * k;
        WR32(p, RD32(p) + (uint32_t)(fx - tx)); WR32(p + 4, RD32(p + 4) + (uint32_t)(fy - ty));
    }
    return (uint32_t)(((fy - ty) & 0xffff) << 16 | ((fx - tx) & 0xffff));
}
static uint32_t u_AdjustWindowRectEx(void) {
    struct wnd tmp; memset(&tmp, 0, sizeof tmp); tmp.style = ARG(1); tmp.menu = ARG(2);
    int l, t, r, b; nc_insets(&tmp, &l, &t, &r, &b);
    struct rect rc = rect_rd(ARG(0)); rc.l -= l; rc.t -= t; rc.r += r; rc.b += b; rect_wr(ARG(0), rc);
    return 1;
}
static uint32_t u_AdjustWindowRect(void) {
    struct wnd tmp; memset(&tmp, 0, sizeof tmp); tmp.style = ARG(1); tmp.menu = ARG(2);
    int l, t, r, b; nc_insets(&tmp, &l, &t, &r, &b);
    struct rect rc = rect_rd(ARG(0)); rc.l -= l; rc.t -= t; rc.r += r; rc.b += b; rect_wr(ARG(0), rc);
    return 1;
}
static uint32_t win_from_point(int x, int y) {
    // deepest visible window containing the point; later siblings are on top
    uint32_t best = 0; int depth = -1;
    for (int k = 0; k < NW; k++) {
        if (!W[k].used || !is_visible(&W[k])) continue;
        int wx, wy, ww, wh; user_window_origin(W[k].hwnd, 0, &wx, &wy, &ww, &wh);
        if (x < wx || y < wy || x >= wx + ww || y >= wy + wh) continue;
        int d = 0; for (struct wnd *p = &W[k]; p && (p->style & 0x40000000u); p = wget(p->parent)) d++;
        if (d >= depth) { depth = d; best = W[k].hwnd; }
    }
    return best;
}
static uint32_t u_WindowFromPoint(void) { return win_from_point((int32_t)ARG(0), (int32_t)ARG(1)); }

static uint32_t *wlong_slot(struct wnd *w, int32_t i) {
    static uint32_t dummy;
    if (i >= 0 && w->extra && (uint32_t)i + 4 <= C[w->cls].wnd_extra) return (uint32_t *)GP(w->extra + (uint32_t)i);
    switch (i) {
    case -4: return &w->proc; case -16: return &w->style; case -20: return &w->exstyle; case -21: return &w->userdata;
    case -12: return &w->id; case -6: return &w->inst; case -8: return &w->parent;
    }
    dummy = 0; return &dummy;
}
static uint32_t u_GetWindowLongA(void) { struct wnd *w = wget(ARG(0)); if (!w) return 0; uint32_t v; memcpy(&v, wlong_slot(w, (int32_t)ARG(1)), 4); return v; }
static uint32_t u_SetWindowLongA(void) {
    struct wnd *w = wget(ARG(0)); if (!w) return 0;
    uint32_t *s = wlong_slot(w, (int32_t)ARG(1)); uint32_t old; memcpy(&old, s, 4); uint32_t v = ARG(2); memcpy(s, &v, 4);
    return old;
}
static uint32_t u_GetWindowWord(void) { struct wnd *w = wget(ARG(0)); if (!w || !w->extra) return 0; return RD16(w->extra + ARG(1)); }
static uint32_t u_SetWindowWord(void) { struct wnd *w = wget(ARG(0)); if (!w || !w->extra) return 0; uint32_t o = RD16(w->extra + ARG(1)); WR16(w->extra + ARG(1), ARG(2)); return o; }
static uint32_t u_SetClassLongA(void) {
    struct wnd *w = wget(ARG(0)); if (!w) return 0; struct wclass *c = &C[w->cls]; int32_t i = (int32_t)ARG(1); uint32_t o = 0;
    switch (i) { case -12: o = c->cursor; c->cursor = ARG(2); break; case -10: o = c->bg; c->bg = ARG(2); break;
                 case -24: o = c->proc; c->proc = ARG(2); break; case -14: o = c->icon; c->icon = ARG(2); break; case -26: o = c->style; c->style = ARG(2); break; }
    return o;
}
static uint32_t u_GetClassNameA(void) {
    struct wnd *w = wget(ARG(0)); if (!w || !ARG(2)) return 0;
    const char *n = C[w->cls].name; size_t l = strlen(n); if (l >= ARG(2)) l = ARG(2) - 1;
    memcpy(GP(ARG(1)), n, l); WR8(ARG(1) + (uint32_t)l, 0); return (uint32_t)l;
}
static uint32_t u_GetWindowTextA(void) {
    struct wnd *w = wget(ARG(0)); if (!w || !ARG(2)) return 0;
    size_t l = strlen(w->text); if (l >= ARG(2)) l = ARG(2) - 1;
    memcpy(GP(ARG(1)), w->text, l); WR8(ARG(1) + (uint32_t)l, 0); return (uint32_t)l;
}
static uint32_t u_GetWindowTextLengthA(void) { struct wnd *w = wget(ARG(0)); return w ? (uint32_t)strlen(w->text) : 0; }
static uint32_t u_SetWindowTextA(void) { struct wnd *w = wget(ARG(0)); if (w) gstr_get(ARG(1), w->text, sizeof w->text); return 1; }
static struct prop *prop_find(struct wnd *w, uint32_t key, int create) {
    char n[32] = ""; if (key >= 0x10000) gstr_get(key, n, sizeof n);
    for (int k = 0; k < 16; k++) {
        struct prop *p = &w->props[k];
        if (!p->key) continue;
        if (key < 0x10000 ? p->key == key : (p->key >= 0x10000 && !strcasecmp(p->name, n))) return p;
    }
    if (!create) return 0;
    for (int k = 0; k < 16; k++) if (!w->props[k].key) { w->props[k].key = key ? key : 1; snprintf(w->props[k].name, 32, "%s", n); return &w->props[k]; }
    return 0;
}
static uint32_t u_SetPropA(void) { struct wnd *w = wget(ARG(0)); if (!w) return 0; struct prop *p = prop_find(w, ARG(1), 1); if (!p) return 0; p->val = ARG(2); return 1; }
static uint32_t u_GetPropA(void) { struct wnd *w = wget(ARG(0)); if (!w) return 0; struct prop *p = prop_find(w, ARG(1), 0); return p ? p->val : 0; }
static uint32_t u_RemovePropA(void) { struct wnd *w = wget(ARG(0)); if (!w) return 0; struct prop *p = prop_find(w, ARG(1), 0); if (!p) return 0; uint32_t v = p->val; p->key = 0; return v; }

// ---------------------------------------------------------------- focus / capture
static uint32_t u_SetFocus(void) {
    uint32_t old = focus_hwnd, h = ARG(0);
    if (h == old) return old;
    if (old && wget(old)) send(old, 0x08 /*WM_KILLFOCUS*/, h, 0);
    focus_hwnd = h;
    if (h) send(h, 0x07 /*WM_SETFOCUS*/, old, 0);
    return old;
}
static uint32_t u_GetFocus(void) { return focus_hwnd; }
static uint32_t u_GetActiveWindow(void) { return active_hwnd; }
static uint32_t u_BringWindowToTop(void) { return 1; }
static uint32_t u_SetCapture(void) { uint32_t o = capture_hwnd; capture_hwnd = ARG(0); return o; }
static uint32_t u_ReleaseCapture(void) { capture_hwnd = 0; return 1; }
static uint32_t u_GetCapture(void) { return capture_hwnd; }

// ---------------------------------------------------------------- painting
static uint32_t u_InvalidateRect(void) {
    struct wnd *w = wget(ARG(0));
    if (!w) { for (int k = 0; k < NW; k++) if (W[k].used) invalidate_all(&W[k], ARG(2) != 0); return 1; }
    if (ARG(1)) invalidate(w, rect_rd(ARG(1)), ARG(2) != 0); else invalidate_all(w, ARG(2) != 0);
    return 1;
}
static uint32_t u_InvalidateRgn(void) {
    struct wnd *w = wget(ARG(0)); if (!w) return 1;
    if (ARG(1)) { struct rect r; gdi_region_get_box(ARG(1), &r); invalidate(w, r, ARG(2) != 0); } else invalidate_all(w, ARG(2) != 0);
    return 1;
}
static uint32_t u_ValidateRect(void) { struct wnd *w = wget(ARG(0)); if (w) { w->inval = (struct rect){ 0, 0, 0, 0 }; w->erase = 0; } return 1; }
static uint32_t u_GetUpdateRgn(void) {
    struct wnd *w = wget(ARG(0)); if (!w) return 0;
    extern void gdi_region_set_rect(uint32_t, struct rect);
    gdi_region_set_rect(ARG(1), w->inval);
    return rect_empty(w->inval) ? 1 /*NULLREGION*/ : 2;
}
static uint32_t u_BeginPaint(void) {
    struct wnd *w = wget(ARG(0)); uint32_t ps = ARG(1);
    memset(GP(ps), 0, 64);
    uint32_t dc = gdi_window_dc(ARG(0), 1);
    struct rect r = w ? w->inval : (struct rect){ 0, 0, 0, 0 };
    int erase = w ? w->erase : 0;
    if (w) { w->inval = (struct rect){ 0, 0, 0, 0 }; w->erase = 0; }
    WR32(ps, dc); rect_wr(ps + 8, r);
    if (erase && w) { if (!send(w->hwnd, 0x14 /*WM_ERASEBKGND*/, dc, 0)) WR32(ps + 4, 1); }
    return dc;
}
static uint32_t u_EndPaint(void) { gdi_release_dc(RD32(ARG(1))); return 1; }
static uint32_t u_UpdateWindow(void) {
    struct wnd *w = wget(ARG(0));
    if (w && !rect_empty(w->inval) && is_visible(w)) send(w->hwnd, 0x0f /*WM_PAINT*/, 0, 0);
    return 1;
}
static uint32_t u_GetDC(void) { return gdi_window_dc(ARG(0), 1); }
static uint32_t u_GetWindowDC(void) { return gdi_window_dc(ARG(0), 0); }
static uint32_t u_ReleaseDC(void) { gdi_release_dc(ARG(1)); return 1; }
static uint32_t u_FillRect(void) { gdi_fill_rect(ARG(0), rect_rd(ARG(1)), ARG(2)); return 1; }
static uint32_t u_FrameRect(void) { gdi_frame_rect(ARG(0), rect_rd(ARG(1)), ARG(2)); return 1; }
static uint32_t u_DrawTextA(void) {
    char s[1024]; int n = (int32_t)ARG(2);
    if (n < 0) { gstr_get(ARG(1), s, sizeof s); n = (int)strlen(s); } else { if (n > 1023) n = 1023; memcpy(s, GP(ARG(1)), (size_t)n); s[n] = 0; }
    struct rect r = rect_rd(ARG(3));
    int h = gdi_draw_text(ARG(0), s, n, &r, ARG(4));
    if (ARG(4) & 0x400 /*DT_CALCRECT*/) rect_wr(ARG(3), r);
    return (uint32_t)h;
}
static uint32_t u_DrawEdge(void) {
    struct rect r = rect_rd(ARG(1));
    gdi_frame_rect(ARG(0), r, gdi_sys_color_brush(16));
    return 1;
}
static uint32_t u_DrawFrameControl(void) { gdi_fill_rect(ARG(0), rect_rd(ARG(1)), gdi_sys_color_brush(15)); return 1; }

// ---------------------------------------------------------------- default procedures
static uint32_t def_proc(uint32_t hwnd, uint32_t m, uint32_t wp, uint32_t lp) {
    struct wnd *w = wget(hwnd);
    switch (m) {
    case 0x81: return 1;                                      // WM_NCCREATE
    case 0x10: if (w) destroy(w); return 0;                   // WM_CLOSE
    case 0x0f: if (w) { w->inval = (struct rect){ 0, 0, 0, 0 }; w->erase = 0; } return 0;   // WM_PAINT
    case 0x14: {                                              // WM_ERASEBKGND
        if (!w || !C[w->cls].bg) return 0;
        int x, y, ww, hh; user_window_origin(hwnd, 1, &x, &y, &ww, &hh);
        uint32_t bg = C[w->cls].bg; if (bg <= 31) bg = gdi_sys_color_brush((int)bg - 1);
        gdi_fill_rect(wp, (struct rect){ 0, 0, ww, hh }, bg);
        return 1;
    }
    case 0x20: {                                              // WM_SETCURSOR
        if (w && (lp & 0xffff) == 1 && C[w->cls].cursor) { extern uint32_t cur_cursor; cur_cursor = C[w->cls].cursor; return 1; }
        if (w && w->parent && (w->style & 0x40000000u)) return send(w->parent, m, wp, lp);
        return 0;
    }
    case 0x84: return 1;                                      // WM_NCHITTEST -> HTCLIENT
    case 0x21: return 1;                                      // WM_MOUSEACTIVATE -> MA_ACTIVATE
    case 0x0c: if (w) gstr_get(lp, w->text, sizeof w->text); return 1;   // WM_SETTEXT
    case 0x0d: { if (!w || !wp) return 0; size_t l = strlen(w->text); if (l >= wp) l = wp - 1; memcpy(GP(lp), w->text, l); WR8(lp + (uint32_t)l, 0); return (uint32_t)l; }
    case 0x0e: return w ? (uint32_t)strlen(w->text) : 0;
    case 0x47: {                                              // WM_WINDOWPOSCHANGED -> WM_SIZE / WM_MOVE
        int x, y, ww, hh; user_window_origin(hwnd, 1, &x, &y, &ww, &hh);
        send(hwnd, 0x05, 0, (uint32_t)(ww & 0xffff) | ((uint32_t)hh << 16));
        send(hwnd, 0x03, 0, 0);
        return 0;
    }
    case 0x112: if ((wp & 0xfff0) == 0xf060) send(hwnd, 0x10, 0, 0); return 0;   // WM_SYSCOMMAND SC_CLOSE
    case 0x86: return 1;                                      // WM_NCACTIVATE
    case 0x83: return 0;                                      // WM_NCCALCSIZE
    case 0x24: return 0;
    case 0x7b: return 0;                                      // WM_CONTEXTMENU
    case 0x104: case 0x105: return 0;                         // WM_SYSKEYDOWN/UP: keep Alt quiet
    default: return 0;
    }
}
static uint32_t u_DefWindowProcA(void) { return def_proc(ARG(0), ARG(1), ARG(2), ARG(3)); }
static uint32_t u_DefMDIChildProcA(void) { return def_proc(ARG(0), ARG(1), ARG(2), ARG(3)); }
static uint32_t u_DefDlgProcA(void) { return def_proc(ARG(0), ARG(1), ARG(2), ARG(3)); }
static uint32_t u_DefFrameProcA(void) { return def_proc(ARG(0), ARG(2), ARG(3), ARG(4)); }
static uint32_t builtin_wndproc(void) {
    // system classes (mdiclient, buttons...) are inert boxes
    uint32_t m = ARG(1);
    if (m == 0x0f) { struct wnd *w = wget(ARG(0)); if (w) { w->inval = (struct rect){ 0, 0, 0, 0 }; w->erase = 0; } return 0; }
    return def_proc(ARG(0), m, ARG(2), ARG(3));
}
static uint32_t u_CallWindowProcA(void) { return call_proc(ARG(0), ARG(1), ARG(2), ARG(3), ARG(4)); }
static uint32_t u_SendMessageA(void) { return send(ARG(0), ARG(1), ARG(2), ARG(3)); }

// ---------------------------------------------------------------- message queue
struct msg { uint32_t hwnd, msg, w, l, time; int x, y; };
#define MQ 1024
static struct msg mq[MQ]; static int mq_r, mq_w;
static void post(uint32_t hwnd, uint32_t m, uint32_t w, uint32_t l) {
    if (((mq_w + 1) & (MQ - 1)) == mq_r) return;
    mq[mq_w] = (struct msg){ hwnd, m, w, l, w32_tick_ms(), mouse_x, mouse_y }; mq_w = (mq_w + 1) & (MQ - 1);
}
void w32_post_message(uint32_t hwnd, uint32_t m, uint32_t w, uint32_t l) { post(hwnd, m, w, l); }
static uint32_t u_PostMessageA(void) { post(ARG(0), ARG(1), ARG(2), ARG(3)); return 1; }
static uint32_t u_PostQuitMessage(void) { post(0, 0x12, ARG(0), 0); return 0; }

// timers
struct timer { int used; uint32_t hwnd, id, ms, proc, next; };
static struct timer TM[64];
static uint32_t u_SetTimer(void) {
    uint32_t hwnd = ARG(0), id = ARG(1);
    int k, f = -1;
    for (k = 0; k < 64; k++) if (TM[k].used && TM[k].hwnd == hwnd && hwnd && TM[k].id == id) { f = k; break; }
    if (f < 0) for (k = 0; k < 64; k++) if (!TM[k].used) { f = k; break; }
    if (f < 0) return 0;
    if (!hwnd) id = 0x100 + (uint32_t)f;
    uint32_t ms = ARG(2) ? ARG(2) : 1;
    TM[f] = (struct timer){ 1, hwnd, id, ms, ARG(3), w32_tick_ms() + ms };
    LOG("SetTimer(%x, %u, %u ms, proc %08x)\n", hwnd, id, ms, ARG(3));
    return id;
}
static uint32_t u_KillTimer(void) {
    for (int k = 0; k < 64; k++) if (TM[k].used && TM[k].hwnd == ARG(0) && TM[k].id == ARG(1)) { TM[k].used = 0; return 1; }
    return 0;
}
static int in_filter(uint32_t m, uint32_t lo, uint32_t hi) { return (!lo && !hi) || (m >= lo && m <= hi); }
static int hwnd_match(uint32_t want, uint32_t h) {
    if (!want) return 1;
    if (want == 0xffffffffu) return h == 0;
    for (struct wnd *w = wget(h); w; w = (w->style & 0x40000000u) ? wget(w->parent) : 0) if (w->hwnd == want) return 1;
    return 0;
}
static uint32_t mouse_target(int *cx, int *cy) {
    uint32_t h = capture_hwnd ? capture_hwnd : win_from_point(mouse_x, mouse_y);
    int x, y, w, hh; if (h && user_window_origin(h, 1, &x, &y, &w, &hh)) { *cx = mouse_x - x; *cy = mouse_y - y; }
    else { *cx = mouse_x; *cy = mouse_y; }
    return h;
}
// fetch the next message matching the filter; returns 1 if found (removing it when `remove`)
static int next_msg(struct msg *out, uint32_t hwnd, uint32_t lo, uint32_t hi, int remove) {
    for (int i = mq_r; i != mq_w; i = (i + 1) & (MQ - 1)) {
        struct msg *m = &mq[i];
        if (!in_filter(m->msg, lo, hi) || !hwnd_match(hwnd, m->hwnd)) continue;
        *out = *m;
        if (remove) {
            for (int j = i; j != mq_r; j = (j - 1) & (MQ - 1)) mq[j] = mq[(j - 1) & (MQ - 1)];
            mq_r = (mq_r + 1) & (MQ - 1);
        }
        return 1;
    }
    // timers
    uint32_t now = w32_tick_ms();
    if (in_filter(0x113, lo, hi)) for (int k = 0; k < 64; k++) {
        struct timer *t = &TM[k];
        if (!t->used || (int32_t)(now - t->next) < 0 || !hwnd_match(hwnd, t->hwnd)) continue;
        *out = (struct msg){ t->hwnd, 0x113, t->id, t->proc, now, mouse_x, mouse_y };
        if (remove) { t->next = now + t->ms; }
        return 1;
    }
    // paint
    if (in_filter(0x0f, lo, hi)) for (int k = 0; k < NW; k++) {
        struct wnd *w = &W[k];
        if (!w->used || rect_empty(w->inval) || !is_visible(w) || !hwnd_match(hwnd, w->hwnd)) continue;
        *out = (struct msg){ w->hwnd, 0x0f, 0, 0, now, mouse_x, mouse_y };
        return 1;
    }
    return 0;
}
static void put_msg(uint32_t p, struct msg *m) {
    WR32(p, m->hwnd); WR32(p + 4, m->msg); WR32(p + 8, m->w); WR32(p + 12, m->l); WR32(p + 16, m->time);
    WR32(p + 20, (uint32_t)m->x); WR32(p + 24, (uint32_t)m->y);
}
static void pump_throttled(void);
static uint32_t u_PeekMessageA(void) {
    pump_throttled();
    struct msg m;
    if (!next_msg(&m, ARG(1), ARG(2), ARG(3), ARG(4) & 1)) {
        video_maybe_present();
        static int spins; if (++spins > 32) { spins = 0; thr_sleep(1); } else thr_yield();
        return 0;
    }
    put_msg(ARG(0), &m);
    if (getenv("PERSONA_DEBUG_MSG")) fprintf(stderr, "[%u] Peek -> hwnd %x msg %x w %x l %x remove %d from %08x\n", w32_tick_ms(), m.hwnd, m.msg, m.w, m.l, ARG(4) & 1, RD32(ESP));
    return 1;
}
static uint32_t u_GetMessageA(void) {
    struct msg m;
    for (;;) {
        pump_throttled();
        if (next_msg(&m, ARG(1), ARG(2), ARG(3), 1)) break;
        video_maybe_present();
        thr_sleep(2);
        IRQCHK();
    }
    put_msg(ARG(0), &m);
    return m.msg == 0x12 ? 0 : 1;
}
static uint32_t u_TranslateMessage(void) {
    uint32_t p = ARG(0), m = RD32(p + 4), vk = RD32(p + 8);
    if (m == 0x100) {
        int ch = 0, sh = keys[0x10] & 0x80;
        if (vk >= 'A' && vk <= 'Z') ch = sh ? (int)vk : (int)vk + 32;
        else if (vk >= '0' && vk <= '9') ch = (int)vk;
        else if (vk == 0x20 || vk == 0x0d || vk == 0x08 || vk == 0x1b || vk == 0x09) ch = (int)vk;
        else if (vk >= 0x60 && vk <= 0x69) ch = '0' + (int)(vk - 0x60);
        else if (vk == 0xbe) ch = '.'; else if (vk == 0xbc) ch = ','; else if (vk == 0xbd) ch = '-';
        if (ch) post(RD32(p), 0x102 /*WM_CHAR*/, (uint32_t)ch, RD32(p + 12));
    }
    return 1;
}
static uint32_t u_DispatchMessageA(void) {
    uint32_t p = ARG(0), h = RD32(p), m = RD32(p + 4);
    if (getenv("PERSONA_DEBUG_MSG")) fprintf(stderr, "[%u] Dispatch hwnd %x msg %x from %08x\n", w32_tick_ms(), h, m, RD32(ESP));
    if (m == 0x113 && RD32(p + 12)) { uint32_t a[4] = { h, 0x113, RD32(p + 8), w32_tick_ms() }; return guest_call(RD32(p + 12), 4, a); }
    struct wnd *w = wget(h); if (!w) return 0;
    if (m >= 0x200 && m <= 0x209) {
        send(h, 0x20 /*WM_SETCURSOR*/, h, 1u | (m << 16));
    }
#ifdef RT_DEBUG
    extern void rt_prof_reset(void), rt_prof_dump_all(const char *);
    int dbg = (m == 0x201 || m == 0x202) && getenv("PERSONA_DEBUG_DISPATCH");
    if (dbg) rt_prof_reset();
#endif
    uint32_t r = send(h, m, RD32(p + 8), RD32(p + 12));
#ifdef RT_DEBUG
    if (dbg) { char t[64]; snprintf(t, sizeof t, "[%u] dispatch %x", w32_tick_ms(), m); rt_prof_dump_all(t); }
#endif
    if (m == 0x0f && (w = wget(h))) { w->inval = (struct rect){ 0, 0, 0, 0 }; w->erase = 0; }   // paint handlers that skip BeginPaint
    return r;
}
static uint32_t u_IsDialogMessageA(void) { return 0; }
static uint32_t u_TranslateAcceleratorA(void) { return 0; }
static uint32_t u_TranslateMDISysAccel(void) { return 0; }

// ---------------------------------------------------------------- input
static void key_event(int vk, int down) {
    if (!vk) return;
    int was = keys[vk] & 0x80;
    if (down && !was) keys[vk] ^= 1;
    keys[vk] = (uint8_t)((keys[vk] & 1) | (down ? 0x80 : 0));
    uint32_t target = focus_hwnd ? focus_hwnd : active_hwnd;
    if (down) post(target, vk == 0x12 ? 0x104 : 0x100, (uint32_t)vk, (was ? 0x40000000u : 0) | 1);
    else post(target, vk == 0x12 ? 0x105 : 0x101, (uint32_t)vk, 0xC0000001u);
}
static uint32_t mk_mbuttons(void) { return ((keys[1] & 0x80) ? 1 : 0) | ((keys[2] & 0x80) ? 2 : 0) | ((keys[0x10] & 0x80) ? 4 : 0) | ((keys[0x11] & 0x80) ? 8 : 0); }
static void mouse_msg(uint32_t m) {
    int cx, cy; uint32_t h = mouse_target(&cx, &cy);
    if (!h) return;
    // coalesce motion
    if (m == 0x200) {
        int last = (mq_w - 1) & (MQ - 1);
        if (mq_r != mq_w && mq[last].msg == 0x200 && mq[last].hwnd == h) { mq[last].l = (uint32_t)(cx & 0xffff) | ((uint32_t)(cy & 0xffff) << 16); mq[last].x = mouse_x; mq[last].y = mouse_y; return; }
    }
    post(h, m, mk_mbuttons(), (uint32_t)(cx & 0xffff) | ((uint32_t)(cy & 0xffff) << 16));
}
static void mouse_button(int right, int down) {
    keys[right ? 2 : 1] = down ? 0x80 : 0;
    if (down) { uint32_t h = win_from_point(mouse_x, mouse_y); if (h) send(h, 0x21 /*WM_MOUSEACTIVATE*/, active_hwnd, 1); }
    mouse_msg(right ? (down ? 0x204 : 0x205) : (down ? 0x201 : 0x202));
}
static uint32_t u_GetKeyState(void) { uint8_t k = keys[ARG(0) & 0xff]; return (uint32_t)(int16_t)(((k & 0x80) ? 0x8000 : 0) | (k & 1)); }
static uint32_t u_GetAsyncKeyState(void) {
    pump_throttled();
    if (getenv("PERSONA_DEBUG_PUMP") && (ARG(0) & 0xff) == 1 && (keys[1] & 0x80)) { static uint32_t last; uint32_t t = w32_tick_ms(); if (t - last > 50) fprintf(stderr, "[%u] button seen down\n", t); last = t; }
    return (keys[ARG(0) & 0xff] & 0x80) ? 0x8000 : 0;
}
static uint32_t u_GetKeyboardState(void) { memcpy(GP(ARG(0)), keys, 256); return 1; }
static uint32_t u_ToAscii(void) {
    uint32_t vk = ARG(0); int ch = 0;
    if (vk >= 'A' && vk <= 'Z') ch = (keys[0x10] & 0x80) ? (int)vk : (int)vk + 32;
    else if ((vk >= '0' && vk <= '9') || vk == 0x20 || vk == 0x0d || vk == 0x08 || vk == 0x1b) ch = (int)vk;
    if (!ch) return 0;
    WR16(ARG(3), (uint32_t)ch); return 1;
}
static uint32_t u_GetCursorPos(void) { pump_throttled(); WR32(ARG(0), (uint32_t)mouse_x); WR32(ARG(0) + 4, (uint32_t)mouse_y); return 1; }

static int vk_of(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return 'A' + (s - SDL_SCANCODE_A);
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return '1' + (s - SDL_SCANCODE_1);
    if (s == SDL_SCANCODE_0) return '0';
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return 0x70 + (s - SDL_SCANCODE_F1);
    switch (s) {
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x0d;
    case SDL_SCANCODE_ESCAPE: return 0x1b; case SDL_SCANCODE_BACKSPACE: return 0x08; case SDL_SCANCODE_TAB: return 0x09;
    case SDL_SCANCODE_SPACE: return 0x20; case SDL_SCANCODE_LEFT: return 0x25; case SDL_SCANCODE_UP: return 0x26;
    case SDL_SCANCODE_RIGHT: return 0x27; case SDL_SCANCODE_DOWN: return 0x28; case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return 0x10;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x11; case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x12;
    case SDL_SCANCODE_PAGEUP: return 0x21; case SDL_SCANCODE_PAGEDOWN: return 0x22; case SDL_SCANCODE_END: return 0x23;
    case SDL_SCANCODE_HOME: return 0x24; case SDL_SCANCODE_INSERT: return 0x2d; case SDL_SCANCODE_DELETE: return 0x2e;
    case SDL_SCANCODE_PERIOD: return 0xbe; case SDL_SCANCODE_COMMA: return 0xbc; case SDL_SCANCODE_MINUS: return 0xbd;
    default: return 0;
    }
}

// scripted input for headless testing: PERSONA_CLICKS="t:x:y,..." (seconds), PERSONA_KEYS="t:vk,..."
struct sev { double t; int kind, a, b; int done; };
static struct sev sevs[8192]; static int nsev;
static void parse_script(void) {
    const char *k = getenv("PERSONA_KEYS");
    while (k && *k && nsev < 8100) {
        double t; int vk, n = 0;
        if (sscanf(k, "%lf:%i%n", &t, &vk, &n) < 2) break;
        sevs[nsev++] = (struct sev){ t, 0, vk, 1, 0 };
        sevs[nsev++] = (struct sev){ t + 0.15, 0, vk, 0, 0 };
        k += n; if (*k == ',') k++;
    }
    const char *c = getenv("PERSONA_CLICKS");
    while (c && *c && nsev < 8100) {
        double t; int x, y, ms = 120, n = 0;
        if (sscanf(c, "%lf:%d:%d:%d%n", &t, &x, &y, &ms, &n) < 4) { ms = 120; if (sscanf(c, "%lf:%d:%d%n", &t, &x, &y, &n) < 3) break; }
        if (!getenv("PERSONA_CLICK_RAW")) { x -= 16; y -= 16; }
        sevs[nsev++] = (struct sev){ t - 0.5, 3, x, y, 0 };     // move there first
        sevs[nsev++] = (struct sev){ t, 1, x, y, 0 };
        sevs[nsev++] = (struct sev){ t + ms / 1000.0, 2, x, y, 0 };
        c += n; if (*c == ',') c++;
    }
}
static void run_script(void) {
    double now = (SDL_GetTicks() - t0_ms) / 1000.0;
    if (nsev && getenv("PERSONA_JITTER")) { static double lastj; if (now - lastj > 0.1) { lastj = now; mouse_msg(0x200); } }
    if (getenv("PERSONA_DEBUG_PUMP")) { static double last; static int n; n++; if (now - last >= 1.0) { fprintf(stderr, "pumps/s %d, asks %d\n", n, 0); n = 0; last = now; } }
    for (int k = 0; k < nsev; k++) {
        struct sev *e = &sevs[k];
        if (e->done || now < e->t) continue;
        e->done = 1;
        if (e->kind == 0) key_event(e->a, e->b);
        else { mouse_x = e->a; mouse_y = e->b; mouse_msg(0x200); if (e->kind != 3) mouse_button(0, e->kind == 1); }
    }
}

// ---------------------------------------------------------------- event pump
extern int video_headless;
extern void video_window_to_screen(float wx, float wy, int *x, int *y);
extern void video_toggle_fullscreen(void);
void w32_pump_events(void) {
    if (thr_current() != 0) return;
    run_script();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            fprintf(stderr, "quit requested\n");
            w32_shutdown(); exit(0);
        case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: {
            int down = e.type == SDL_EVENT_KEY_DOWN;
            if (down && e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)) { video_toggle_fullscreen(); break; }
            if (down && e.key.repeat) { int vk = vk_of(e.key.scancode); if (vk) post(focus_hwnd ? focus_hwnd : active_hwnd, 0x100, (uint32_t)vk, 0x40000001u); break; }
            key_event(vk_of(e.key.scancode), down);
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            video_window_to_screen(e.motion.x, e.motion.y, &mouse_x, &mouse_y);
            mouse_msg(0x200); screen_dirty = 1;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
            video_window_to_screen(e.button.x, e.button.y, &mouse_x, &mouse_y);
            if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)
                mouse_button(e.button.button == SDL_BUTTON_RIGHT, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            break;
        default: break;
        }
    }
}
static void pump_throttled(void) {
    static uint32_t last;
    uint32_t now = (uint32_t)SDL_GetTicks();
    if (now - last >= 4) { last = now; w32_pump_events(); }
}

// ---------------------------------------------------------------- cursors
#define NCUR 64
static struct cursor CUR[NCUR];
uint32_t cur_cursor = 0x7f00;      // IDC_ARROW
static int cursor_count = 0;        // ShowCursor counter (visible when >= 0)
struct cursor *user_cursor_get(uint32_t h) { uint32_t k = h - 0x7700u; return (k < NCUR && CUR[k].used) ? &CUR[k] : 0; }
uint32_t user_current_cursor(void) { return cur_cursor; }
int user_cursor_shown(void) { return cursor_count >= 0 && cur_cursor != 0; }
static uint32_t new_cursor(void) { for (int k = 1; k < NCUR; k++) if (!CUR[k].used) { memset(&CUR[k], 0, sizeof CUR[k]); CUR[k].used = 1; return 0x7700u + k; } return 0; }
// CreateCursor(inst, hx, hy, w, h, and, xor): 1-bpp word-aligned planes
static uint32_t u_CreateCursor(void) {
    uint32_t h = new_cursor(); struct cursor *c = user_cursor_get(h); if (!c) return 0;
    int w = (int)ARG(3), hh = (int)ARG(4); if (w > 32) w = 32; if (hh > 32) hh = 32;
    c->w = w; c->h = hh; c->hx = (int)ARG(1); c->hy = (int)ARG(2);
    int pitch = (((int)ARG(3) + 15) / 16) * 2;
    for (int y = 0; y < hh; y++) for (int x = 0; x < w; x++) {
        c->and_[y * 32 + x] = (RD8(ARG(5) + (uint32_t)(y * pitch + x / 8)) >> (7 - x % 8)) & 1;
        c->xor_[y * 32 + x] = (RD8(ARG(6) + (uint32_t)(y * pitch + x / 8)) >> (7 - x % 8)) & 1;
    }
    return h;
}
static uint32_t cursor_from_resource(uint32_t name) {
    uint32_t sz, de = res_find(12 /*RT_GROUP_CURSOR*/, name, &sz);
    if (!de) return 0;
    uint32_t grp = 0x400000u + RD32(de);
    uint32_t id = RD16(grp + 6 + 12);
    uint32_t de2 = res_find(1 /*RT_CURSOR*/, id, &sz); if (!de2) return 0;
    uint32_t p = 0x400000u + RD32(de2);
    uint32_t h = new_cursor(); struct cursor *c = user_cursor_get(h); if (!c) return 0;
    c->hx = RD16(p); c->hy = RD16(p + 2);
    uint32_t bi = p + 4; int w = (int32_t)RD32(bi + 4), hh = (int32_t)RD32(bi + 8) / 2;
    uint32_t bits = bi + RD32(bi) + 8;          // 2-entry color table
    int pitch = ((w + 31) / 32) * 4;
    c->w = w > 32 ? 32 : w; c->h = hh > 32 ? 32 : hh;
    for (int y = 0; y < c->h; y++) for (int x = 0; x < c->w; x++) {
        int sy = hh - 1 - y;
        c->xor_[y * 32 + x] = (RD8(bits + (uint32_t)(sy * pitch + x / 8)) >> (7 - x % 8)) & 1;
        c->and_[y * 32 + x] = (RD8(bits + (uint32_t)((sy + hh) * pitch + x / 8)) >> (7 - x % 8)) & 1;
    }
    return h;
}
static uint32_t u_LoadCursorA(void) {
    uint32_t name = ARG(1);
    if (ARG(0) && name) { uint32_t h = cursor_from_resource(name); if (h) return h; }
    if (name < 0x10000) return name;             // stock cursor ids are their own handles
    return 0x7f00;
}
static uint32_t u_SetCursor(void) { uint32_t o = cur_cursor; cur_cursor = ARG(0); screen_dirty = 1; return o; }
static uint32_t u_ShowCursor(void) { cursor_count += ARG(0) ? 1 : -1; screen_dirty = 1; return (uint32_t)cursor_count; }
static uint32_t u_DestroyCursor(void) { struct cursor *c = user_cursor_get(ARG(0)); if (c) c->used = 0; return 1; }
static uint32_t u_LoadIconA(void) { return 0x7a00; }
static uint32_t u_DrawIcon(void) { return 1; }
static uint32_t u_LoadBitmapA(void) {
    uint32_t sz, de = res_find(2 /*RT_BITMAP*/, ARG(1), &sz);
    if (!de) return 0;
    uint32_t bi = 0x400000u + RD32(de);
    return gdi_bitmap_from_dib(bi, 0);
}

// ---------------------------------------------------------------- resources: strings, accelerators, dialogs
static uint32_t u_LoadStringA(void) {
    uint32_t id = ARG(1), buf = ARG(2), cap = ARG(3), sz;
    if (!cap) return 0;
    uint32_t de = res_find(6 /*RT_STRING*/, (id >> 4) + 1, &sz);
    WR8(buf, 0);
    if (!de) return 0;
    uint32_t p = 0x400000u + RD32(de);
    for (uint32_t k = 0; k < (id & 15); k++) p += 2 + 2 * RD16(p);
    uint32_t n = RD16(p);
    char *s = SDL_iconv_string("CP932", "UTF-16LE", (const char *)GP(p + 2), 2 * n);
    uint32_t l = 0;
    if (s) { l = (uint32_t)strlen(s); if (l >= cap) l = cap - 1; memcpy(GP(buf), s, l); SDL_free(s); }
    WR8(buf + l, 0);
    return l;
}
static uint32_t u_LoadAcceleratorsA(void) { return 0x7b00; }
static uint32_t u_DialogBoxParamA(void) {
    char n[64] = ""; if (ARG(1) >= 0x10000) gstr_get(ARG(1), n, sizeof n);
    fprintf(stderr, "DialogBoxParam(%x '%s') -> IDCANCEL\n", ARG(1), n);
    return 2;
}
static uint32_t u_CreateDialog(void) { fprintf(stderr, "CreateDialog -> NULL\n"); return 0; }
static uint32_t u_MessageBoxA(void) {
    char t[1024], c[256]; gstr_get(ARG(1), t, sizeof t); gstr_get(ARG(2), c, sizeof c);
    char *tu = SDL_iconv_string("UTF-8", "CP932", t, strlen(t) + 1), *cu = SDL_iconv_string("UTF-8", "CP932", c, strlen(c) + 1);
    fprintf(stderr, "MessageBox [%s]: %s\n", cu ? cu : c, tu ? tu : t);
    SDL_free(tu); SDL_free(cu);
    uint32_t type = ARG(3) & 0xf;
    return type == 4 || type == 3 ? 6 /*IDYES*/ : 1 /*IDOK*/;
}

// ---------------------------------------------------------------- misc
static uint32_t u_GetSystemMetrics(void) {
    switch (ARG(0)) {
    case 0: case 16: case 78: return SCREEN_W; case 1: case 79: return SCREEN_H; case 17: return SCREEN_H - 18;
    case 2: case 3: case 9: case 10: case 20: case 21: return 16; case 4: return 18; case 5: case 6: return 1;
    case 7: case 8: return 3; case 11: case 12: case 13: case 14: return 32; case 15: return 19; case 19: return 1;
    case 28: return 112; case 29: return 27; case 30: case 31: return 18; case 32: case 33: return 4;
    case 36: case 37: return 4; case 42: return 1; case 43: return 2; case 49: case 50: return 16; case 61: return SCREEN_W + 8; case 62: return SCREEN_H + 8;
    default: return 0;
    }
}
uint32_t sys_color(int i) {
    static const uint32_t c[31] = { 0xc0c0c0, 0x808000, 0x800000, 0x808080, 0xc0c0c0, 0xffffff, 0x000000, 0x000000, 0x000000, 0xffffff,
                                    0xc0c0c0, 0xc0c0c0, 0x808080, 0x800000, 0xffffff, 0xc0c0c0, 0x808080, 0x808080, 0x000000, 0xc0c0c0,
                                    0xffffff, 0x000000, 0xdfdfdf, 0x000000, 0xffffe1, 0, 0, 0, 0, 0, 0 };
    return (i >= 0 && i < 31) ? c[i] : 0;   // COLORREF (0x00BBGGRR)
}
static uint32_t u_GetSysColor(void) { return sys_color((int)ARG(0)); }
static uint32_t u_SystemParametersInfoA(void) {
    if (ARG(0) == 48 && ARG(2)) { rect_wr(ARG(2), (struct rect){ 0, 0, SCREEN_W, SCREEN_H }); return 1; }
    if (ARG(2) && ARG(0) < 0x100) WR32(ARG(2), 0);
    return 1;
}
static uint32_t u_one(void) { return 1; }
static uint32_t u_zero(void) { return 0; }
static uint32_t u_SetRect(void) { rect_wr(ARG(0), (struct rect){ (int32_t)ARG(1), (int32_t)ARG(2), (int32_t)ARG(3), (int32_t)ARG(4) }); return 1; }
static uint32_t u_IsRectEmpty(void) { return rect_empty(rect_rd(ARG(0))); }
static uint32_t u_EqualRect(void) { return !memcmp(GP(ARG(0)), GP(ARG(1)), 16); }
static uint32_t u_IntersectRect(void) { struct rect r = rect_isect(rect_rd(ARG(1)), rect_rd(ARG(2))); rect_wr(ARG(0), r); return !rect_empty(r); }
static uint32_t u_InflateRect(void) { struct rect r = rect_rd(ARG(0)); int dx = (int32_t)ARG(1), dy = (int32_t)ARG(2); r.l -= dx; r.r += dx; r.t -= dy; r.b += dy; rect_wr(ARG(0), r); return 1; }
static uint32_t u_OffsetRect(void) { struct rect r = rect_rd(ARG(0)); int dx = (int32_t)ARG(1), dy = (int32_t)ARG(2); r.l += dx; r.r += dx; r.t += dy; r.b += dy; rect_wr(ARG(0), r); return 1; }
static uint32_t u_PtInRect(void) { struct rect r = rect_rd(ARG(0)); int x = (int32_t)ARG(1), y = (int32_t)ARG(2); return x >= r.l && x < r.r && y >= r.t && y < r.b; }
static uint32_t u_CharUpperA(void) {
    uint32_t p = ARG(0);
    if (p < 0x10000) return (uint32_t)toupper((int)p);
    for (; RD8(p); p++) { uint8_t c = RD8(p); if (c >= 0x81 && (c <= 0x9f || c >= 0xe0)) { p++; if (!RD8(p)) break; continue; } WR8(p, (uint32_t)toupper(c)); }
    return ARG(0);
}
static uint32_t u_CharUpperBuffA(void) {
    for (uint32_t k = 0; k < ARG(1); k++) { uint8_t c = RD8(ARG(0) + k); if (c >= 0x81 && (c <= 0x9f || c >= 0xe0)) { k++; continue; } WR8(ARG(0) + k, (uint32_t)toupper(c)); }
    return ARG(1);
}
static uint32_t u_CharNextA(void) { uint8_t c = RD8(ARG(0)); if (!c) return ARG(0); return ARG(0) + (((c >= 0x81 && c <= 0x9f) || c >= 0xe0) && RD8(ARG(0) + 1) ? 2 : 1); }
static uint32_t u_CharToOemBuffA(void) { if (ARG(0) != ARG(1)) memmove(GP(ARG(1)), GP(ARG(0)), ARG(2)); return 1; }
static uint32_t u_OemToCharA(void) { if (ARG(0) != ARG(1)) gstr_put(ARG(1), (char *)GP(ARG(0))); return 1; }
static uint32_t u_GetCaretBlinkTime(void) { return 530; }
static uint32_t u_GetDoubleClickTime(void) { return 500; }
static uint32_t u_GetMenuCheckMarkDimensions(void) { return 0x000d000d; }
static uint32_t u_CreateMenu(void) { static uint32_t n = 0x7c00; return ++n; }

// wsprintf: Windows format subset (%d %u %x %X %s %c %ld %lu %lx, flags/width/precision)
static int w_format(char *out, int cap, uint32_t fmt, uint32_t args) {
    int o = 0;
    for (uint32_t p = fmt;; p++) {
        char c = (char)RD8(p);
        if (!c) break;
        if (c != '%') { if (o < cap - 1) out[o++] = c; continue; }
        char spec[32]; int s = 0; spec[s++] = '%';
        c = (char)RD8(++p);
        while (c && strchr("-+ #0123456789.", c) && s < 20) { spec[s++] = c; c = (char)RD8(++p); }
        while (c == 'l' || c == 'h') c = (char)RD8(++p);
        if (!c) break;
        char buf[1024];
        if (c == 's') { char str[1024]; gstr_get(RD32(args), str, sizeof str); args += 4; spec[s++] = 's'; spec[s] = 0; snprintf(buf, sizeof buf, spec, str); }
        else if (c == 'c') { spec[s++] = 'c'; spec[s] = 0; snprintf(buf, sizeof buf, spec, (int)RD32(args)); args += 4; }
        else if (c == 'd' || c == 'i') { spec[s++] = 'd'; spec[s] = 0; snprintf(buf, sizeof buf, spec, (int32_t)RD32(args)); args += 4; }
        else if (c == 'u' || c == 'x' || c == 'X') { spec[s++] = c; spec[s] = 0; snprintf(buf, sizeof buf, spec, RD32(args)); args += 4; }
        else { buf[0] = c; buf[1] = 0; }
        for (char *b = buf; *b && o < cap - 1; b++) out[o++] = *b;
    }
    out[o] = 0;
    return o;
}
static uint32_t u_wsprintfA(void) { char b[1025]; int n = w_format(b, 1025, ARG(1), ESP + 12); gstr_put(ARG(0), b); return (uint32_t)n; }

const struct api API_USER32[] = {
    A(SetClipboardViewer, u_zero, 4), A(SetMenu, u_one, 8), A(WinHelpA, u_one, 16), A(RegisterClipboardFormatA, u_one, 4),
    A(CharUpperBuffA, u_CharUpperBuffA, 8), A(ChangeClipboardChain, u_one, 8), A(SetClipboardData, u_zero, 8), A(SetMessageQueue, u_one, 4),
    A(DestroyAcceleratorTable, u_one, 4), A(LoadAcceleratorsA, u_LoadAcceleratorsA, 8), A(GetMenuState, u_zero, 12), A(EnableMenuItem, u_zero, 12),
    A(SystemParametersInfoA, u_SystemParametersInfoA, 16), A(CreateAcceleratorTableA, u_LoadAcceleratorsA, 8), A(DialogBoxParamA, u_DialogBoxParamA, 20),
    A(CopyAcceleratorTableA, u_zero, 12), A(DestroyMenu, u_one, 4), A(GetMenuItemID, u_zero, 8), A(HiliteMenuItem, u_one, 16), A(RemoveMenu, u_one, 12),
    A(GetMenuStringA, u_zero, 20), A(GetSubMenu, u_zero, 8), A(ModifyMenuA, u_one, 20), A(InsertMenuA, u_one, 20), A(DrawMenuBar, u_one, 4),
    A(wsprintfA, u_wsprintfA, -1), A(GetMenuItemCount, u_zero, 4), A(DeleteMenu, u_one, 12), A(GetMenuCheckMarkDimensions, u_GetMenuCheckMarkDimensions, 0),
    A(CreateMenu, u_CreateMenu, 0), A(CheckMenuItem, u_zero, 12), A(AppendMenuA, u_one, 16), A(TrackPopupMenu, u_zero, 28), A(CreatePopupMenu, u_CreateMenu, 0),
    A(GetCaretBlinkTime, u_GetCaretBlinkTime, 0), A(GetCapture, u_GetCapture, 0), A(ReleaseCapture, u_ReleaseCapture, 0), A(IsWindow, u_IsWindow, 4),
    A(IsChild, u_IsChild, 8), A(DefWindowProcA, u_DefWindowProcA, 16), A(IsZoomed, u_zero, 4), A(GetSystemMetrics, u_GetSystemMetrics, 4),
    A(GetAsyncKeyState, u_GetAsyncKeyState, 4), A(GetKeyboardState, u_GetKeyboardState, 4), A(ToAscii, u_ToAscii, 20), A(GetKeyState, u_GetKeyState, 4),
    A(SetTimer, u_SetTimer, 16), A(PeekMessageA, u_PeekMessageA, 20), A(GetMessageA, u_GetMessageA, 16), A(KillTimer, u_KillTimer, 8),
    A(IsDialogMessageA, u_IsDialogMessageA, 8), A(TranslateMDISysAccel, u_TranslateMDISysAccel, 8), A(TranslateAcceleratorA, u_TranslateAcceleratorA, 12),
    A(TranslateMessage, u_TranslateMessage, 4), A(DispatchMessageA, u_DispatchMessageA, 4), A(PostMessageA, u_PostMessageA, 16),
    A(BeginPaint, u_BeginPaint, 8), A(EndPaint, u_EndPaint, 8), A(CallNextHookEx, u_zero, 16), A(PtInRect, u_PtInRect, 12), A(SetCursor, u_SetCursor, 4),
    A(SetClassLongA, u_SetClassLongA, 12), A(GetPropA, u_GetPropA, 8), A(GetClassNameA, u_GetClassNameA, 12), A(IsDlgButtonChecked, u_zero, 8),
    A(CreateDialogIndirectParamA, u_CreateDialog, 20), A(EqualRect, u_EqualRect, 8), A(CharToOemBuffA, u_CharToOemBuffA, 12),
    A(CallWindowProcA, u_CallWindowProcA, 20), A(GetWindow, u_GetWindow, 8), A(IntersectRect, u_IntersectRect, 12), A(InflateRect, u_InflateRect, 12),
    A(ShowWindow, u_ShowWindow, 8), A(WindowFromPoint, u_WindowFromPoint, 8), A(SendDlgItemMessageA, u_zero, 20), A(GetDlgItem, u_zero, 8),
    A(OffsetRect, u_OffsetRect, 12), A(MoveWindow, u_MoveWindow, 24), A(DestroyWindow, u_DestroyWindow, 4), A(IsIconic, u_zero, 4),
    A(FillRect, u_FillRect, 12), A(DrawFrameControl, u_DrawFrameControl, 16), A(SetCapture, u_SetCapture, 4), A(GetCursorPos, u_GetCursorPos, 4),
    A(GetDlgCtrlID, u_zero, 4), A(SetScrollPos, u_zero, 16), A(ValidateRect, u_ValidateRect, 8), A(GetWindowRect, u_GetWindowRect, 8),
    A(ScreenToClient, u_ScreenToClient, 8), A(SetWindowPos, u_SetWindowPos, 28), A(GetWindowTextLengthA, u_GetWindowTextLengthA, 4),
    A(GetWindowTextA, u_GetWindowTextA, 12), A(IsWindowVisible, u_IsWindowVisible, 4), A(MapWindowPoints, u_MapWindowPoints, 16),
    A(UpdateWindow, u_UpdateWindow, 4), A(SetWindowTextA, u_SetWindowTextA, 8), A(CreateWindowExA, u_CreateWindowExA, 48),
    A(SetWindowLongA, u_SetWindowLongA, 12), A(SetScrollRange, u_one, 20), A(GetClientRect, u_GetClientRect, 8), A(SendMessageA, u_SendMessageA, 16),
    A(InvalidateRect, u_InvalidateRect, 12), A(GetDC, u_GetDC, 4), A(CreateCursor, u_CreateCursor, 28), A(DestroyCursor, u_DestroyCursor, 4),
    A(LoadCursorA, u_LoadCursorA, 8), A(OemToCharA, u_OemToCharA, 8), A(GetDesktopWindow, u_GetDesktopWindow, 0), A(GetMenu, u_zero, 4),
    A(GetDoubleClickTime, u_GetDoubleClickTime, 0), A(GetWindowDC, u_GetWindowDC, 4), A(ReleaseDC, u_ReleaseDC, 8), A(GetSysColor, u_GetSysColor, 4),
    A(LoadIconA, u_LoadIconA, 8), A(GetFocus, u_GetFocus, 0), A(GetWindowLongA, u_GetWindowLongA, 8), A(GetParent, u_GetParent, 4),
    A(IsWindowEnabled, u_IsWindowEnabled, 4), A(LoadStringA, u_LoadStringA, 16), A(EnableWindow, u_EnableWindow, 8), A(SetFocus, u_SetFocus, 4),
    A(GetClassInfoA, u_GetClassInfoA, 12), A(RegisterClassA, u_RegisterClassA, 4), A(DefMDIChildProcA, u_DefMDIChildProcA, 16),
    A(DefDlgProcA, u_DefDlgProcA, 16), A(DrawTextA, u_DrawTextA, 20), A(DrawCaption, u_one, 16), A(FrameRect, u_FrameRect, 12),
    A(DrawEdge, u_DrawEdge, 16), A(DefFrameProcA, u_DefFrameProcA, 20), A(ValidateRgn, u_ValidateRect, 8), A(InvalidateRgn, u_InvalidateRgn, 12),
    A(LoadBitmapA, u_LoadBitmapA, 8), A(RemovePropA, u_RemovePropA, 8), A(SetPropA, u_SetPropA, 12), A(SetWindowWord, u_SetWindowWord, 12),
    A(GetSystemMenu, u_zero, 8), A(AdjustWindowRect, u_AdjustWindowRect, 12), A(AdjustWindowRectEx, u_AdjustWindowRectEx, 16), A(ExitWindowsEx, u_zero, 8),
    A(PostQuitMessage, u_PostQuitMessage, 4), A(CharNextA, u_CharNextA, 4), A(GetClipboardData, u_zero, 4), A(DrawIcon, u_DrawIcon, 16),
    A(EnumClipboardFormats, u_zero, 4), A(EmptyClipboard, u_one, 0), A(OpenClipboard, u_zero, 4), A(ShowCursor, u_ShowCursor, 4),
    A(CloseClipboard, u_one, 0), A(ScrollDC, u_one, 28), A(IsRectEmpty, u_IsRectEmpty, 4), A(GetTopWindow, u_GetTopWindow, 4),
    A(SetSysColors, u_one, 12), A(MessageBoxA, u_MessageBoxA, 16), A(CreateDialogParamA, u_CreateDialog, 20), A(BringWindowToTop, u_BringWindowToTop, 4),
    A(SetRect, u_SetRect, 20), A(FlashWindow, u_one, 8), A(MessageBeep, u_one, 4), A(EndDialog, u_one, 8), A(GetUpdateRgn, u_GetUpdateRgn, 12),
    A(ClientToScreen, u_ClientToScreen, 8), A(SetDlgItemTextA, u_one, 12), A(GetWindowWord, u_GetWindowWord, 8), A(CharUpperA, u_CharUpperA, 4),
    A(GetActiveWindow, u_GetActiveWindow, 0),
};
const int N_USER32 = sizeof API_USER32 / sizeof API_USER32[0];

void user_init(void) {
    t0_ms = (uint32_t)SDL_GetTicks();
    builtin_proc = w32_register("BuiltinWndProc", builtin_wndproc, 16);
    add_class("mdiclient", builtin_proc);
    parse_script();
}
