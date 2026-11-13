// QuickTime for Windows 2.x emulation (QTIM32.DLL "_EntryPoint" and CMGR32.DLL "_EntryPoint").
//
// Director reaches QuickTime through thunks that put a selector in BX (and AX=1 for CMGR32) and call
// the entry point; the caller's arguments follow on the stack (cdecl, caller pops). The selector
// numbers are QTW's own and are not exported by name; the meanings below were identified from
// Director's call sites (see NOTES.md, "QuickTime").
//
// Movies are read directly from the .mov files (moov/trak/stbl atoms). The disc only uses Cinepak
// video and raw 8-bit PCM audio. A movie controller plays its movie into the Director child window it
// was created for: frames are decoded to XRGB and overlaid on the presented screen at that window's
// position (true colour, unaffected by the 8-bit palette), audio goes to an SDL stream which is
// also the playback clock.
#include "win32.h"
#include <SDL3/SDL.h>

#define QT_LOG(...) do { if (qt_log) fprintf(stderr, __VA_ARGS__); } while (0)
static int qt_log = -1;

// ---------------------------------------------------------------- .mov parsing
struct sample { uint32_t off, size; int64_t time, dur; };
struct track {
    int video, audio;
    uint32_t timescale; int64_t duration;           // media time scale / duration
    int w, h;                                       // video
    int rate, channels, bits;                       // audio
    struct sample *s; int ns;
};
struct movie {
    int used; uint32_t handle;
    FILE *f; char path[1024];
    uint32_t timescale; int64_t duration;           // movie time scale / duration
    struct track tr[4]; int ntr;
    int vt, at;                                      // video / audio track index or -1
    // playback state
    int64_t time;                                   // current movie time (movie time scale)
    int32_t rate;                                   // Fixed 16.16
    int active;
    double play_t0; int64_t time0;                  // wall clock reference while playing
    int done;
    // video output
    int cur_frame;
    uint32_t *rgb; int w, h;
    struct cvid *dec; int dec_frame;
    // controller
    uint32_t mc, hwnd; int x, y, cw, ch; int visible;
    SDL_AudioStream *as; int64_t audio_fed;        // samples queued
    struct sub { float t0, t1; int x, y, w, h; uint8_t *rgba; } *subs; int nsubs;   // English subtitles (data_en)
};
#define NMOV 32
static struct movie MV[NMOV];
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t be16(const uint8_t *p) { return (uint32_t)(p[0] << 8 | p[1]); }

