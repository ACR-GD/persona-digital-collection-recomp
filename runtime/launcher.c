// Launcher: pick the disc image or folder, install the game data (with the English patch), change
// settings, play. Drawn with SDL3's renderer at a logical 800x600 with 2x text (crisp on HiDPI).
#include "launcher.h"
#include "installer.h"
#include <stdint.h>
#include "font_ui.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LW 800
#define LH 600

// ---------------------------------------------------------------- settings model
struct opt { const char *key, *label; int n; const char *val[5]; const char *txt[5]; int cur; };
static struct opt OPTS[] = {
    { "LANG", "Language", 2, { "en", "ja" }, { "English", "Japanese (original)" }, 0 },
    { "SCALE", "Window size", 4, { "1", "2", "3", "4" }, { "640 x 480", "1280 x 960", "1920 x 1440", "2560 x 1920" }, 1 },
    { "FULLSCREEN", "Display", 2, { "0", "1" }, { "Window", "Fullscreen" }, 0 },
    { "SMOOTH", "Scaling", 2, { "0", "1" }, { "Pixel", "Smooth" }, 0 },
    { "NOAUDIO", "Sound", 2, { "0", "1" }, { "On", "Off" }, 0 },
    { "LAUNCHER", "Launcher at startup", 2, { "1", "0" }, { "Show", "Skip" }, 0 },
};
#define NOPT (int)(sizeof OPTS / sizeof OPTS[0])
enum { OPT_LANG = 0 };

static void load_opts(void) {
    for (int k = 0; k < NOPT; k++) {
        char name[64]; snprintf(name, sizeof name, "PERSONA_%s", OPTS[k].key);
        const char *v = getenv(name);
        if (!v) continue;
        for (int i = 0; i < OPTS[k].n; i++) if (!strcmp(v, OPTS[k].val[i])) OPTS[k].cur = i;
    }
}

