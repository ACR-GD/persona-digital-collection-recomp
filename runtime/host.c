// Host side: SDL window, presentation of the 8-bit screen through the hardware palette with a
// software cursor, headless screenshots, and the program entry point.
#include "gdi.h"
#include "../gen/image_info.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <sys/stat.h>

int video_headless;
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static uint32_t rgba[SCREEN_W * SCREEN_H];
static uint32_t t0;
static const char *shot_path; static int shot_ms = 1000; static uint32_t next_shot;
extern uint32_t gdi_screen_bits(void);

void video_init(void) {
    video_headless = getenv("PERSONA_HEADLESS") && atoi(getenv("PERSONA_HEADLESS"));
    shot_path = getenv("PERSONA_SHOT");
    if (getenv("PERSONA_SHOT_MS")) shot_ms = atoi(getenv("PERSONA_SHOT_MS"));
    t0 = (uint32_t)SDL_GetTicks();
    if (video_headless) return;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) rt_fatal("SDL video init: %s", SDL_GetError());
    int scale = getenv("PERSONA_SCALE") ? atoi(getenv("PERSONA_SCALE")) : 2;
    win = SDL_CreateWindow("Megami Ibunroku Persona Digital Collection", SCREEN_W * scale, SCREEN_H * scale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) rt_fatal("SDL window: %s", SDL_GetError());
    ren = SDL_CreateRenderer(win, 0);
    SDL_SetRenderVSync(ren, 1);
    SDL_SetRenderLogicalPresentation(ren, SCREEN_W, SCREEN_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    SDL_SetTextureScaleMode(tex, getenv("PERSONA_SMOOTH") ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    SDL_HideCursor();
    if (getenv("PERSONA_FULLSCREEN") && atoi(getenv("PERSONA_FULLSCREEN"))) SDL_SetWindowFullscreen(win, true);
}
void video_toggle_fullscreen(void) { if (win) SDL_SetWindowFullscreen(win, !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)); }
void video_window_to_screen(float wx, float wy, int *x, int *y) {
    float rx = wx, ry = wy;
    if (ren) SDL_RenderCoordinatesFromWindow(ren, wx, wy, &rx, &ry);
    int ix = (int)rx, iy = (int)ry;
    if (ix < 0) ix = 0; if (iy < 0) iy = 0; if (ix >= SCREEN_W) ix = SCREEN_W - 1; if (iy >= SCREEN_H) iy = SCREEN_H - 1;
    *x = ix; *y = iy;
}
void video_shutdown(void) {
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    tex = 0; ren = 0; win = 0;
}
// standard arrow for IDC_ARROW and other stock cursors
static const char *ARROW[] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ", "X.....X     ", "X......X    ",
    "X.......X   ", "X........X  ", "X.....XXXXX ", "X..X..X     ", "X.X X..X    ", "XX  X..X    ", "X    X..X   ", "     X..X   ", "      XX    ", 0 };