static uint8_t *read_at(FILE *f, long off, uint32_t n) {
    uint8_t *b = malloc(n ? n : 1);
    fseek(f, off, SEEK_SET);
    if (fread(b, 1, n, f) != n) { free(b); return 0; }
    return b;
}
// stbl: build the sample list from stts (durations), stsc (chunk -> samples), stsz (sizes), stco (offsets)
static void parse_stbl(struct track *t, const uint8_t *d, uint32_t n) {
    const uint8_t *stts = 0, *stsc = 0, *stsz = 0, *stco = 0;
    for (uint32_t p = 0; p + 8 <= n;) {
        uint32_t sz = be32(d + p); if (sz < 8 || p + sz > n) break;
        const uint8_t *a = d + p;
        if (!memcmp(a + 4, "stts", 4)) stts = a; else if (!memcmp(a + 4, "stsc", 4)) stsc = a;
        else if (!memcmp(a + 4, "stsz", 4)) stsz = a; else if (!memcmp(a + 4, "stco", 4)) stco = a;
        else if (!memcmp(a + 4, "stsd", 4) && be32(a + 12) >= 1) {
            const uint8_t *e = a + 16;
            if (t->video) { t->w = (int)be16(e + 32); t->h = (int)be16(e + 34); }
            if (t->audio) { t->channels = (int)be16(e + 24); t->bits = (int)be16(e + 26); t->rate = (int)(be32(e + 32) >> 16); }
        }
        p += sz;
    }
    if (!stts || !stsc || !stsz || !stco) return;
    uint32_t count = be32(stsz + 16), fixed = be32(stsz + 12);
    if (t->audio && fixed == 1) {
        // raw PCM: one "sample" per audio sample; group per chunk instead
    }
    uint32_t nchunks = be32(stco + 12), nstsc = be32(stsc + 12);
    // per chunk sample counts
    uint32_t *per = calloc(nchunks + 1, 4);
    for (uint32_t k = 0; k < nstsc; k++) {
        uint32_t first = be32(stsc + 16 + 12 * k), spc = be32(stsc + 20 + 12 * k);
        uint32_t last = k + 1 < nstsc ? be32(stsc + 16 + 12 * (k + 1)) : nchunks + 1;
        for (uint32_t c = first; c < last && c <= nchunks; c++) per[c - 1] = spc;
    }
    if (t->audio && fixed) {
        // audio: keep one entry per chunk (offset, byte size, start time in media units)
        t->s = calloc(nchunks, sizeof *t->s); t->ns = (int)nchunks;
        int64_t time = 0; int bps = (t->bits / 8) * t->channels; if (bps < 1) bps = 1;
        for (uint32_t c = 0; c < nchunks; c++) {
            t->s[c].off = be32(stco + 16 + 4 * c); t->s[c].size = per[c] * (uint32_t)bps;
            t->s[c].time = time; t->s[c].dur = per[c]; time += per[c];
        }
        free(per); return;
    }
    t->s = calloc(count + 1, sizeof *t->s); t->ns = (int)count;
    uint32_t i = 0;
    for (uint32_t c = 0; c < nchunks && i < count; c++) {
        uint32_t off = be32(stco + 16 + 4 * c);
        for (uint32_t k = 0; k < per[c] && i < count; k++, i++) {
            t->s[i].off = off; t->s[i].size = fixed ? fixed : be32(stsz + 20 + 4 * i); off += t->s[i].size;
        }
    }
    int64_t time = 0; i = 0;
    for (uint32_t k = 0, ne = be32(stts + 12); k < ne; k++) {
        uint32_t cnt = be32(stts + 16 + 8 * k), dur = be32(stts + 20 + 8 * k);
        for (uint32_t j = 0; j < cnt && i < count; j++, i++) { t->s[i].time = time; t->s[i].dur = dur; time += dur; }
    }
    free(per);
}
static void parse_atoms(struct movie *m, const uint8_t *d, uint32_t n, struct track *t) {
    for (uint32_t p = 0; p + 8 <= n;) {
        uint32_t sz = be32(d + p); if (sz < 8 || p + sz > n) break;
        const uint8_t *a = d + p; const char *ty = (const char *)a + 4;
        if (!memcmp(ty, "mvhd", 4)) { m->timescale = be32(a + 20); m->duration = be32(a + 24); }
        else if (!memcmp(ty, "trak", 4) && m->ntr < 4) { struct track *nt = &m->tr[m->ntr++]; memset(nt, 0, sizeof *nt); parse_atoms(m, a + 8, sz - 8, nt); }
        else if (!memcmp(ty, "mdia", 4) || !memcmp(ty, "minf", 4)) parse_atoms(m, a + 8, sz - 8, t);
        else if (t && !memcmp(ty, "mdhd", 4)) { t->timescale = be32(a + 20); t->duration = be32(a + 24); }
        else if (t && !memcmp(ty, "hdlr", 4)) { if (!memcmp(a + 16, "vide", 4)) t->video = 1; if (!memcmp(a + 16, "soun", 4)) t->audio = 1; }
        else if (t && !memcmp(ty, "stbl", 4)) parse_stbl(t, a + 8, sz - 8);
        p += sz;
    }
}
static int movie_load(struct movie *m, const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long len = ftell(f);
    for (long p = 0; p + 8 <= len;) {
        uint8_t h[8]; fseek(f, p, SEEK_SET); if (fread(h, 1, 8, f) != 8) break;
        uint32_t sz = be32(h); if (sz < 8) break;
        if (!memcmp(h + 4, "moov", 4)) { uint8_t *b = read_at(f, p + 8, sz - 8); if (b) { parse_atoms(m, b, sz - 8, 0); free(b); } }
        p += sz;
    }
    m->f = f; snprintf(m->path, sizeof m->path, "%s", path);
    m->vt = m->at = -1;
    for (int k = 0; k < m->ntr; k++) { if (m->tr[k].video && m->vt < 0) m->vt = k; if (m->tr[k].audio && m->at < 0) m->at = k; }
    if (m->vt >= 0) { m->w = m->tr[m->vt].w; m->h = m->tr[m->vt].h; m->rgb = calloc((size_t)(m->w * m->h), 4); }
    m->cur_frame = -1; m->dec_frame = -1; m->rate = 0; m->active = 1;
    QT_LOG("[qt] movie %s: scale %u duration %lld, video %dx%d (%d frames), audio %d Hz x%d %d-bit\n", path, m->timescale, (long long)m->duration,
           m->w, m->h, m->vt >= 0 ? m->tr[m->vt].ns : 0, m->at >= 0 ? m->tr[m->at].rate : 0, m->at >= 0 ? m->tr[m->at].channels : 0, m->at >= 0 ? m->tr[m->at].bits : 0);
    return 1;
}
static struct movie *MOV(uint32_t h) { for (int k = 0; k < NMOV; k++) if (MV[k].used && MV[k].handle == h) return &MV[k]; return 0; }
static struct movie *MOVMC(uint32_t mc) { for (int k = 0; k < NMOV; k++) if (MV[k].used && MV[k].mc == mc) return &MV[k]; return 0; }