// ---------------------------------------------------------------- drawing helpers
static SDL_Window *win; static SDL_Renderer *ren;
static float mx, my; static int mclick, rclick;
struct ttex { uint64_t key; SDL_Texture *t; float w, h; int age; };
static struct ttex cache[192];
static uint64_t hash_str(const char *s, const void *f) {
    uint64_t h = 1469598103934665603ull ^ (uint64_t)(uintptr_t)f;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull;
    return h;
}
static int glyph_index(const char **s) {      // ASCII; any other UTF-8 sequence shows as '?'
    int c = (uint8_t)**s;
    if (c >= 0x80) { (*s)++; while (((uint8_t)**s & 0xc0) == 0x80) (*s)++; (*s)--; return '?' - 32; }
    if (c < 32 || c > 126) c = '?';
    return c - 32;
}
static float text_width(const struct uifont *f, const char *s) {
    int w = 0; for (; *s; s++) w += f->g[glyph_index(&s)].adv;
    return w / 2.0f;
}
static struct ttex *text(const struct uifont *f, const char *s) {
    uint64_t key = hash_str(s, f);
    int free_slot = -1, old = 0;
    for (int k = 0; k < 192; k++) {
        if (cache[k].t && cache[k].key == key) { cache[k].age = 0; return &cache[k]; }
        if (!cache[k].t && free_slot < 0) free_slot = k;
        if (cache[k].age > cache[old].age) old = k;
    }
    if (free_slot < 0) { SDL_DestroyTexture(cache[old].t); cache[old].t = 0; free_slot = old; }
    int w = 2, h = f->ascent + f->descent + 2;
    for (const char *p = s; *p; p++) w += f->g[glyph_index(&p)].adv;
    SDL_Surface *sf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    memset(sf->pixels, 0, (size_t)sf->pitch * h);
    int pen = 1;
    for (const char *p = s; *p; p++) {
        const struct uiglyph *g = &f->g[glyph_index(&p)];
        for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++) {
            int px = pen + g->x + x, py = g->y + y;
            if (px < 0 || py < 0 || px >= w || py >= h) continue;
            uint8_t a = f->a[g->off + y * g->w + x];
            if (a) ((uint32_t *)((uint8_t *)sf->pixels + py * sf->pitch))[px] = (uint32_t)a << 24 | 0xffffff;
        }
        pen += g->adv;
    }
    struct ttex *t = &cache[free_slot];
    t->t = SDL_CreateTextureFromSurface(ren, sf); t->w = w / 2.0f; t->h = h / 2.0f; t->key = key; t->age = 0;
    SDL_SetTextureScaleMode(t->t, SDL_SCALEMODE_LINEAR);
    SDL_DestroySurface(sf);
    return t;
}
enum { AL_LEFT, AL_CENTER, AL_RIGHT };
static void draw_text(const struct uifont *f, const char *s, float x, float y, uint32_t rgb, int align, float alpha) {
    if (!*s) return;
    struct ttex *t = text(f, s);
    float dx = align == AL_CENTER ? x - t->w / 2 : align == AL_RIGHT ? x - t->w : x;
    SDL_SetTextureColorMod(t->t, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
    SDL_SetTextureAlphaModFloat(t->t, alpha);
    SDL_FRect d = { dx, y, t->w, t->h };
    SDL_RenderTexture(ren, t->t, 0, &d);
}
static void fill(float x, float y, float w, float h, uint32_t rgb, float a) {
    SDL_SetRenderDrawColor(ren, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (Uint8)(a * 255));
    SDL_FRect r = { x, y, w, h }; SDL_RenderFillRect(ren, &r);
}
static void frame(float x, float y, float w, float h, uint32_t rgb, float a) {
    SDL_SetRenderDrawColor(ren, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (Uint8)(a * 255));
    SDL_FRect r = { x, y, w, h }; SDL_RenderRect(ren, &r);
}
static void gradient(void) {
    SDL_Vertex v[4] = {
        { { 0, 0 }, { 0.05f, 0.07f, 0.16f, 1 }, { 0, 0 } }, { { LW, 0 }, { 0.07f, 0.06f, 0.18f, 1 }, { 0, 0 } },
        { { LW, LH }, { 0.02f, 0.02f, 0.05f, 1 }, { 0, 0 } }, { { 0, LH }, { 0.02f, 0.03f, 0.06f, 1 }, { 0, 0 } },
    };
    int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(ren, 0, v, 4, idx, 6);
}
// a small butterfly (Philemon's), drawn with triangles
static void butterfly(float cx, float cy, float s, float a) {
    SDL_FColor c1 = { 0.55f, 0.72f, 1.0f, a }, c2 = { 0.30f, 0.42f, 0.85f, a * 0.8f };
    SDL_Vertex v[] = {
        { { cx, cy }, c1, { 0, 0 } }, { { cx - 1.1f * s, cy - 0.9f * s }, c2, { 0, 0 } }, { { cx - 0.9f * s, cy + 0.1f * s }, c1, { 0, 0 } },
        { { cx, cy }, c1, { 0, 0 } }, { { cx - 0.8f * s, cy + 0.2f * s }, c1, { 0, 0 } }, { { cx - 0.45f * s, cy + 0.8f * s }, c2, { 0, 0 } },
        { { cx, cy }, c1, { 0, 0 } }, { { cx + 1.1f * s, cy - 0.9f * s }, c2, { 0, 0 } }, { { cx + 0.9f * s, cy + 0.1f * s }, c1, { 0, 0 } },
        { { cx, cy }, c1, { 0, 0 } }, { { cx + 0.8f * s, cy + 0.2f * s }, c1, { 0, 0 } }, { { cx + 0.45f * s, cy + 0.8f * s }, c2, { 0, 0 } },
    };
    SDL_RenderGeometry(ren, 0, v, 12, 0, 0);
}
static int hover(float x, float y, float w, float h) { return mx >= x && mx < x + w && my >= y && my < y + h; }
#define ACCENT 0x8fb4ffu
#define ACCENT_DIM 0x4a5d94u
#define TEXT 0xe4e8f2u
#define MUTED 0x8e96aau
#define PANEL 0x0d1124u
#define FIELD 0x070915u
#define EDGE 0x2c3556u
#define GOOD 0x8fd0a0u
#define BAD 0xe88a7au
// button: returns 1 when clicked
static int button(const char *label, float x, float y, float w, float h, int primary, int enabled) {
    int hv = enabled && hover(x, y, w, h);
    fill(x, y, w, h, primary ? (hv ? 0xa9c6ffu : 0x86a6f0u) : (hv ? 0x26304fu : 0x181e36u), enabled ? 1 : 0.4f);
    frame(x, y, w, h, primary ? 0xc8dbffu : ACCENT_DIM, enabled ? 0.9f : 0.3f);
    draw_text(&UI_BODY, label, x + w / 2, y + (h - (UI_BODY.ascent + UI_BODY.descent) / 2.0f) / 2, primary ? 0x0a0f22u : TEXT, AL_CENTER, enabled ? 1 : 0.45f);
    return hv && mclick;
}
static void elide(const char *path, char *out, size_t n, float maxw) {
    if (!path || !*path) { snprintf(out, n, "(not selected)"); return; }
    if (text_width(&UI_SMALL, path) <= maxw) { snprintf(out, n, "%s", path); return; }
    const char *s = path;
    while (*s && text_width(&UI_SMALL, s) + text_width(&UI_SMALL, "...") > maxw) s++;
    snprintf(out, n, "...%s", s);
}

// ---------------------------------------------------------------- file / folder dialogs
static char picked[1024]; static SDL_AtomicInt picked_ready;
static void SDLCALL on_pick(void *ud, const char *const *files, int filter) {
    (void)ud; (void)filter;
    if (files && files[0]) { snprintf(picked, sizeof picked, "%s", files[0]); SDL_SetAtomicInt(&picked_ready, 1); }
}
static const SDL_DialogFileFilter FILTERS[] = { { "Disc images (iso, bin, cue)", "iso;bin;cue" }, { "All files", "*" } };

// ---------------------------------------------------------------- install thread
static struct install_progress prog; static SDL_AtomicInt installing;
static char src_path[1024]; static const struct launch_paths *P;
static int SDLCALL install_thread(void *arg) {
    (void)arg;
    install_game(src_path, P->data_dir, P->overlay_dir, P->patch, &prog);
    SDL_SetAtomicInt(&installing, 0);
    return 0;
}
static void start_install(char *status) {
    memset(&prog, 0, sizeof prog); SDL_SetAtomicInt(&installing, 1); status[0] = 0;
    SDL_DetachThread(SDL_CreateThread(install_thread, "install", 0));
}

// ---------------------------------------------------------------- config file
static void write_config(const char *cfg) {
    char buf[2048]; int n = snprintf(buf, sizeof buf,
        "# Persona Digital Collection settings (written by the launcher).\n"
        "# Environment variables PERSONA_<NAME> override them.\n\nSOURCE = %s\n", src_path);
    for (int k = 0; k < NOPT && n < (int)sizeof buf; k++)
        n += snprintf(buf + n, sizeof buf - n, "%s = %s\n", OPTS[k].key, OPTS[k].val[OPTS[k].cur]);
    SDL_SaveFile(cfg, buf, strlen(buf));
}
static void apply_env(void) {
    for (int k = 0; k < NOPT; k++) {
        char name[64]; snprintf(name, sizeof name, "PERSONA_%s", OPTS[k].key);
        SDL_setenv_unsafe(name, OPTS[k].val[OPTS[k].cur], 1);
    }
}

// ---------------------------------------------------------------- main loop
int launcher_run(const struct launch_paths *lp) {
    P = lp;
    load_opts();
    snprintf(src_path, sizeof src_path, "%s", SDL_getenv("PERSONA_SOURCE") ? SDL_getenv("PERSONA_SOURCE") : "");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) { fprintf(stderr, "launcher: %s\n", SDL_GetError()); return LAUNCH_PLAY; }
    if (!SDL_CreateWindowAndRenderer("Persona Digital Collection - Recomp by Second Save", LW, LH, SDL_WINDOW_HIGH_PIXEL_DENSITY, &win, &ren)) {
        fprintf(stderr, "launcher: %s\n", SDL_GetError()); return LAUNCH_PLAY;
    }
    SDL_SetRenderLogicalPresentation(ren, LW, LH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderVSync(ren, 1);
    int result = -1, ready = install_is_complete(lp->data_dir), english = install_has_english(lp->overlay_dir);
    int patch_ok = lp->patch && lp->patch[0];
    char status[700] = ""; int status_bad = 0;
    while (result < 0) {
        int busy = SDL_GetAtomicInt(&installing);
        mclick = rclick = 0;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) { if (!busy) result = LAUNCH_QUIT; }
            else if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                SDL_ConvertEventToRenderCoordinates(ren, &e);
                if (e.type == SDL_EVENT_MOUSE_MOTION) { mx = e.motion.x; my = e.motion.y; }
                else { mx = e.button.x; my = e.button.y; }
                if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) { if (e.button.button == SDL_BUTTON_LEFT) mclick = 1; if (e.button.button == SDL_BUTTON_RIGHT) rclick = 1; }
            } else if (e.type == SDL_EVENT_KEY_DOWN && !busy) {
                if (e.key.key == SDLK_ESCAPE) result = LAUNCH_QUIT;
                if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) && ready) result = LAUNCH_PLAY;
            } else if (e.type == SDL_EVENT_DROP_FILE && !busy && e.drop.data) {
                snprintf(src_path, sizeof src_path, "%s", e.drop.data); status[0] = 0;
            }
        }
        if (SDL_GetAtomicInt(&picked_ready)) { SDL_SetAtomicInt(&picked_ready, 0); snprintf(src_path, sizeof src_path, "%s", picked); status[0] = 0; }
        if (prog.done) {
            prog.done = 0; ready = install_is_complete(lp->data_dir); english = install_has_english(lp->overlay_dir);
            if (prog.ok) { snprintf(status, sizeof status, "%s", prog.warn[0] ? prog.warn : "Game installed with the English translation. Ready to play."); status_bad = prog.warn[0] != 0; }
            else { snprintf(status, sizeof status, "Install failed: %s", prog.err); status_bad = 1; }
            write_config(lp->cfg_path);
        }
        if (!english && OPTS[OPT_LANG].cur == 0 && ready) OPTS[OPT_LANG].cur = 1;
        for (int k = 0; k < 192; k++) if (cache[k].t) cache[k].age++;

        gradient();
        // title
        butterfly(LW / 2.0f, 40, 22, 0.9f);
        draw_text(&UI_HEAD, "MEGAMI IBUNROKU", LW / 2.0f, 64, ACCENT, AL_CENTER, 0.9f);
        draw_text(&UI_TITLE, "PERSONA", LW / 2.0f, 74, TEXT, AL_CENTER, 1);
        draw_text(&UI_SUB, "Digital Collection", LW / 2.0f, 124, TEXT, AL_CENTER, 0.85f);
        fill(LW / 2.0f - 120, 148, 240, 1, ACCENT_DIM, 0.8f);
        draw_text(&UI_SMALL, "Recomp by Second Save", LW / 2.0f, 152, ACCENT, AL_CENTER, 1);

        // game data panel
        float px = 40, py = 186, pw = LW - 80;
        fill(px, py, pw, 132, PANEL, 0.88f); frame(px, py, pw, 132, ACCENT_DIM, 0.6f);
        draw_text(&UI_HEAD, "Game disc", px + 16, py + 6, ACCENT, AL_LEFT, 1);
        draw_text(&UI_SMALL, "Disc image (iso, bin/cue) or a folder with the disc contents", px + pw - 16, py + 14, MUTED, AL_RIGHT, 1);
        float ry = py + 44;
        fill(px + 16, ry, pw - 280, 28, FIELD, 1); frame(px + 16, ry, pw - 280, 28, EDGE, 1);
        char el[1100]; elide(src_path, el, sizeof el, pw - 300);
        draw_text(&UI_SMALL, el, px + 24, ry + 5, src_path[0] ? TEXT : MUTED, AL_LEFT, 1);
        if (button("Disc image...", px + pw - 256, ry, 124, 28, 0, !busy))
            SDL_ShowOpenFileDialog(on_pick, 0, win, FILTERS, 2, src_path[0] ? src_path : 0, false);
        if (button("Folder...", px + pw - 124, ry, 108, 28, 0, !busy))
            SDL_ShowOpenFolderDialog(on_pick, 0, win, src_path[0] ? src_path : 0, false);
        float sy = py + 90;
        if (busy) {
            fill(px + 16, sy + 8, pw - 170, 12, FIELD, 1);
            fill(px + 16, sy + 8, (pw - 170) * prog.frac, 12, ACCENT, 1);
            char m[256]; snprintf(m, sizeof m, "%s", prog.msg); elide(m, el, sizeof el, pw - 180);
            draw_text(&UI_SMALL, el, px + 16, sy - 12, MUTED, AL_LEFT, 1);
        } else {
            const char *st = status[0] ? status
                : ready ? (english ? "Game data installed, with the English translation." : "Game data installed (Japanese only).")
                : "Choose your disc image or folder, then Install (about 480 MB).";
            elide(st, el, sizeof el, pw - 180);
            draw_text(&UI_SMALL, el, px + 16, sy + 3, status[0] ? (status_bad ? BAD : GOOD) : ready ? GOOD : MUTED, AL_LEFT, 1);
        }
        if (button(ready ? "Reinstall" : "Install", px + pw - 136, sy, 120, 28, !ready, !busy && src_path[0])) start_install(status);

        // settings panel
        py = 334; float ph = 148;
        fill(px, py, pw, ph, PANEL, 0.88f); frame(px, py, pw, ph, ACCENT_DIM, 0.6f);
        draw_text(&UI_HEAD, "Settings", px + 16, py + 6, ACCENT, AL_LEFT, 1);
        for (int k = 0; k < NOPT; k++) {
            int col = k % 2, row = k / 2;
            float ox = px + 16 + col * (pw / 2), oy = py + 44 + row * 33;
            int disabled = k == OPT_LANG && !english;
            draw_text(&UI_SMALL, OPTS[k].label, ox, oy + 5, MUTED, AL_LEFT, 1);
            float bx = ox + 140, bw = pw / 2 - 172;
            int hv = !disabled && hover(bx, oy, bw, 26);
            fill(bx, oy, bw, 26, hv ? 0x26304fu : 0x141a31u, 1); frame(bx, oy, bw, 26, hv ? ACCENT : EDGE, 1);
            draw_text(&UI_SMALL, OPTS[k].txt[OPTS[k].cur], bx + bw / 2, oy + 5, TEXT, AL_CENTER, disabled ? 0.5f : 1);
            draw_text(&UI_SMALL, "<", bx + 10, oy + 4, ACCENT_DIM, AL_LEFT, 1);
            draw_text(&UI_SMALL, ">", bx + bw - 10, oy + 4, ACCENT_DIM, AL_RIGHT, 1);
            if (hv && (mclick || rclick)) {
                int back = rclick || mx < bx + bw / 3;
                OPTS[k].cur = (OPTS[k].cur + (back ? OPTS[k].n - 1 : 1)) % OPTS[k].n;
            }
        }
        if (!patch_ok) draw_text(&UI_SMALL, "persona_en.patch not found next to the program: install gives the Japanese version.", px + 16, py + ph - 2, BAD, AL_LEFT, 0.9f);

        // footer
        if (button("Quit", px, LH - 62, 120, 38, 0, !busy)) result = LAUNCH_QUIT;
        if (button("Play", LW - px - 180, LH - 62, 180, 38, 1, ready && !busy)) result = LAUNCH_PLAY;
        int ho = hover(px + 140, LH - 52, 170, 22);
        draw_text(&UI_SMALL, "Open save folder", px + 140, LH - 52, ho ? ACCENT : MUTED, AL_LEFT, 1);
        if (ho && mclick) {
            SDL_CreateDirectory(lp->save_dir);
            char url[1200]; snprintf(url, sizeof url, "file://%s%s", lp->save_dir[0] == '/' ? "" : "/", lp->save_dir);
            for (char *c = url; *c; c++) if (*c == '\\') *c = '/';
            SDL_OpenURL(url);
        }
        // PERSONA_LAUNCHER_SHOT=file.bmp (testing): save the window after ~1 s, then act on
        // PERSONA_LAUNCHER_AUTO (play / quit / install: install, save file.bmp.after.bmp, quit)
        static int frames; const char *shot = SDL_getenv("PERSONA_LAUNCHER_SHOT");
        const char *aa = getenv("PERSONA_LAUNCHER_AUTO");
        if (shot && ++frames == 60) {
            SDL_Surface *sf = SDL_RenderReadPixels(ren, 0);
            if (sf) { SDL_SaveBMP(sf, shot); SDL_DestroySurface(sf); }
            if (aa && !strcmp(aa, "play") && ready) result = LAUNCH_PLAY;
            else if (aa && !strcmp(aa, "install") && src_path[0]) start_install(status);
            else if (!aa || strcmp(aa, "install")) result = LAUNCH_QUIT;
        }
        if (shot && aa && !strcmp(aa, "install") && frames > 61 && !busy && !SDL_GetAtomicInt(&installing) && !prog.done) {
            if (frames > 63) {
                SDL_Surface *sf = SDL_RenderReadPixels(ren, 0); char p2[1100]; snprintf(p2, sizeof p2, "%s.after.bmp", shot);
                if (sf) { SDL_SaveBMP(sf, p2); SDL_DestroySurface(sf); }
                result = LAUNCH_QUIT;
            }
        }
        SDL_RenderPresent(ren);
    }
    write_config(lp->cfg_path);
    if (result == LAUNCH_PLAY) apply_env();
    for (int k = 0; k < 192; k++) if (cache[k].t) { SDL_DestroyTexture(cache[k].t); cache[k].t = 0; }
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); ren = 0; win = 0;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return result;
}