static void draw_cursor(void) {
    if (!user_cursor_shown()) return;
    uint32_t h = user_current_cursor();
    struct cursor *c = user_cursor_get(h);
    if (!c) {
        for (int y = 0; ARROW[y]; y++) for (int x = 0; ARROW[y][x]; x++) {
            int px = mouse_x + x, py = mouse_y + y; if (px >= SCREEN_W || py >= SCREEN_H) continue;
            if (ARROW[y][x] == 'X') rgba[py * SCREEN_W + px] = 0xff000000u; else if (ARROW[y][x] == '.') rgba[py * SCREEN_W + px] = 0xffffffffu;
        }
        return;
    }
    for (int y = 0; y < c->h; y++) for (int x = 0; x < c->w; x++) {
        int px = mouse_x - c->hx + x, py = mouse_y - c->hy + y;
        if (px < 0 || py < 0 || px >= SCREEN_W || py >= SCREEN_H) continue;
        uint32_t *p = &rgba[py * SCREEN_W + px];
        int a = c->and_[y * 32 + x], xo = c->xor_[y * 32 + x];
        uint32_t v = (a ? *p : 0) ^ (xo ? 0xffffffu : 0);
        *p = 0xff000000u | v;
    }
}
static void write_ppm(const char *path) {
    char tmp[1100]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    static uint8_t rgb[SCREEN_W * SCREEN_H * 3];
    for (int k = 0; k < SCREEN_W * SCREEN_H; k++) { uint32_t p = rgba[k]; rgb[3 * k] = (uint8_t)(p >> 16); rgb[3 * k + 1] = (uint8_t)(p >> 8); rgb[3 * k + 2] = (uint8_t)p; }
    fwrite(rgb, 3, SCREEN_W * SCREEN_H, f);
    fclose(f);
    rename(tmp, path);
}
void video_present_screen(void) {
    const uint8_t *px = GP(gdi_screen_bits());
    for (int k = 0; k < SCREEN_W * SCREEN_H; k++) rgba[k] = 0xff000000u | sys_pal[px[k]];
    extern void qt_overlay(uint32_t *, int, int); qt_overlay(rgba, SCREEN_W, SCREEN_H);
    draw_cursor();
    screen_dirty = 0;
    uint32_t now = (uint32_t)SDL_GetTicks() - t0;
    if (shot_path && now >= next_shot) {
        char p[1100]; snprintf(p, sizeof p, shot_path, (int)(now / 100));   // %d = tenths of a second
        write_ppm(p);
        next_shot = now + (uint32_t)shot_ms;
    }
    if (!ren) return;
    SDL_UpdateTexture(tex, 0, rgba, SCREEN_W * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, tex, 0, 0);
    SDL_RenderPresent(ren);
}
// at most ~60 presents per second; screenshots keep their own cadence
void video_maybe_present(void) {
    static uint64_t last;
    static const char *dbg; static int dbg_init; static uint32_t dbg_t;
    if (!dbg_init) { dbg_init = 1; dbg = getenv("PERSONA_DEBUG_MIX"); }
#ifdef RT_DEBUG
    { extern void rt_prof_dump(void), rt_prof_reset(void); static int st; uint32_t now = (uint32_t)SDL_GetTicks();
      const char *pt = getenv("PERSONA_PROF_AT");
      if (pt) { uint32_t at = (uint32_t)(atof(pt) * 1000); uint32_t len = getenv("PERSONA_PROF_LEN") ? (uint32_t)atoi(getenv("PERSONA_PROF_LEN")) : 2000;
        extern void rt_prof_dump_all(const char *);
        if (st == 0 && now >= at) { rt_prof_reset(); st = 1; } else if (st == 1 && now >= at + len) { if (getenv("PERSONA_PROF_ALL")) rt_prof_dump_all("profall"); else rt_prof_dump(); st = 2; } } }
#endif
    { static uint32_t dt; const char *dd = getenv("PERSONA_DUMP");
      if (dd && (uint32_t)SDL_GetTicks() - dt >= 1000) { dt = (uint32_t)SDL_GetTicks(); uint32_t a = (uint32_t)strtoul(dd, 0, 16); const char *c = strchr(dd, ':'); int n = c ? atoi(c + 1) : 16;
        fprintf(stderr, "[%u] dump %08x:", dt, a); for (int k = 0; k < n; k++) fprintf(stderr, " %08x", RD32(a + 4u * k)); fprintf(stderr, "\n"); } }
    if (dbg && (uint32_t)SDL_GetTicks() - dbg_t >= 1000) {
        dbg_t = (uint32_t)SDL_GetTicks();
        uint32_t o = (uint32_t)strtoul(dbg, 0, 16); int n = RD16(o + 0x2c);
        fprintf(stderr, "[%u] mix ch=%d busy=%x f1506=%x f14f6=%x:", dbg_t, n, RD32(o + 0x14fa), RD16(o + 0x1506), RD32(o + 0x14f6));
        for (int k = 0; k < n && k < 8; k++) { uint32_t c = o + 0x30 + 116u * k; fprintf(stderr, " [%d:%x %x %x %x]", k, RD32(c), RD32(c + 4), RD32(c + 8), RD32(c + 12)); }
        fprintf(stderr, "\n");
    }
    uint64_t now = SDL_GetTicksNS();
    int shot_due = shot_path && (uint32_t)SDL_GetTicks() - t0 >= next_shot;
    extern int qt_playing(void);
    if (!screen_dirty && !shot_due && !qt_playing()) return;
    if (now - last < 16000000ull && !shot_due) return;
    last = now;
    video_present_screen();
}