// ---------------------------------------------------------------- Cinepak (from the Icarus recompilation)
struct cvid_cb { uint8_t y[4]; int8_t u, v; };
struct cvid_strip { struct cvid_cb v1[256], v4[256]; };
struct cvid {
    int used, w, h;
    struct cvid_strip strips[32];
    uint32_t *rgb;             // persistent XRGB frame (inter frames update it)
};
static inline uint32_t be24(const uint8_t *p) { return (uint32_t)(p[0] << 16 | p[1] << 8 | p[2]); }
static void cvid_codebook(struct cvid_cb *cb, int id, const uint8_t *d, const uint8_t *end) {
    int entsz = (id & 4) ? 4 : 6;
    int selective = id & 1;
    uint32_t flag = 0, mask = 0;
    for (int i = 0; i < 256; i++) {
        if (selective) {
            if (!(mask >>= 1)) { if (d + 4 > end) return; flag = be32(d); d += 4; mask = 0x80000000u; }
            if (!(flag & mask)) continue;
        }
        if (d + entsz > end) return;
        for (int k = 0; k < 4; k++) cb[i].y[k] = d[k];
        if (entsz == 6) { cb[i].u = (int8_t)d[4]; cb[i].v = (int8_t)d[5]; } else { cb[i].u = 0; cb[i].v = 0; }
        d += entsz;
    }
}
static inline uint32_t yuv(int y, int u, int v) {
    int r = y + 2 * v, g = y - u / 2 - v, b = y + 2 * u;
    r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
    return (uint32_t)(r << 16 | g << 8 | b);
}
static void cvid_vectors(struct cvid *c, struct cvid_strip *s, int id, const uint8_t *d, const uint8_t *end, int y0, int y1) {
    uint32_t flag = 0, mask = 0;
    for (int y = y0; y < y1; y += 4) for (int x = 0; x < c->w; x += 4) {
        if ((id & 1) && !(mask >>= 1)) { if (d + 4 > end) return; flag = be32(d); d += 4; mask = 0x80000000u; }
        if ((id & 1) && !(flag & mask)) continue;                      // skipped block (inter)
        if (!(id & 2) && !(mask >>= 1)) { if (d + 4 > end) return; flag = be32(d); d += 4; mask = 0x80000000u; }
        uint32_t px[16];
        if ((id & 2) || !(flag & mask)) {                               // V1: one entry, 2x2 upscaled
            if (d >= end) return;
            struct cvid_cb *e = &s->v1[*d++];
            for (int k = 0; k < 4; k++) {
                uint32_t p = yuv(e->y[k], e->u, e->v);
                int bx = (k & 1) * 2, by = (k >> 1) * 2;
                px[by * 4 + bx] = px[by * 4 + bx + 1] = px[(by + 1) * 4 + bx] = px[(by + 1) * 4 + bx + 1] = p;
            }
        } else {                                                        // V4: four entries, one per quadrant
            if (d + 4 > end) return;
            for (int q = 0; q < 4; q++) {
                struct cvid_cb *e = &s->v4[*d++];
                int bx = (q & 1) * 2, by = (q >> 1) * 2;
                px[by * 4 + bx] = yuv(e->y[0], e->u, e->v); px[by * 4 + bx + 1] = yuv(e->y[1], e->u, e->v);
                px[(by + 1) * 4 + bx] = yuv(e->y[2], e->u, e->v); px[(by + 1) * 4 + bx + 1] = yuv(e->y[3], e->u, e->v);
            }
        }
        for (int r = 0; r < 4; r++) {
            if (y + r >= c->h) break;
            for (int k = 0; k < 4 && x + k < c->w; k++) c->rgb[(y + r) * c->w + x + k] = px[r * 4 + k];
        }
    }
}
static int cvid_decode(struct cvid *c, const uint8_t *d, uint32_t size) {
    if (size < 10) return 0;
    const uint8_t *end = d + size;
    int flags = d[0];
    int nstrips = (int)be16(d + 8);
    d += 10;
    int y0 = 0;
    for (int i = 0; i < nstrips && i < 32 && d + 12 <= end; i++) {
        uint32_t ssize = be24(d + 1);
        const uint8_t *se = d + ssize; if (se > end || ssize < 12) se = end;
        int top = (int)be16(d + 4), bot = (int)be16(d + 8);
        int h = (top && bot > top) ? bot - top : bot;
        if (i > 0 && !(flags & 1)) c->strips[i] = c->strips[i - 1];
        const uint8_t *p = d + 12;
        while (p + 4 <= se) {
            int cid = p[0]; uint32_t csz = be24(p + 1);
            if (csz < 4) break;
            const uint8_t *ce = p + csz; if (ce > se) ce = se;
            switch (cid) {
            case 0x20: case 0x21: case 0x24: case 0x25: cvid_codebook(c->strips[i].v4, cid, p + 4, ce); break;
            case 0x22: case 0x23: case 0x26: case 0x27: cvid_codebook(c->strips[i].v1, cid, p + 4, ce); break;
            case 0x30: case 0x31: case 0x32: cvid_vectors(c, &c->strips[i], cid, p + 4, ce, y0, y0 + h > c->h ? c->h : y0 + h); break;
            default: break;
            }
            p += csz;
        }
        y0 += h;
        d = se;
    }
    return 1;
}

// ---------------------------------------------------------------- MSVFW32

// ---------------------------------------------------------------- open files
static char openfile[16][1024], openguest[16][512];
static uint32_t last_err;

// subtitles for burned-in Japanese (tools/make_subs.py): <movie>.sub next to the .mov in the overlay
static void load_subs(struct movie *m, const char *guest) {
    char g[512], hp[1024]; snprintf(g, sizeof g, "%s", guest);
    char *dot = strrchr(g, '.'); if (!dot) return; strcpy(dot, ".sub");
    if (!host_path(g, hp, sizeof hp, 0)) return;
    FILE *f = fopen(hp, "rb"); if (!f) return;
    char tag[4]; uint32_t n = 0;
    if (fread(tag, 1, 4, f) == 4 && !memcmp(tag, "PSUB", 4) && fread(&n, 4, 1, f) == 1 && n < 64) {
        m->subs = calloc(n, sizeof *m->subs);
        for (uint32_t k = 0; k < n; k++) {
            float tt[2]; uint16_t r[4];
            if (fread(tt, 4, 2, f) != 2 || fread(r, 2, 4, f) != 4) break;
            struct sub *s = &m->subs[k]; s->t0 = tt[0]; s->t1 = tt[1]; s->x = r[0]; s->y = r[1]; s->w = r[2]; s->h = r[3];
            s->rgba = malloc((size_t)s->w * s->h * 4);
            if (fread(s->rgba, 4, (size_t)s->w * s->h, f) != (size_t)s->w * s->h) { free(s->rgba); break; }
            m->nsubs++;
        }
    }
    fclose(f);
    QT_LOG("[qt] %d subtitles from %s\n", m->nsubs, hp);
}