// ---------------------------------------------------------------- entry
static void load_image(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) rt_fatal("cannot open %s (run `make gen` first)", path);
    uint32_t hdr[2];
    while (fread(hdr, 4, 2, f) == 2)
        if (fread(MEM + hdr[0], 1, hdr[1], f) != hdr[1]) rt_fatal("short image");
    fclose(f);
}
static int is_dir(const char *p) { struct stat st; return !stat(p, &st) && S_ISDIR(st.st_mode); }
void user_init(void);
void audio_poll(void) {}

int main(int argc, char **argv) {
    rt_verbose = getenv("PERSONA_VERBOSE") != 0;
    w32_log_api = getenv("PERSONA_TRACE") ? atoi(getenv("PERSONA_TRACE")) : 0;
    const char *data = getenv("PERSONA_DATA");
    char buf[1024];
    if (!data) {
        if (is_dir("data")) data = "data";
        else { const char *base = SDL_GetBasePath(); snprintf(buf, sizeof buf, "%sdata", base ? base : ""); data = buf; }
    }
    snprintf(game_dir, sizeof game_dir, "%s", data);
    snprintf(save_dir, sizeof save_dir, "%s", getenv("PERSONA_SAVE") ? getenv("PERSONA_SAVE") : "save");
    // translation overlay: PERSONA_OVERLAY, else data_en/ next to data/ (PERSONA_OVERLAY= disables)
    const char *ov = getenv("PERSONA_OVERLAY");
    if (ov) snprintf(overlay_dir, sizeof overlay_dir, "%s", ov);
    else { snprintf(overlay_dir, sizeof overlay_dir, "%s_en", game_dir); if (!is_dir(overlay_dir)) overlay_dir[0] = 0; }
    if (overlay_dir[0]) fprintf(stderr, "translation overlay: %s\n", overlay_dir);
    if (!is_dir(game_dir)) rt_fatal("game data folder '%s' not found (see README)", game_dir);
    MEM = calloc(1, MEM_SIZE + 16);
    char img[1100]; snprintf(img, sizeof img, "%s/persona.img", game_dir);
    FILE *probe = fopen(img, "rb"); if (probe) fclose(probe);
    if (getenv("PERSONA_IMAGE") || probe) load_image(getenv("PERSONA_IMAGE") ? getenv("PERSONA_IMAGE") : img);
    else {   // the image compiled into the binary
        extern const uint32_t IMAGE_BLOB_SIZE; extern const uint8_t IMAGE_BLOB[];
        uint32_t base, size; memcpy(&base, IMAGE_BLOB, 4); memcpy(&size, IMAGE_BLOB + 4, 4);
        if (size + 8 > IMAGE_BLOB_SIZE) rt_fatal("bad embedded image");
        memcpy(MEM + base, IMAGE_BLOB + 8, size);
    }
    SDL_SetAppMetadata("Persona Digital Collection", "1.0", "persona.recomp");
    SDL_Init(SDL_INIT_EVENTS);
    w32_init(argc, argv);
    gdi_init();
    user_init();
    video_init();
    audio_init();
    ESP = STACK_HI - 16;
    PUSH32(0);
    uint32_t args[1] = { 0 };
    guest_call(GUEST_ENTRY, 0, args);
    fprintf(stderr, "guest returned from entry\n");
    w32_shutdown();
    return 0;
}
void w32_shutdown(void) {
    audio_shutdown();
    video_shutdown();
    SDL_Quit();
}