// ---------------------------------------------------------------- the entry points
static uint32_t A_(int k) { return RD32(ESP + 12 + 4u * (uint32_t)k); }   // [ret into thunk][saved ebx][caller ret][args]
static void wr_time(uint32_t rec, int64_t v, uint32_t scale) { if (!rec) return; WR32(rec, (uint32_t)v); WR32(rec + 4, (uint32_t)(v >> 32)); WR32(rec + 8, scale); WR32(rec + 12, 0); }
static int64_t movie_now(struct movie *m);

static uint32_t qt_entry(void) {
    int sel = EBX & 0xffff;
    struct movie *m = 0;
    uint32_t r = 0;
    switch (sel) {
    case 57: case 58: case 11: case 1: r = 0; break;                       // init / exit / EnterMovies / ClearMoviesStickyError
    case 94: r = last_err; break;                                         // GetMoviesError
    case 44: {                                                             // OpenMovieFile(spec, short *refNum, perm)
        char g[512]; gstr_get(A_(0), g, sizeof g);
        char hp[1024]; int k;
        for (k = 1; k < 16 && openfile[k][0]; k++);
        if (k == 16 || !host_path(g, hp, sizeof hp, 0)) { QT_LOG("[qt] OpenMovieFile(%s) failed\n", g); last_err = (uint32_t)-43; r = (uint32_t)-43; break; }
        snprintf(openfile[k], sizeof openfile[k], "%s", hp); snprintf(openguest[k], sizeof openguest[k], "%s", g);
        WR16(A_(1), (uint32_t)k); r = 0; last_err = 0;
        QT_LOG("[qt] OpenMovieFile(%s) -> ref %d\n", g, k);
        break;
    }
    case 2: { uint32_t k = A_(0) & 0xffff; if (k < 16) openfile[k][0] = 0; r = 0; break; }   // CloseMovieFile
    case 42: {                                                             // NewMovieFromFile(Movie*, ref, ...)
        uint32_t k = A_(1) & 0xffff; int i;
        for (i = 0; i < NMOV && MV[i].used; i++);
        if (k >= 16 || !openfile[k][0] || i == NMOV) { last_err = (uint32_t)-2048; r = last_err; break; }
        memset(&MV[i], 0, sizeof MV[i]);
        if (!movie_load(&MV[i], openfile[k])) { last_err = (uint32_t)-2048; r = last_err; break; }
        MV[i].used = 1; MV[i].handle = gmem_alloc(64);
        load_subs(&MV[i], openguest[k]);
        WR32(A_(0), MV[i].handle); r = 0; last_err = 0;
        break;
    }
    case 7: if ((m = MOV(A_(0)))) {                                         // DisposeMovie
            if (m->f) fclose(m->f); free(m->rgb); for (int k = 0; k < m->ntr; k++) free(m->tr[k].s);
            if (m->as) SDL_DestroyAudioStream(m->as);
            for (int k = 0; k < m->nsubs; k++) free(m->subs[k].rgba); free(m->subs);
            gmem_free(m->handle); m->used = 0;
        } break;
    case 15: if ((m = MOV(A_(0)))) {                                        // GetMovieBox(movie, RECT*)
            WR32(A_(1), 0); WR32(A_(1) + 4, 0); WR32(A_(1) + 8, (uint32_t)m->w); WR32(A_(1) + 12, (uint32_t)m->h);
        } break;
    case 28: if ((m = MOV(A_(0)))) r = m->timescale; else r = 600; break;   // GetMovieTimeScale
    case 18: if ((m = MOV(A_(0)))) r = (uint32_t)m->duration; break;        // GetMovieDuration
    case 54: if ((m = MOV(A_(0)))) { int64_t t = movie_now(m); wr_time(A_(1), t, m->timescale); r = (uint32_t)t; } break;   // GetMovieTime
    case 13: if ((m = MOV(A_(0)))) r = movie_now(m) >= m->duration; break;  // IsMovieDone
    case 49: if ((m = MOV(A_(0)))) m->active = A_(1) & 0xff; break;        // SetMovieActive(movie, bool)
    case 23: r = 0x100; break;                                             // GetMovieVolume (full)
    case 56: if ((m = MOV(A_(0)))) {                                        // NewMovieController(movie, RECT*, flags, hwnd)
            uint32_t rc = A_(1);
            m->mc = gmem_alloc(64); m->hwnd = A_(3);
            m->x = (int32_t)RD32(rc); m->y = (int32_t)RD32(rc + 4); m->cw = (int32_t)RD32(rc + 8) - m->x; m->ch = (int32_t)RD32(rc + 12) - m->y;
            r = m->mc;
            QT_LOG("[qt] NewMovieController rect (%d,%d %dx%d) flags %x hwnd %x\n", m->x, m->y, m->cw, m->ch, A_(2), A_(3));
        } break;
    case 4: {                                                              // ConvertTimeScale(TimeRecord*, newScale)
        uint32_t rec = A_(0), ns = A_(1);
        int64_t v = (int64_t)((uint64_t)RD32(rec) | (uint64_t)RD32(rec + 4) << 32); uint32_t os = RD32(rec + 8);
        if (os && ns) { v = v * (int64_t)ns / (int64_t)os; wr_time(rec, v, ns); }
        break;
    }
    default:
        QT_LOG("[qt] QT#%d(%x, %x, %x, %x, %x) from %08x [unimplemented]\n", sel, A_(0), A_(1), A_(2), A_(3), A_(4), RD32(ESP + 8));
        return 0;
    }
    QT_LOG("[qt] QT#%d(%x, %x, %x, %x) -> %x\n", sel, A_(0), A_(1), A_(2), A_(3), r);
    return r;
}
static void movie_play(struct movie *m, int32_t rate);
static void movie_seek(struct movie *m, int64_t t);
static uint32_t cm_entry(void) {
    int sel = EBX & 0xffff;
    struct movie *m = MOVMC(A_(0));
    if (sel == 1 && m) {                                                   // MCDoAction(mc, action, params)
        uint32_t act = A_(1), prm = A_(2);
        QT_LOG("[qt] MCDoAction(%x, %u, %x)\n", A_(0), act, prm);
        switch (act) {
        case 8: movie_play(m, prm == 0x100 ? 0x10000 : (int32_t)prm); break;   // mcActionPlay(Fixed rate); the first play passes 0x100 for normal speed
        case 12: if (prm) {                                               // mcActionGoToTime (TimeRecord*)
                int64_t v = (int64_t)((uint64_t)RD32(prm) | (uint64_t)RD32(prm + 4) << 32); uint32_t sc = RD32(prm + 8);
                movie_seek(m, sc && sc != m->timescale ? v * m->timescale / sc : v);
            } break;
        case 15: if (prm) WR16(prm, 0x100); break;                        // mcActionGetVolume
        case 37: case 42: {                                                // mcActionGetPlayRate (42 in QTW): 0 once an end is reached
            int64_t t = movie_now(m);
            if (prm) WR32(prm, (uint32_t)((m->rate > 0 && t >= m->duration) || (m->rate < 0 && t <= 0) ? 0 : m->rate));
            break;
        }
        default: break;
        }
        return 0;
    }
    if (sel == 0 && m) { m->visible = A_(2) & 0xff; QT_LOG("[qt] MCActivate(%x, %x, %d)\n", A_(0), A_(1), m->visible); return 0; }
    QT_LOG("[qt] CM#%d(%x, %x, %x, %x, %x) from %08x [unimplemented]\n", sel, A_(0), A_(1), A_(2), A_(3), A_(4), RD32(ESP + 8));
    return 0;
}
static int64_t movie_now(struct movie *m) {
    if (m->rate && m->play_t0 > 0) {
        double el = SDL_GetTicksNS() / 1e9 - m->play_t0;
        int64_t t = m->time0 + (int64_t)(el * m->timescale * (m->rate / 65536.0));
        if (t > m->duration) t = m->duration;
        if (t < 0) t = 0;
        m->time = t;
    }
    return m->time;
}
static void movie_seek(struct movie *m, int64_t t) {
    if (t < 0) t = 0; if (t > m->duration) t = m->duration;
    m->time = m->time0 = t; m->play_t0 = SDL_GetTicksNS() / 1e9; m->audio_fed = -1;
}
static void movie_play(struct movie *m, int32_t rate) {
    movie_now(m);
    m->rate = rate; m->time0 = m->time; m->play_t0 = SDL_GetTicksNS() / 1e9; m->audio_fed = -1;
    QT_LOG("[qt] play rate %d at %lld\n", rate, (long long)m->time);
}
// ---------------------------------------------------------------- output (called from the presentation)
extern int user_window_visible(uint32_t hwnd);
extern SDL_AudioDeviceID audio_device(void);
static void decode_to(struct movie *m, int frame) {
    struct track *t = &m->tr[m->vt];
    if (frame >= t->ns) frame = t->ns - 1;
    if (frame < 0 || frame == m->dec_frame) return;
    if (!m->dec) { m->dec = calloc(1, sizeof *m->dec); m->dec->w = m->w; m->dec->h = m->h; m->dec->rgb = m->rgb; m->dec_frame = -1; }
    int k = frame < m->dec_frame || m->dec_frame < 0 ? 0 : m->dec_frame + 1;
    if (frame - k > 30) k = frame - 30 > 0 ? frame - 30 : 0;   // long jumps: a few frames are enough for Cinepak keyframes
    for (; k <= frame; k++) {
        uint8_t *b = read_at(m->f, (long)t->s[k].off, t->s[k].size);
        if (b) { cvid_decode(m->dec, b, t->s[k].size); free(b); }
    }
    m->dec_frame = frame;
}
static void audio_pump(struct movie *m) {
    if (m->at < 0 || !audio_device()) return;
    struct track *t = &m->tr[m->at];
    if (!m->as) {
        SDL_AudioSpec sp = { t->bits == 16 ? SDL_AUDIO_S16BE : SDL_AUDIO_U8, t->channels ? t->channels : 1, t->rate ? t->rate : 22050 };
        m->as = SDL_CreateAudioStream(&sp, 0);
        if (!m->as || !SDL_BindAudioStream(audio_device(), m->as)) return;
    }
    if (m->rate != 0x10000 || !m->visible) { if (m->audio_fed != -2) { SDL_ClearAudioStream(m->as); m->audio_fed = -2; } return; }   // sound only at normal speed
    int bps = (t->bits / 8) * (t->channels ? t->channels : 1);
    int64_t now = (int64_t)(movie_now(m) * (double)t->rate / m->timescale);   // audio sample position
    if (m->audio_fed < 0) { SDL_ClearAudioStream(m->as); m->audio_fed = now; }
    int64_t want = now + t->rate / 2;                                          // keep ~0.5 s queued
    while (m->audio_fed < want) {
        int c = 0; while (c + 1 < t->ns && t->s[c + 1].time <= m->audio_fed) c++;
        if (c >= t->ns || m->audio_fed >= t->s[c].time + t->s[c].dur) break;
        int64_t skip = m->audio_fed - t->s[c].time, n = t->s[c].dur - skip;
        if (n <= 0) break;
        uint8_t *b = read_at(m->f, (long)(t->s[c].off + skip * bps), (uint32_t)(n * bps));
        if (!b) break;
        SDL_PutAudioStreamData(m->as, b, (int)(n * bps)); free(b);
        m->audio_fed += n;
    }
}
// overlay the current frame of every visible controller onto the presented XRGB screen
void qt_overlay(uint32_t *rgba, int sw, int sh) {
    for (int k = 0; k < NMOV; k++) {
        struct movie *m = &MV[k];
        if (!m->used || !m->mc || m->vt < 0 || !m->visible || !user_window_visible(m->hwnd)) continue;
        audio_pump(m);
        struct track *t = &m->tr[m->vt];
        int64_t mt = movie_now(m) * t->timescale / (m->timescale ? m->timescale : 600);
        int f = 0; while (f + 1 < t->ns && t->s[f + 1].time <= mt) f++;
        decode_to(m, f);
        int wx, wy, ww, wh; if (!user_window_origin(m->hwnd, 1, &wx, &wy, &ww, &wh)) continue;
        int dw = m->cw > 0 ? m->cw : m->w, dh = m->ch > 0 ? m->ch : m->h;
        // English subtitles: alpha-blend into the decoded frame, with 0.35 s fades
        uint32_t *frame = m->rgb; static uint32_t *tmp; static int tmpn;
        double sec = (double)movie_now(m) / (m->timescale ? m->timescale : 600);
        for (int si = 0; si < m->nsubs; si++) {
            struct sub *su = &m->subs[si];
            if (sec < su->t0 || sec > su->t1) continue;
            double a = 1.0; if (sec - su->t0 < 0.35) a = (sec - su->t0) / 0.35; if (su->t1 - sec < 0.35) a = (su->t1 - sec) / 0.35;
            if (frame == m->rgb) {
                if (tmpn < m->w * m->h) { free(tmp); tmpn = m->w * m->h; tmp = malloc((size_t)tmpn * 4); }
                memcpy(tmp, m->rgb, (size_t)m->w * m->h * 4); frame = tmp;
            }
            for (int y = 0; y < su->h && su->y + y < m->h; y++) for (int x = 0; x < su->w && su->x + x < m->w; x++) {
                const uint8_t *p = su->rgba + ((size_t)y * su->w + x) * 4;
                int al = (int)(p[3] * a); if (!al) continue;
                uint32_t *d = &frame[(su->y + y) * m->w + su->x + x];
                int r = ((*d >> 16) & 255) * (255 - al) / 255 + p[0] * al / 255, g = ((*d >> 8) & 255) * (255 - al) / 255 + p[1] * al / 255, b = (*d & 255) * (255 - al) / 255 + p[2] * al / 255;
                *d = (uint32_t)(r << 16 | g << 8 | b);
            }
        }
        for (int y = 0; y < dh; y++) {
            int sy = wy + m->y + y; if (sy < 0 || sy >= sh || y >= wh) continue;
            int fy = y * m->h / dh;
            for (int x = 0; x < dw; x++) {
                int sx = wx + m->x + x; if (sx < 0 || sx >= sw || x >= ww) continue;
                rgba[sy * sw + sx] = 0xff000000u | frame[fy * m->w + x * m->w / dw];
            }
        }
    }
}
int qt_playing(void) { for (int k = 0; k < NMOV; k++) if (MV[k].used && MV[k].mc && MV[k].rate && MV[k].visible) return 1; return 0; }
static uint32_t cm_initialize(void) { return 0; }
uint32_t qt_proc(int dll, const char *name) {
    static uint32_t m_qt, m_cm, m_init;
    if (qt_log < 0) qt_log = getenv("PERSONA_DEBUG_QT") != 0;
    if (!strcmp(name, "_EntryPoint")) {
        if (dll == 0) { if (!m_qt) m_qt = w32_register("QTIM32::_EntryPoint", qt_entry, 0); return m_qt; }
        if (!m_cm) m_cm = w32_register("CMGR32::_EntryPoint", cm_entry, 0); return m_cm;
    }
    if (dll == 1 && !strcmp(name, "_CMgrInitialize")) { if (!m_init) m_init = w32_register("CMGR32::_CMgrInitialize", cm_initialize, 0); return m_init; }
    return 0;
}
