// KERNEL32 emulation: host-function registry, guest heap, files, resources, profiles,
// process/environment queries, synchronisation objects.
#include "win32.h"
#include <time.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>
#include <SDL3/SDL.h>
#ifndef _WIN32
#include <unistd.h>
#endif

int w32_log_api;
char game_dir[1024] = "data", save_dir[1024] = "save", overlay_dir[1024] = "";
char cur_dir[260] = "D:\\DC";
static uint32_t last_error;

// ---------------------------------------------------------------- host function registry
#define MAX_HOST 4096
static struct api host[MAX_HOST];
static int nhost;
uint32_t w32_register(const char *name, apifn fn, int argbytes) {
    if (nhost >= MAX_HOST) rt_fatal("too many host functions");
    host[nhost] = (struct api){ name, fn, argbytes };
    return MAGIC_BASE + 16u * nhost++;
}
static uint32_t api_missing(void) { return 0; }
static const char *trace_quiet;
void w32_call_host(uint32_t magic) {
    uint32_t k = (magic - MAGIC_BASE) / 16;
    if (k >= (uint32_t)nhost) rt_fatal("call to bad host address %08x", magic);
    struct api *a = &host[k];
    if (a->fn == api_missing) rt_fatal("unimplemented host function %s (called from %08x)", a->name, RD32(ESP));
    if (w32_log_api && (!trace_quiet || !strstr(trace_quiet, a->name)))
        fprintf(stderr, "[api %x] %s(%x, %x, %x, %x, %x, %x, %x, %x, %x) from %08x\n", thr_current_id(), a->name, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4), ARG(5), ARG(6), ARG(7), ARG(8), RD32(ESP));
    uint32_t r = a->fn();
    if (w32_log_api > 1 && (!trace_quiet || !strstr(trace_quiet, a->name))) fprintf(stderr, "    -> %x\n", r);
    EAX = r;
    ESP += 4 + (a->argbytes > 0 ? a->argbytes : 0);
    thr_maybe_yield();
    IRQCHK();
}
static uint32_t com_stub_magic(const char *iface, const char *method) {
    char *nm = malloc(strlen(iface) + strlen(method) + 3);
    sprintf(nm, "%s::%s", iface, method);
    return w32_register(nm, api_missing, 0);
}
uint32_t w32_make_vtable(const char *iface, const struct api *m, int n) {
    uint32_t vt = gmem_alloc(4 * n);
    for (int k = 0; k < n; k++)
        WR32(vt + 4 * k, m[k].fn ? w32_register(m[k].name, m[k].fn, m[k].argbytes) : com_stub_magic(iface, m[k].name));
    return vt;
}
uint32_t w32_new_object(uint32_t vtable, uint32_t size) {
    uint32_t o = gmem_alloc(size < 8 ? 8 : size);
    WR32(o, vtable);
    return o;
}

// ---------------------------------------------------------------- guest heap
// First-fit allocator over [HEAP_LO, HEAP_HI); bookkeeping lives on the host side so guest
// buffer overruns cannot corrupt it.
struct range { uint32_t a, n; };
static struct range *fr; static int nfr, cfr;           // free ranges, sorted by address
static uint32_t *ha, *hs; static uint32_t hcap;          // open-addressing map addr -> size
static void hput(uint32_t a, uint32_t s) {
    uint32_t i = (a >> 4) * 2654435761u & (hcap - 1);
    while (ha[i] && ha[i] != a && ha[i] != 1) i = (i + 1) & (hcap - 1);
    ha[i] = a; hs[i] = s;
}
static int hfind(uint32_t a) {
    uint32_t i = (a >> 4) * 2654435761u & (hcap - 1);
    while (ha[i]) { if (ha[i] == a) return (int)i; i = (i + 1) & (hcap - 1); }
    return -1;
}
static void heap_init(void) {
    cfr = 1024; fr = malloc(sizeof *fr * cfr); nfr = 1;
    fr[0] = (struct range){ HEAP_LO, HEAP_HI - HEAP_LO };
    hcap = 1u << 21; ha = calloc(hcap, 4); hs = calloc(hcap, 4);
}
static uint32_t heap_used, heap_count, heap_tomb;
static void heap_rehash(void) {
    uint32_t *oa = ha, *os = hs; uint32_t oc = hcap;
    ha = calloc(hcap, 4); hs = calloc(hcap, 4);
    for (uint32_t i = 0; i < oc; i++) if (oa[i] > 1) hput(oa[i], os[i]);
    free(oa); free(os); heap_tomb = 0;
}
uint32_t gmem_alloc(uint32_t size) {
    uint32_t n = (size + 15) & ~15u; if (!n) n = 16;
    for (int k = 0; k < nfr; k++) if (fr[k].n >= n) {
        uint32_t a = fr[k].a;
        fr[k].a += n; fr[k].n -= n;
        if (!fr[k].n) { memmove(fr + k, fr + k + 1, sizeof *fr * (nfr - k - 1)); nfr--; }
        if ((heap_count + heap_tomb) * 2 > hcap) { if (heap_count * 4 > hcap) rt_fatal("too many heap blocks"); heap_rehash(); }
        hput(a, n); heap_used += n; heap_count++;
        memset(MEM + a, 0, n);
        return a;
    }
    fprintf(stderr, "guest heap exhausted (%u bytes requested, %u in use)\n", size, heap_used);
    return 0;
}
uint32_t gmem_size(uint32_t a) { int i = hfind(a); return i < 0 ? 0 : hs[i]; }
void gmem_free(uint32_t a) {
    if (!a) return;
    int i = hfind(a);
    if (i < 0) { LOG("free of unknown block %08x\n", a); return; }
    uint32_t n = hs[i]; ha[i] = 1; hs[i] = 0; heap_tomb++;   // tombstone
    heap_used -= n; heap_count--;
    int k = 0;
    { int lo = 0, hi = nfr; while (lo < hi) { int m = (lo + hi) / 2; if (fr[m].a < a) lo = m + 1; else hi = m; } k = lo; }
    if (k > 0 && fr[k - 1].a + fr[k - 1].n == a) {
        fr[k - 1].n += n;
        if (k < nfr && fr[k - 1].a + fr[k - 1].n == fr[k].a) { fr[k - 1].n += fr[k].n; memmove(fr + k, fr + k + 1, sizeof *fr * (nfr - k - 1)); nfr--; }
        return;
    }
    if (k < nfr && a + n == fr[k].a) { fr[k].a = a; fr[k].n += n; return; }
    if (nfr == cfr) { cfr *= 2; fr = realloc(fr, sizeof *fr * cfr); }
    memmove(fr + k + 1, fr + k, sizeof *fr * (nfr - k)); nfr++;
    fr[k] = (struct range){ a, n };
}
uint32_t gmem_realloc(uint32_t a, uint32_t size) {
    if (!a) return gmem_alloc(size);
    uint32_t old = gmem_size(a);
    if (old >= size && old - size < 4096) return a;
    uint32_t b = gmem_alloc(size);
    if (!b) return 0;
    memcpy(MEM + b, MEM + a, old < size ? old : size);
    gmem_free(a);
    return b;
}
void gstr_get(uint32_t a, char *out, int n) {
    int k = 0;
    if (a) for (; k < n - 1; k++) { out[k] = (char)RD8(a + k); if (!out[k]) break; }
    out[k] = 0;
}
void gstr_put(uint32_t a, const char *s) { size_t n = strlen(s) + 1; memcpy(GP(a), s, n); }

// ---------------------------------------------------------------- paths
// Guest view: the CD-ROM is drive D: (the projector is D:\DC\PS.EXE and runs from D:\DC);
// C:\ is a writable scratch drive kept in save_dir/C. Files written on D: go to save_dir/D and
// shadow the read-only originals (copy-on-write).
static int find_ci(const char *dir, const char *name, char *out, size_t n) {
    DIR *d = opendir(dir[0] ? dir : ".");
    if (!d) return 0;
    struct dirent *e; int ok = 0;
    while ((e = readdir(d))) if (!strcasecmp(e->d_name, name)) { snprintf(out, n, "%s/%s", dir, e->d_name); ok = 1; break; }
    closedir(d);
    return ok;
}
static int resolve_ci(const char *root, const char *rel, char *out, size_t n, int for_write) {
    char cur[1024]; snprintf(cur, sizeof cur, "%s", root);
    char tmp[1024]; snprintf(tmp, sizeof tmp, "%s", rel);
    char *comp[64]; int nc = 0, k = 0;
    for (char *s = tmp; *s && nc < 64;) {
        while (*s == '/') *s++ = 0;
        if (!*s) break;
        comp[nc++] = s;
        while (*s && *s != '/') s++;
    }
    for (k = 0; k < nc; k++) {
        if (!strcmp(comp[k], ".")) continue;
        if (!strcmp(comp[k], "..")) { char *sl = strrchr(cur, '/'); if (sl && sl > cur + strlen(root) - 1) *sl = 0; continue; }
        char nx[1024];
        if (!find_ci(cur, comp[k], nx, sizeof nx)) {
            if (!for_write) return 0;
            snprintf(nx, sizeof nx, "%s/%s", cur, comp[k]);
            if (k < nc - 1) MKDIR(nx);
        }
        snprintf(cur, sizeof cur, "%s", nx);
    }
    snprintf(out, n, "%s", cur);
    return 1;
}
// full guest path "X:\a\b" with forward slashes after the drive, '.'/'..' left for resolve_ci
static void guest_full(const char *g, char *out, size_t n) {
    char p[1024]; int k = 0;
    for (const char *s = g; *s && k < 1023; s++) p[k++] = *s == '/' ? '\\' : *s;
    p[k] = 0;
    if (p[0] && p[1] == ':') snprintf(out, n, "%s", p);
    else if (p[0] == '\\') snprintf(out, n, "%c:%s", cur_dir[0], p);
    else snprintf(out, n, "%s\\%s", cur_dir, p);
}
static int contains_ci(const char *h, const char *n) {
    size_t l = strlen(n);
    for (; *h; h++) if (!strncasecmp(h, n, l)) return 1;
    return 0;
}
int host_path(const char *guest, char *out, size_t n, int for_write) {
    char full[1024]; guest_full(guest, full, sizeof full);
    { static const char *hide; static int hi; if (!hi) { hi = 1; hide = getenv("PERSONA_HIDE"); }   // testing: pretend matching files are missing
      if (hide && contains_ci(full, hide)) return 0; }
    char drv = (char)(full[0] | 0x20);
    char rel[1024]; int k = 0;
    for (const char *s = full + 2; *s && k < 1023; s++) rel[k++] = *s == '\\' ? '/' : *s;
    rel[k] = 0;
    char sroot[1100]; snprintf(sroot, sizeof sroot, "%s/%c", save_dir, drv == 'd' ? 'D' : 'C');
    if (for_write) { MKDIR(save_dir); MKDIR(sroot); return resolve_ci(sroot, rel, out, n, 1); }
    if (resolve_ci(sroot, rel, out, n, 0)) return 1;
    if (drv == 'd' && overlay_dir[0] && resolve_ci(overlay_dir, rel, out, n, 0)) return 1;   // translated files
    if (drv == 'd') return resolve_ci(game_dir, rel, out, n, 0);
    return 0;
}

// ---------------------------------------------------------------- file handles
#define NFH 256
static FILE *fh[NFH];
static char *fh_guest[NFH]; static int fh_cow[NFH];
#define FH_BASE 0x100u
#define STD_IN 0x7001u
#define STD_OUT 0x7002u
#define STD_ERR 0x7003u
static FILE *fget(uint32_t h) { return (h >= FH_BASE && h < FH_BASE + NFH) ? fh[h - FH_BASE] : 0; }
// disp: 1 CREATE_NEW 2 CREATE_ALWAYS 3 OPEN_EXISTING 4 OPEN_ALWAYS 5 TRUNCATE_EXISTING
static uint32_t open_guest(const char *name, int wr, int disp) {
    char hp[1024]; FILE *f = 0; int cow = 0;
    int truncate = disp == 1 || disp == 2 || disp == 5;
    if (!wr) {
        if (host_path(name, hp, sizeof hp, 0)) {
            struct stat st;
            if (!stat(hp, &st) && S_ISDIR(st.st_mode)) { last_error = 5; return 0xffffffffu; }
            f = fopen(hp, "rb");
        }
    } else if (truncate) {
        if (host_path(name, hp, sizeof hp, 1)) f = fopen(hp, "w+b");
    } else {
        char sp[1024];
        if (host_path(name, sp, sizeof sp, 1) && (f = fopen(sp, "r+b"))) snprintf(hp, sizeof hp, "%s", sp);
        else if (host_path(name, hp, sizeof hp, 0) && (f = fopen(hp, "rb"))) cow = 1;
        else if (disp == 4 && host_path(name, hp, sizeof hp, 1)) f = fopen(hp, "w+b");
    }
    if (!f) { LOG("open %s: not found\n", name); last_error = 2; return 0xffffffffu; }
    for (int k = 3; k < NFH; k++) if (!fh[k]) {
        fh[k] = f; fh_cow[k] = cow; free(fh_guest[k]); fh_guest[k] = strdup(name);
        LOG("open %s -> %s%s = %x\n", name, hp, cow ? " (cow)" : "", FH_BASE + k);
        return FH_BASE + k;
    }
    fclose(f); return 0xffffffffu;
}
static FILE *fget_w(uint32_t h) {
    if (h < FH_BASE || h >= FH_BASE + NFH) return 0;
    int k = (int)(h - FH_BASE);
    if (!fh[k] || !fh_cow[k]) return fh[k];
    char sp[1024];
    if (!host_path(fh_guest[k], sp, sizeof sp, 1)) return 0;
    long pos = ftell(fh[k]);
    FILE *o = fopen(sp, "w+b"); if (!o) return 0;
    fseek(fh[k], 0, SEEK_SET);
    char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, fh[k])) > 0) fwrite(buf, 1, n, o);
    fclose(fh[k]); fh[k] = o; fh_cow[k] = 0;
    fseek(o, pos, SEEK_SET);
    return o;
}

static uint32_t k_CreateFileA(void) {
    char name[512]; gstr_get(ARG(0), name, sizeof name);
    return open_guest(name, (ARG(1) & 0x40000000u) != 0, (int)ARG(4));
}
static uint32_t k__lopen(void) {
    char name[512]; gstr_get(ARG(0), name, sizeof name);
    uint32_t h = open_guest(name, (ARG(1) & 3) != 0, 3);
    return h;
}
static uint32_t k__lcreat(void) { char name[512]; gstr_get(ARG(0), name, sizeof name); return open_guest(name, 1, 2); }
static uint32_t k_CloseHandle(void);
static uint32_t k__lclose(void) { FILE *f = fget(ARG(0)); if (f) { fclose(f); fh[ARG(0) - FH_BASE] = 0; } return 0; }
static uint32_t k_ReadFile(void) {
    FILE *f = fget(ARG(0)); uint32_t n = 0;
    if (f) n = (uint32_t)fread(GP(ARG(1)), 1, ARG(2), f);
    if (ARG(3)) WR32(ARG(3), n);
    return f != 0;
}
static uint32_t k__lread(void) { FILE *f = fget(ARG(0)); return f ? (uint32_t)fread(GP(ARG(1)), 1, ARG(2), f) : 0xffffffffu; }
static uint32_t k_WriteFile(void) {
    uint32_t h = ARG(0), n = ARG(2);
    if (h == STD_OUT || h == STD_ERR) { fwrite(GP(ARG(1)), 1, n, stderr); }
    else { FILE *f = fget_w(h); if (!f) return 0; n = (uint32_t)fwrite(GP(ARG(1)), 1, n, f); fflush(f); }
    if (ARG(3)) WR32(ARG(3), n);
    return 1;
}
static uint32_t k__lwrite(void) {
    FILE *f = fget_w(ARG(0)); if (!f) return 0xffffffffu;
    uint32_t n = ARG(2);
    if (!n) { long p = ftell(f); (void)p; return 0; }     // _lwrite(h, p, 0) truncates: rarely used
    n = (uint32_t)fwrite(GP(ARG(1)), 1, n, f); fflush(f); return n;
}
static uint32_t k_SetFilePointer(void) {
    FILE *f = fget(ARG(0)); if (!f) return 0xffffffffu;
    int64_t d = (int32_t)ARG(1);
    if (ARG(2)) d = (int64_t)(((uint64_t)RD32(ARG(2)) << 32) | ARG(1));
    FSEEK64(f, d, ARG(3) == 0 ? SEEK_SET : ARG(3) == 1 ? SEEK_CUR : SEEK_END);
    int64_t pos = FTELL64(f);
    if (ARG(2)) WR32(ARG(2), (uint32_t)(pos >> 32));
    return (uint32_t)pos;
}
static uint32_t k__llseek(void) {
    FILE *f = fget(ARG(0)); if (!f) return 0xffffffffu;
    fseek(f, (int32_t)ARG(1), ARG(2) == 0 ? SEEK_SET : ARG(2) == 1 ? SEEK_CUR : SEEK_END);
    return (uint32_t)ftell(f);
}
static uint32_t k_SetEndOfFile(void) {
    FILE *f = fget_w(ARG(0)); if (!f) return 0;
    long p = ftell(f); fflush(f);
#ifndef _WIN32
    if (ftruncate(fileno(f), p)) return 0;
#endif
    return 1;
}
static uint32_t k_FlushFileBuffers(void) { FILE *f = fget(ARG(0)); if (f) fflush(f); return 1; }
static uint32_t k_GetFileType(void) { uint32_t h = ARG(0); if (h >= STD_IN && h <= STD_ERR) return 2; return fget(h) ? 1 : 0; }
static uint32_t k_GetStdHandle(void) { int32_t n = (int32_t)ARG(0); return n == -10 ? STD_IN : n == -11 ? STD_OUT : n == -12 ? STD_ERR : 0xffffffffu; }
static uint32_t k_GetFileAttributesA(void) {
    char name[512], hp[1024]; gstr_get(ARG(0), name, sizeof name);
    struct stat st;
    if (strlen(name) <= 3 && name[1] == ':') return 0x10;
    if (!host_path(name, hp, sizeof hp, 0) || stat(hp, &st)) { last_error = 2; return 0xffffffffu; }
    uint32_t a = S_ISDIR(st.st_mode) ? 0x10 : 0x20;
    if (!S_ISDIR(st.st_mode) && strncmp(hp, save_dir, strlen(save_dir))) a |= 1;   // CD files are read-only
    return a;
}
static uint32_t k_DeleteFileA(void) {
    char name[512], hp[1024]; gstr_get(ARG(0), name, sizeof name);
    if (!host_path(name, hp, sizeof hp, 0) || strncmp(hp, save_dir, strlen(save_dir))) { last_error = 5; return 0; }
    return remove(hp) == 0;
}
static uint32_t k_MoveFileA(void) {
    char a[512], b[512], ha_[1024], hb[1024]; gstr_get(ARG(0), a, sizeof a); gstr_get(ARG(1), b, sizeof b);
    if (!host_path(a, ha_, sizeof ha_, 0) || !host_path(b, hb, sizeof hb, 1)) return 0;
    return rename(ha_, hb) == 0;
}
// OpenFile(name, OFSTRUCT*, style)
static uint32_t k_OpenFile(void) {
    char name[512]; gstr_get(ARG(0), name, sizeof name);
    uint32_t of = ARG(1), st = ARG(2);
    char full[300]; guest_full(name, full, sizeof full);
    if (of) { WR8(of, 136); WR8(of + 1, 0); WR16(of + 2, 0); gstr_put(of + 8, full); }
    if (st & 0x0100) return 0;                                        // OF_PARSE
    if (st & 0x0200) { char hp[1024]; if (host_path(name, hp, sizeof hp, 0)) remove(hp); return 1; }   // OF_DELETE
    uint32_t h;
    if (st & 0x1000) h = open_guest(name, 1, 2);                      // OF_CREATE
    else h = open_guest(name, (st & 3) != 0, 3);
    if (h == 0xffffffffu) { if (of) WR16(of + 2, 2); return 0xffffffffu; }
    if (st & 0x4000) { FILE *f = fget(h); if (f) { fclose(f); fh[h - FH_BASE] = 0; } return 1; }   // OF_EXIST
    return h;
}
// ---------------------------------------------------------------- directory search
struct find { int used; DIR *d; char dir[1024]; char pat[260]; };
static struct find F[16];
static int wild_match(const char *p, const char *s) {
    for (; *p; p++, s++) {
        if (*p == '*') { for (const char *t = s;; t++) { if (wild_match(p + 1, t)) return 1; if (!*t) return 0; } }
        if (!*s) return *p == '.' && p[1] == '*' && !p[2] ? 1 : 0;
        if (*p != '?' && tolower((unsigned char)*p) != tolower((unsigned char)*s)) return 0;
    }
    return !*s;
}
static int find_next(struct find *f, uint32_t out) {
    struct dirent *e;
    while (f->d && (e = readdir(f->d))) {
        if (!wild_match(f->pat, e->d_name) && !(strcmp(f->pat, "*.*") == 0)) continue;
        char p[1300]; snprintf(p, sizeof p, "%s/%s", f->dir, e->d_name);
        struct stat st; if (stat(p, &st)) continue;
        memset(GP(out), 0, 318);
        WR32(out, S_ISDIR(st.st_mode) ? 0x10 : 0x21);
        WR32(out + 32, (uint32_t)st.st_size);
        char up[260]; snprintf(up, sizeof up, "%s", e->d_name);
        gstr_put(out + 44, up);
        return 1;
    }
    return 0;
}
static uint32_t k_FindFirstFileA(void) {
    char name[512]; gstr_get(ARG(0), name, sizeof name);
    char full[1024]; guest_full(name, full, sizeof full);
    char *sl = strrchr(full, '\\');
    char dirg[1024], pat[260];
    if (sl) { *sl = 0; snprintf(dirg, sizeof dirg, "%s\\", full); snprintf(pat, sizeof pat, "%s", sl + 1); }
    else { snprintf(dirg, sizeof dirg, "%s", cur_dir); snprintf(pat, sizeof pat, "%s", full); }
    char hp[1024];
    int k; for (k = 0; k < 16 && F[k].used; k++);
    if (k == 16 || !host_path(dirg, hp, sizeof hp, 0)) { last_error = 2; return 0xffffffffu; }
    F[k].used = 1; F[k].d = opendir(hp); snprintf(F[k].dir, sizeof F[k].dir, "%s", hp); snprintf(F[k].pat, sizeof F[k].pat, "%s", pat);
    if (!find_next(&F[k], ARG(1))) { if (F[k].d) closedir(F[k].d); F[k].used = 0; last_error = 2; return 0xffffffffu; }
    return 0x6600u + k;
}
static uint32_t k_FindNextFileA(void) {
    uint32_t k = ARG(0) - 0x6600u; if (k >= 16 || !F[k].used) return 0;
    if (!find_next(&F[k], ARG(1))) { last_error = 18; return 0; }
    return 1;
}
static uint32_t k_FindClose(void) { uint32_t k = ARG(0) - 0x6600u; if (k < 16 && F[k].used) { if (F[k].d) closedir(F[k].d); F[k].used = 0; } return 1; }
static uint32_t k_GetDiskFreeSpaceA(void) {
    if (ARG(1)) WR32(ARG(1), 8); if (ARG(2)) WR32(ARG(2), 512); if (ARG(3)) WR32(ARG(3), 200000); if (ARG(4)) WR32(ARG(4), 400000);
    return 1;
}
static uint32_t k_GetLogicalDrives(void) { return (1u << 2) | (1u << 3); }
static uint32_t k_GetDriveTypeA(void) {
    char r[16] = "D"; if (ARG(0)) gstr_get(ARG(0), r, sizeof r); else r[0] = cur_dir[0];
    char d = (char)(r[0] | 0x20);
    return d == 'd' ? 5 : d == 'c' ? 3 : 1;
}
static uint32_t k_GetWindowsDirectoryA(void) { const char *s = "C:\\WINDOWS"; if (ARG(1) > strlen(s)) gstr_put(ARG(0), s); return (uint32_t)strlen(s); }
static uint32_t k_GetCurrentDirectoryA(void) { if (ARG(0) > strlen(cur_dir)) gstr_put(ARG(1), cur_dir); return (uint32_t)strlen(cur_dir); }
static uint32_t k_SetCurrentDirectoryA(void) {
    char p[260]; gstr_get(ARG(0), p, sizeof p);
    char full[1024]; guest_full(p, full, sizeof full);
    size_t n = strlen(full); while (n > 3 && full[n - 1] == '\\') full[--n] = 0;
    snprintf(cur_dir, sizeof cur_dir, "%s", full);
    LOG("SetCurrentDirectory %s\n", cur_dir);
    return 1;
}
static uint32_t k_GetFullPathNameA(void) {
    char in[512], out[1024]; gstr_get(ARG(0), in, sizeof in);
    guest_full(in, out, sizeof out);
    uint32_t n = (uint32_t)strlen(out);
    if (ARG(1) > n) { gstr_put(ARG(2), out); if (ARG(3)) { char *b = strrchr(out, '\\'); WR32(ARG(3), ARG(2) + (uint32_t)(b ? b - out + 1 : 0)); } }
    return n;
}
static uint32_t k_FileTimeToLocalFileTime(void) { WR32(ARG(1), RD32(ARG(0))); WR32(ARG(1) + 4, RD32(ARG(0) + 4)); return 1; }
static uint32_t k_FileTimeToSystemTime(void) {
    uint64_t t = RD32(ARG(0)) | ((uint64_t)RD32(ARG(0) + 4) << 32);
    time_t s = (time_t)(t / 10000000ull - 11644473600ull);
    struct tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &s);
#else
    gmtime_r(&s, &tm);
#endif
    uint32_t p = ARG(1);
    WR16(p, tm.tm_year + 1900); WR16(p + 2, tm.tm_mon + 1); WR16(p + 4, tm.tm_wday); WR16(p + 6, tm.tm_mday);
    WR16(p + 8, tm.tm_hour); WR16(p + 10, tm.tm_min); WR16(p + 12, tm.tm_sec); WR16(p + 14, 0);
    return 1;
}

// ---------------------------------------------------------------- memory APIs
// Handles are the block addresses (GMEM_MOVEABLE memory never actually moves while locked).
static uint32_t k_GlobalAlloc(void) { uint32_t a = gmem_alloc(ARG(1) ? ARG(1) : 1); return a; }
static uint32_t k_GlobalLock(void) { return ARG(0); }
static uint32_t k_GlobalUnlock(void) { return 0; }
static uint32_t k_GlobalHandle(void) { return ARG(0); }
static uint32_t k_GlobalFree(void) { gmem_free(ARG(0)); return 0; }
static uint32_t k_GlobalSize(void) { return gmem_size(ARG(0)); }
static uint32_t k_GlobalReAlloc(void) {
    uint32_t h = ARG(0), n = ARG(1), fl = ARG(2);
    if (fl & 0x80) return h;                         // GMEM_MODIFY
    uint32_t old = gmem_size(h), q = gmem_realloc(h, n ? n : 1);
    if (q && n > old && (fl & 0x40)) memset(GP(q + old), 0, n - old);
    return q;
}
static uint32_t k_GlobalCompact(void) { return 64u << 20; }
static uint32_t k_GlobalMemoryStatus(void) {
    uint32_t p = ARG(0);
    WR32(p, 32); WR32(p + 4, 30); WR32(p + 8, 64u << 20); WR32(p + 12, 48u << 20);
    WR32(p + 16, 128u << 20); WR32(p + 20, 100u << 20); WR32(p + 24, 0x7ffe0000u); WR32(p + 28, 0x70000000u);
    return 0;
}
// VirtualAlloc: reservations are 64 KB aligned (SmartHeap masks pointers to find its pages)
struct vres { uint32_t base, size; };
static struct vres VR[512]; static int nvr;
static int vr_find(uint32_t a) { for (int k = 0; k < nvr; k++) if (a >= VR[k].base && a < VR[k].base + VR[k].size) return k; return -1; }
static uint32_t va_reserve(uint32_t n) {
    n = (n + 0xffffu) & ~0xffffu;
    uint32_t a = VA_LO;
    for (;;) {
        int clash = 0;
        for (int k = 0; k < nvr; k++) if (a < VR[k].base + VR[k].size && VR[k].base < a + n) { a = (VR[k].base + VR[k].size + 0xffffu) & ~0xffffu; clash = 1; }
        if (!clash) break;
    }
    if (a + n > VA_HI || nvr == 512) { fprintf(stderr, "VirtualAlloc: out of address space (%u)\n", n); return 0; }
    VR[nvr++] = (struct vres){ a, n };
    memset(MEM + a, 0, n);
    return a;
}
static uint32_t k_VirtualAlloc(void) {
    uint32_t a = ARG(0), n = ARG(1);
    if (a) {
        int k = vr_find(a);
        if (k >= 0) return a & ~0xfffu;              // commit inside a reservation
        if (ARG(2) & 0x2000) { LOG("VirtualAlloc at %08x refused\n", a); return 0; }
        return 0;
    }
    return va_reserve(n);
}
static uint32_t k_VirtualFree(void) {
    uint32_t a = ARG(0), n = ARG(1), type = ARG(2);
    int k = vr_find(a);
    if (k < 0) return 0;
    if (type & 0x8000) { VR[k] = VR[--nvr]; }
    else if (type & 0x4000) memset(GP(a & ~0xfffu), 0, (n + 0xfffu) & ~0xfffu);
    return 1;
}
static uint32_t k_VirtualQuery(void) {
    uint32_t a = ARG(0), p = ARG(1);
    memset(GP(p), 0, 28);
    int k = vr_find(a);
    uint32_t page = a & ~0xfffu;
    WR32(p, page);
    if (k >= 0) { WR32(p + 4, VR[k].base); WR32(p + 8, 4); WR32(p + 12, VR[k].base + VR[k].size - page); WR32(p + 16, 0x1000); WR32(p + 20, 4); WR32(p + 24, 0x20000); }
    else if (a >= 0x400000u && a < 0x400000u + 0x136000u) { WR32(p + 4, 0x400000u); WR32(p + 8, 0x80); WR32(p + 12, 0x1000); WR32(p + 16, 0x1000); WR32(p + 20, 0x80); WR32(p + 24, 0x1000000); }
    else if (a >= HEAP_LO && a < HEAP_HI) { WR32(p + 4, HEAP_LO); WR32(p + 8, 4); WR32(p + 12, 0x1000); WR32(p + 16, 0x1000); WR32(p + 20, 4); WR32(p + 24, 0x20000); }
    else { WR32(p + 12, 0x10000); WR32(p + 16, 0x10000); WR32(p + 20, 1); }
    return 28;
}
static uint32_t k_IsBad(void) { return 0; }

// ---------------------------------------------------------------- strings / locale (Japanese system, code page 932)
static int is_lead(unsigned c) { return (c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc); }
static uint32_t k_IsDBCSLeadByte(void) { return is_lead(ARG(0) & 0xff); }
static uint32_t k_lstrlenA(void) { return ARG(0) ? (uint32_t)strlen((char *)GP(ARG(0))) : 0; }
static uint32_t k_lstrlenW(void) { uint32_t n = 0; if (ARG(0)) while (RD16(ARG(0) + 2 * n)) n++; return n; }
static uint32_t k_lstrcpyA(void) { if (ARG(1)) memmove(GP(ARG(0)), GP(ARG(1)), strlen((char *)GP(ARG(1))) + 1); return ARG(0); }
static uint32_t k_lstrcatA(void) { char *d = (char *)GP(ARG(0)); memmove(d + strlen(d), GP(ARG(1)), strlen((char *)GP(ARG(1))) + 1); return ARG(0); }
static uint32_t k_lstrcmpiA(void) { int r = strcasecmp((char *)GP(ARG(0)), (char *)GP(ARG(1))); return (uint32_t)(r < 0 ? -1 : r > 0); }
static uint32_t k_GetACP(void) { return 932; }
static uint32_t k_GetOEMCP(void) { return 932; }
static uint32_t k_GetCPInfo(void) {
    uint32_t p = ARG(1); memset(GP(p), 0, 20);
    WR32(p, 2); WR8(p + 4, 0x81); WR8(p + 5, 0x40);
    WR8(p + 6, 0x81); WR8(p + 7, 0x9f); WR8(p + 8, 0xe0); WR8(p + 9, 0xfc);
    return 1;
}
// SJIS <-> UTF-16 through SDL's iconv
static uint32_t k_MultiByteToWideChar(void) {
    uint32_t src = ARG(2), dst = ARG(4); int32_t n = (int32_t)ARG(3); uint32_t cap = ARG(5);
    if (n < 0) n = (int32_t)strlen((char *)GP(src)) + 1;
    uint16_t tmp[4096]; uint32_t m = 0;
    for (int32_t k = 0; k < n && m < 4096;) {
        unsigned c = RD8(src + k);
        if (c < 0x80) { tmp[m++] = (uint16_t)c; k++; continue; }
        char in[3] = { (char)c, 0, 0 }; size_t il = 1;
        if (is_lead(c) && k + 1 < n) { in[1] = (char)RD8(src + k + 1); il = 2; }
        char *u = SDL_iconv_string("UTF-16LE", "CP932", in, il);
        uint16_t w = '?'; if (u) { memcpy(&w, u, 2); SDL_free(u); }
        if (il == 1 && c >= 0xa1 && c <= 0xdf) w = (uint16_t)(0xff61 + (c - 0xa1));
        tmp[m++] = w; k += (int)il;
    }
    if (!cap) return m;
    uint32_t k = 0; for (; k < m && k < cap; k++) WR16(dst + 2 * k, tmp[k]);
    return k;
}
static uint32_t k_WideCharToMultiByte(void) {
    uint32_t src = ARG(2), dst = ARG(4); int32_t n = (int32_t)ARG(3); uint32_t cap = ARG(5);
    if (n < 0) { n = 0; while (RD16(src + 2 * n)) n++; n++; }
    char tmp[8192]; uint32_t m = 0;
    for (int32_t k = 0; k < n && m < 8190; k++) {
        uint16_t w = RD16(src + 2 * k);
        if (w < 0x80) { tmp[m++] = (char)w; continue; }
        char *s = SDL_iconv_string("CP932", "UTF-16LE", (const char *)&w, 2);
        if (s && s[0]) { size_t l = strlen(s); memcpy(tmp + m, s, l); m += (uint32_t)l; } else tmp[m++] = '?';
        SDL_free(s);
    }
    if (!cap) return m;
    uint32_t k = 0; for (; k < m && k < cap; k++) WR8(dst + k, (uint8_t)tmp[k]);
    return k;
}
static uint32_t k_GetStringTypeA(void) {
    uint32_t src = ARG(2), out = ARG(4); int32_t n = (int32_t)ARG(3);
    if (n < 0) n = (int32_t)strlen((char *)GP(src)) + 1;
    for (int k = 0; k < n; k++) {
        unsigned c = RD8(src + k); uint16_t t = 0;
        if (c >= 'A' && c <= 'Z') t |= 0x101; if (c >= 'a' && c <= 'z') t |= 0x102;
        if (c >= '0' && c <= '9') t |= 0x84; if (c == ' ' || (c >= 9 && c <= 13)) t |= 0x8;
        if (c == ' ' || c == 9) t |= 0x40; if (c < 32 || c == 127) t |= 0x20;
        if (c > 32 && c < 127 && !(t & 0x107)) t |= 0x10;
        WR16(out + 2 * k, t);
    }
    return 1;
}
static uint32_t k_CompareStringA(void) {
    uint32_t a = ARG(2), b = ARG(4); int32_t na = (int32_t)ARG(3), nb = (int32_t)ARG(5);
    if (na < 0) na = (int32_t)strlen((char *)GP(a)); if (nb < 0) nb = (int32_t)strlen((char *)GP(b));
    int n = na < nb ? na : nb;
    int r = (ARG(1) & 1) ? strncasecmp((char *)GP(a), (char *)GP(b), n) : memcmp(GP(a), GP(b), n);
    if (!r) r = na - nb;
    return r < 0 ? 1 : r > 0 ? 3 : 2;
}
static uint32_t k_GetDateFormatA(void) {
    // (lcid, flags, SYSTEMTIME*, fmt, out, cap) -> "yyyy/MM/dd" (Japanese short date)
    uint32_t st = ARG(2); char b[64];
    struct tm t; time_t now = time(0);
#ifdef _WIN32
    localtime_s(&t, &now);
#else
    localtime_r(&now, &t);
#endif
    int y = st ? RD16(st) : t.tm_year + 1900, m = st ? RD16(st + 2) : t.tm_mon + 1, d = st ? RD16(st + 6) : t.tm_mday;
    snprintf(b, sizeof b, "%04d/%02d/%02d", y, m, d);
    uint32_t n = (uint32_t)strlen(b) + 1;
    if (!ARG(5)) return n;
    if (ARG(5) < n) return 0;
    gstr_put(ARG(4), b); return n;
}
static uint32_t k_MulDiv(void) {
    int64_t a = (int32_t)ARG(0), b = (int32_t)ARG(1), c = (int32_t)ARG(2);
    if (!c) return 0xffffffffu;
    int64_t p = a * b; int64_t r = (p >= 0) == (c > 0) ? (p + c / 2) / c : (p - c / 2) / c;
    return (uint32_t)(int32_t)r;
}

// ---------------------------------------------------------------- atoms
static char *atoms[256];
static uint32_t k_GlobalAddAtomA(void) {
    char s[256]; gstr_get(ARG(0), s, sizeof s);
    for (int k = 1; k < 256; k++) if (atoms[k] && !strcasecmp(atoms[k], s)) return 0xc000u + k;
    for (int k = 1; k < 256; k++) if (!atoms[k]) { atoms[k] = strdup(s); return 0xc000u + k; }
    return 0;
}
static uint32_t k_GlobalDeleteAtom(void) { return 0; }

// ---------------------------------------------------------------- resources (main module image)
static uint32_t rsrc_base;
static int res_name_eq(uint32_t ent_name, uint32_t want) {
    if (want < 0x10000) return !(ent_name & 0x80000000u) && ent_name == want;
    if (!(ent_name & 0x80000000u)) {
        char s[64]; gstr_get(want, s, sizeof s);
        return s[0] == '#' && (uint32_t)atoi(s + 1) == ent_name;
    }
    uint32_t p = rsrc_base + (ent_name & 0x7fffffffu);
    uint16_t n = RD16(p);
    char s[256]; gstr_get(want, s, sizeof s);
    if (strlen(s) != n) return 0;
    for (int k = 0; k < n; k++) if (toupper((unsigned char)s[k]) != toupper(RD16(p + 2 + 2 * k) & 0xff)) return 0;
    return 1;
}
static uint32_t res_lookup(uint32_t dir, uint32_t want) {
    uint32_t n = RD16(dir + 12) + RD16(dir + 14);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t e = dir + 16 + 8 * k;
        if (want == 0xffffffffu || res_name_eq(RD32(e), want)) return RD32(e + 4);
    }
    return 0;
}
// returns the data entry address (guest) or 0
uint32_t res_find(uint32_t type, uint32_t name, uint32_t *size) {
    if (!rsrc_base) return 0;
    uint32_t t = res_lookup(rsrc_base, type); if (!t || !(t & 0x80000000u)) return 0;
    uint32_t nm = res_lookup(rsrc_base + (t & 0x7fffffffu), name); if (!nm || !(nm & 0x80000000u)) return 0;
    uint32_t lang = res_lookup(rsrc_base + (nm & 0x7fffffffu), 0xffffffffu); if (!lang || (lang & 0x80000000u)) return 0;
    uint32_t de = rsrc_base + lang;
    if (size) *size = RD32(de + 4);
    return de;
}
static uint32_t k_FindResourceA(void) {
    uint32_t de = res_find(ARG(2), ARG(1), 0);
    if (!de) { char s[64] = ""; if (ARG(1) >= 0x10000) gstr_get(ARG(1), s, sizeof s); LOG("FindResource(%x '%s', type %x) failed\n", ARG(1), s, ARG(2)); }
    return de;
}
static uint32_t k_LoadResource(void) { uint32_t de = ARG(1); return de ? 0x400000u + RD32(de) : 0; }
static uint32_t k_LockResource(void) { return ARG(0); }
static uint32_t k_SizeofResource(void) { return ARG(1) ? RD32(ARG(1) + 4) : 0; }
static uint32_t k_FreeResource(void) { return 0; }

// ---------------------------------------------------------------- profiles (.ini)
static int ini_get(const char *file, const char *sec, const char *key, char *out, size_t n) {
    char hp[1024];
    if (!host_path(file, hp, sizeof hp, 0)) return 0;
    FILE *f = fopen(hp, "rb"); if (!f) return 0;
    char line[1024]; int in = 0, ok = 0;
    while (fgets(line, sizeof line, f)) {
        char *s = line; while (*s == ' ' || *s == '\t') s++;
        char *e = s + strlen(s); while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
        if (*s == '[') { char *c = strchr(s, ']'); if (c) *c = 0; in = !strcasecmp(s + 1, sec); continue; }
        if (!in || *s == ';') continue;
        char *eq = strchr(s, '='); if (!eq) continue;
        *eq = 0; char *k2 = eq; while (k2 > s && k2[-1] == ' ') *--k2 = 0;
        if (!strcasecmp(s, key)) { char *v = eq + 1; while (*v == ' ') v++; snprintf(out, n, "%s", v); ok = 1; break; }
    }
    fclose(f);
    return ok;
}
static uint32_t prof_string(const char *file, uint32_t sec, uint32_t key, uint32_t def, uint32_t out, uint32_t n) {
    char s[256] = "", k[256] = "", d[1024] = "", v[1024];
    gstr_get(sec, s, sizeof s); gstr_get(def, d, sizeof d);
    if (!key) { if (n >= 2) { WR8(out, 0); WR8(out + 1, 0); } return 0; }
    gstr_get(key, k, sizeof k);
    if (!file || !ini_get(file, s, k, v, sizeof v)) snprintf(v, sizeof v, "%s", d);
    LOG("profile %s [%s] %s = '%s'\n", file ? file : "win.ini", s, k, v);
    if (!n) return 0;
    size_t l = strlen(v); if (l > n - 1) l = n - 1;
    memcpy(GP(out), v, l); WR8(out + (uint32_t)l, 0);
    return (uint32_t)l;
}
static uint32_t k_GetPrivateProfileStringA(void) { char f[512]; gstr_get(ARG(5), f, sizeof f); return prof_string(f, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4)); }
static uint32_t k_GetProfileStringA(void) { return prof_string(0, ARG(0), ARG(1), ARG(2), ARG(3), ARG(4)); }
static uint32_t k_GetPrivateProfileIntA(void) {
    char f[512], s[256], k[256], v[64]; gstr_get(ARG(3), f, sizeof f); gstr_get(ARG(0), s, sizeof s); gstr_get(ARG(1), k, sizeof k);
    uint32_t r = ini_get(f, s, k, v, sizeof v) ? (uint32_t)strtol(v, 0, 0) : ARG(2);
    LOG("profile int %s [%s] %s = %d\n", f, s, k, r);
    return r;
}
static uint32_t k_GetProfileIntA(void) { return ARG(2); }
static uint32_t k_WriteProfileStringA(void) { return 1; }

// ---------------------------------------------------------------- process / environment
static uint32_t k_GetVersion(void) { return 0xC0000004u; }   // Windows 95
static uint32_t k_GetVersionExA(void) {
    uint32_t p = ARG(0);
    WR32(p + 4, 4); WR32(p + 8, 0); WR32(p + 12, 950); WR32(p + 16, 1); WR8(p + 20, 0);
    return 1;
}
static uint32_t k_GetSystemInfo(void) {
    uint32_t p = ARG(0); memset(GP(p), 0, 36);
    WR32(p + 4, 0x1000); WR32(p + 8, 0x10000); WR32(p + 12, 0x7ffeffff); WR32(p + 16, 1); WR32(p + 20, 1); WR32(p + 24, 586); WR32(p + 28, 0x10000); WR16(p + 32, 5);
    return 0;
}
static uint32_t k_GetCommandLineA(void) { return HOSTSTR; }
static uint32_t k_GetEnvironmentStrings(void) { return HOSTSTR + 0x100; }
static uint32_t k_GetStartupInfoA(void) { memset(GP(ARG(0)), 0, 68); WR32(ARG(0), 68); return 0; }
#define FAKE_KERNEL32 0x7fe00000u
static uint32_t k_GetModuleHandleA(void) {
    if (!ARG(0)) return 0x400000u;
    char n[64]; gstr_get(ARG(0), n, sizeof n);
    if (!strncasecmp(n, "kernel32", 8)) return FAKE_KERNEL32;
    if (!strncasecmp(n, "user32", 6)) return FAKE_KERNEL32 + 0x10000;
    LOG("GetModuleHandle(%s) -> 0\n", n);
    return 0;
}
static uint32_t k_GetModuleFileNameA(void) {
    const char *s = ARG(0) == 0x7e000000u ? "C:\\WINDOWS\\SYSTEM\\QTIM32.DLL" : ARG(0) == 0x7e100000u ? "C:\\WINDOWS\\SYSTEM\\CMGR32.DLL" : "D:\\DC\\PS.EXE";
    uint32_t n = ARG(2);
    if (n) { strncpy((char *)GP(ARG(1)), s, n); WR8(ARG(1) + n - 1, 0); }
    return (uint32_t)strlen(s);
}
static uint32_t k_one(void) { return 1; }
static uint32_t k_zero(void) { return 0; }
static uint32_t k_GetLastError(void) { return last_error; }
static uint32_t k_SetErrorMode(void) { return 0; }
static uint32_t k_ExitProcess(void) { fprintf(stderr, "ExitProcess(%d)\n", ARG(0)); w32_shutdown(); exit((int)ARG(0)); }
static uint32_t k_ExitThread(void) { thr_exit(ARG(0)); }
static uint32_t k_RaiseException(void) { rt_fatal("RaiseException(%08x)", ARG(0)); }
static uint32_t k_RtlUnwind(void) { rt_fatal("RtlUnwind (structured exception unwinding is not supported)"); }
// QuickTime for Windows (QTIM32.DLL) and its Component Manager (CMGR32.DLL) are emulated in w32_qt.c
#define FAKE_QTIM32 0x7e000000u
#define FAKE_CMGR32 0x7e100000u
#define FAKE_VERSION 0x7e200000u
// VERSION.DLL: only used to read QTIM32's file version (Director needs QuickTime >= 2.0); reports 2.1.2.59
static uint32_t v_GetFileVersionInfoSizeA(void) { if (ARG(1)) WR32(ARG(1), 0); return 0x100; }
static uint32_t v_GetFileVersionInfoA(void) {
    uint32_t p = ARG(3); memset(GP(p), 0, ARG(2) < 0x100 ? ARG(2) : 0x100);
    uint32_t fi = p + 0x28;                        // VS_FIXEDFILEINFO
    WR32(fi, 0xFEEF04BDu); WR32(fi + 4, 0x10000); WR32(fi + 8, 0x00020001u); WR32(fi + 12, 0x0002003Bu);
    WR32(fi + 16, 0x00020001u); WR32(fi + 20, 0x0002003Bu);
    return 1;
}
static uint32_t v_VerQueryValueA(void) {
    char sub[64]; gstr_get(ARG(1), sub, sizeof sub);
    if (strcmp(sub, "\\")) return 0;
    WR32(ARG(2), ARG(0) + 0x28); WR32(ARG(3), 52); return 1;
}
extern uint32_t qt_proc(int dll, const char *name);
static uint32_t k_LoadLibraryA(void) {
    char n[256]; gstr_get(ARG(0), n, sizeof n);
    const char *b = strrchr(n, '\\'); b = b ? b + 1 : n;
    if (!getenv("PERSONA_NO_QT")) {
        if (!strncasecmp(b, "QTIM32", 6)) { LOG("LoadLibraryA(%s) -> emulated QuickTime\n", n); return FAKE_QTIM32; }
        if (!strncasecmp(b, "CMGR32", 6)) { LOG("LoadLibraryA(%s) -> emulated Component Manager\n", n); return FAKE_CMGR32; }
        if (!strncasecmp(b, "version", 7)) return FAKE_VERSION;
    }
    LOG("LoadLibraryA(%s) -> fail\n", n); last_error = 126; return 0;
}
static uint32_t k_GetProcAddress(void) {
    char n[128] = ""; if (ARG(1) >= 0x10000) gstr_get(ARG(1), n, sizeof n);
    if (ARG(0) == FAKE_VERSION) {
        static uint32_t m[3];
        if (!strcmp(n, "GetFileVersionInfoSizeA")) return m[0] ? m[0] : (m[0] = w32_register(n, v_GetFileVersionInfoSizeA, 8));
        if (!strcmp(n, "GetFileVersionInfoA")) return m[1] ? m[1] : (m[1] = w32_register(n, v_GetFileVersionInfoA, 16));
        if (!strcmp(n, "VerQueryValueA")) return m[2] ? m[2] : (m[2] = w32_register(n, v_VerQueryValueA, 16));
        return 0;
    }
    if (ARG(0) == FAKE_QTIM32 || ARG(0) == FAKE_CMGR32) {
        uint32_t f = qt_proc(ARG(0) == FAKE_CMGR32, n);
        LOG("GetProcAddress(%x, %s) -> %x\n", ARG(0), n, f);
        return f;
    }
    LOG("GetProcAddress(%x, %s) -> 0\n", ARG(0), n);
    return 0;
}
static uint32_t k_OutputDebugStringA(void) { char s[512]; gstr_get(ARG(0), s, sizeof s); LOG("debug: %s\n", s); return 0; }
static uint32_t k_WinExec(void) { char s[512]; gstr_get(ARG(0), s, sizeof s); fprintf(stderr, "WinExec(%s) ignored\n", s); return 2; }
static void put_systime(uint32_t p, struct tm *t, int ms) {
    WR16(p, t->tm_year + 1900); WR16(p + 2, t->tm_mon + 1); WR16(p + 4, t->tm_wday); WR16(p + 6, t->tm_mday);
    WR16(p + 8, t->tm_hour); WR16(p + 10, t->tm_min); WR16(p + 12, t->tm_sec); WR16(p + 14, ms);
}
static void now_tm(struct tm *t, int *ms, int local) {
    SDL_Time ns = 0; SDL_GetCurrentTime(&ns);
    time_t s = (time_t)(ns / 1000000000);
#ifdef _WIN32
    if (local) localtime_s(t, &s); else gmtime_s(t, &s);
#else
    if (local) localtime_r(&s, t); else gmtime_r(&s, t);
#endif
    *ms = (int)((ns / 1000000) % 1000);
}
static uint32_t k_GetLocalTime(void) { struct tm t; int ms; now_tm(&t, &ms, 1); put_systime(ARG(0), &t, ms); return 0; }
static uint32_t k_GetTimeZoneInformation(void) { memset(GP(ARG(0)), 0, 172); WR32(ARG(0), (uint32_t)-540); return 0; }
static uint32_t k_GetTickCount(void) { return w32_tick_ms(); }
static uint32_t k_Sleep(void) {
    uint32_t ms = ARG(0);
    if (thr_current() == 0) { w32_pump_events(); video_maybe_present(); }
    thr_sleep(ms ? ms : 1);
    return 0;
}

// ---------------------------------------------------------------- synchronisation objects
// Critical sections: owner thread id + recursion count in the guest structure (+8 / +12).
static uint32_t k_InitializeCriticalSection(void) { memset(GP(ARG(0)), 0, 24); return 0; }
static uint32_t k_EnterCriticalSection(void) {
    uint32_t cs = ARG(0), me = thr_current_id();
    for (;;) {
        uint32_t own = RD32(cs + 12);
        if (!own || own == me) { WR32(cs + 12, me); WR32(cs + 8, RD32(cs + 8) + 1); return 0; }
        thr_sleep(1);
    }
}
static uint32_t k_LeaveCriticalSection(void) {
    uint32_t cs = ARG(0);
    uint32_t n = RD32(cs + 8); if (n) n--;
    WR32(cs + 8, n); if (!n) WR32(cs + 12, 0);
    return 0;
}
struct sem { int used; int count, max; };
static struct sem SEM[64];
#define SEM_BASE 0x2e000000u
static uint32_t k_CreateSemaphoreA(void) {
    for (int k = 0; k < 64; k++) if (!SEM[k].used) { SEM[k] = (struct sem){ 1, (int)ARG(1), (int)ARG(2) }; return SEM_BASE + k; }
    return 0;
}
static uint32_t k_ReleaseSemaphore(void) {
    uint32_t k = ARG(0) - SEM_BASE; if (k >= 64 || !SEM[k].used) return 0;
    if (ARG(2)) WR32(ARG(2), (uint32_t)SEM[k].count);
    SEM[k].count += (int)ARG(1); if (SEM[k].count > SEM[k].max) SEM[k].count = SEM[k].max;
    return 1;
}
static uint32_t k_WaitForSingleObject(void) {
    uint32_t h = ARG(0), ms = ARG(1);
    uint32_t t0 = w32_tick_ms();
    for (;;) {
        if (h - SEM_BASE < 64) { struct sem *s = &SEM[h - SEM_BASE]; if (s->count > 0) { s->count--; return 0; } }
        else if (thr_is_thread_handle(h)) { if (thr_finished(h)) return 0; }
        else return 0;
        if (ms != 0xffffffffu && w32_tick_ms() - t0 >= ms) return 0x102;   // WAIT_TIMEOUT
        if (ms == 0) return 0x102;
        thr_sleep(1);
    }
}
static uint32_t k_CreateThread(void) {
    uint32_t tid = 0;
    if (getenv("PERSONA_NO_THREADS")) { LOG("CreateThread refused (PERSONA_NO_THREADS)\n"); return 0; }
    uint32_t h = thr_create(ARG(2), ARG(3), &tid);
    if (ARG(5)) WR32(ARG(5), tid);
    LOG("CreateThread(%08x, %08x) -> %x\n", ARG(2), ARG(3), h);
    return h;
}
static uint32_t k_CloseHandle(void) {
    uint32_t h = ARG(0);
    FILE *f = fget(h); if (f) { fclose(f); fh[h - FH_BASE] = 0; return 1; }
    if (h - SEM_BASE < 64) SEM[h - SEM_BASE].used = 0;
    return 1;
}

const struct api API_KERNEL32[] = {
    A(FreeResource, k_FreeResource, 4), A(LockResource, k_LockResource, 4), A(LoadResource, k_LoadResource, 8),
    A(OutputDebugStringA, k_OutputDebugStringA, 4), A(SetThreadPriority, k_one, 8), A(Sleep, k_Sleep, 4),
    A(SizeofResource, k_SizeofResource, 8), A(WinExec, k_WinExec, 8), A(_lopen, k__lopen, 8), A(VirtualQuery, k_VirtualQuery, 12),
    A(GetSystemInfo, k_GetSystemInfo, 4), A(SetErrorMode, k_SetErrorMode, 4), A(GetPrivateProfileStringA, k_GetPrivateProfileStringA, 24),
    A(GetPrivateProfileIntA, k_GetPrivateProfileIntA, 16), A(EnterCriticalSection, k_EnterCriticalSection, 4),
    A(LeaveCriticalSection, k_LeaveCriticalSection, 4), A(MultiByteToWideChar, k_MultiByteToWideChar, 24),
    A(DeleteCriticalSection, k_one, 4), A(InitializeCriticalSection, k_InitializeCriticalSection, 4),
    A(CreateSemaphoreA, k_CreateSemaphoreA, 16), A(CreateThread, k_CreateThread, 24), A(ExitThread, k_ExitThread, 4),
    A(CloseHandle, k_CloseHandle, 4), A(ReleaseSemaphore, k_ReleaseSemaphore, 12), A(WaitForSingleObject, k_WaitForSingleObject, 8),
    A(IsBadReadPtr, k_IsBad, 8), A(WriteProfileStringA, k_WriteProfileStringA, 12), A(LoadLibraryA, k_LoadLibraryA, 4),
    A(FreeLibrary, k_one, 4), A(GlobalFree, k_GlobalFree, 4), A(GlobalReAlloc, k_GlobalReAlloc, 12), A(GlobalAlloc, k_GlobalAlloc, 8),
    A(GetModuleHandleA, k_GetModuleHandleA, 4), A(GlobalCompact, k_GlobalCompact, 4), A(GlobalMemoryStatus, k_GlobalMemoryStatus, 4),
    A(GetProfileStringA, k_GetProfileStringA, 20), A(GetProfileIntA, k_GetProfileIntA, 12), A(GetDateFormatA, k_GetDateFormatA, 24),
    A(CompareStringA, k_CompareStringA, 24), A(GetWindowsDirectoryA, k_GetWindowsDirectoryA, 8), A(GetLogicalDrives, k_GetLogicalDrives, 0),
    A(GetDriveTypeA, k_GetDriveTypeA, 4), A(_lwrite, k__lwrite, 12), A(_lread, k__lread, 12), A(_lcreat, k__lcreat, 8),
    A(_lclose, k__lclose, 4), A(OpenFile, k_OpenFile, 12), A(_llseek, k__llseek, 12), A(GetProcAddress, k_GetProcAddress, 8),
    A(IsDBCSLeadByte, k_IsDBCSLeadByte, 4), A(GlobalUnlock, k_GlobalUnlock, 4), A(GlobalLock, k_GlobalLock, 4), A(MulDiv, k_MulDiv, 12),
    A(lstrcmpiA, k_lstrcmpiA, 8), A(lstrlenW, k_lstrlenW, 4), A(WideCharToMultiByte, k_WideCharToMultiByte, 32),
    A(GlobalHandle, k_GlobalHandle, 4), A(GlobalSize, k_GlobalSize, 4), A(GetFileAttributesA, k_GetFileAttributesA, 4),
    A(GetDiskFreeSpaceA, k_GetDiskFreeSpaceA, 20), A(MoveFileA, k_MoveFileA, 8), A(DeleteFileA, k_DeleteFileA, 4),
    A(FindNextFileA, k_FindNextFileA, 8), A(FindClose, k_FindClose, 4), A(FindFirstFileA, k_FindFirstFileA, 8),
    A(SetCurrentDirectoryA, k_SetCurrentDirectoryA, 4), A(GetLastError, k_GetLastError, 0), A(FlushFileBuffers, k_FlushFileBuffers, 4),
    A(GetCurrentDirectoryA, k_GetCurrentDirectoryA, 8), A(lstrlenA, k_lstrlenA, 4), A(lstrcpyA, k_lstrcpyA, 8),
    A(GetModuleFileNameA, k_GetModuleFileNameA, 12), A(lstrcatA, k_lstrcatA, 8), A(GlobalDeleteAtom, k_GlobalDeleteAtom, 4),
    A(GetVersion, k_GetVersion, 0), A(GlobalAddAtomA, k_GlobalAddAtomA, 4), A(GetTickCount, k_GetTickCount, 0),
    A(FindResourceA, k_FindResourceA, 12), A(FileTimeToSystemTime, k_FileTimeToSystemTime, 8), A(CreateFileA, k_CreateFileA, 28),
    A(GetFileType, k_GetFileType, 4), A(SetEndOfFile, k_SetEndOfFile, 4), A(ExitProcess, k_ExitProcess, 4),
    A(GetLocalTime, k_GetLocalTime, 4), A(FileTimeToLocalFileTime, k_FileTimeToLocalFileTime, 8), A(IsBadWritePtr, k_IsBad, 8),
    A(VirtualAlloc, k_VirtualAlloc, 16), A(IsBadCodePtr, k_IsBad, 4), A(_hwrite, k__lwrite, 12), A(_hread, k__lread, 12),
    A(VirtualFree, k_VirtualFree, 12), A(GetVersionExA, k_GetVersionExA, 4), A(UnhandledExceptionFilter, k_zero, 4),
    A(ReadFile, k_ReadFile, 20), A(SetFilePointer, k_SetFilePointer, 16), A(WriteFile, k_WriteFile, 20),
    A(GetFullPathNameA, k_GetFullPathNameA, 16), A(GetStartupInfoA, k_GetStartupInfoA, 4), A(GetEnvironmentStrings, k_GetEnvironmentStrings, 0),
    A(SetStdHandle, k_one, 8), A(GetCommandLineA, k_GetCommandLineA, 0), A(SetEnvironmentVariableA, k_one, 8),
    A(RtlUnwind, k_RtlUnwind, 16), A(GetTimeZoneInformation, k_GetTimeZoneInformation, 4), A(RaiseException, k_RaiseException, 16),
    A(GetStdHandle, k_GetStdHandle, 4), A(GetCPInfo, k_GetCPInfo, 8), A(GetACP, k_GetACP, 0), A(GetOEMCP, k_GetOEMCP, 0),
    A(GetStringTypeA, k_GetStringTypeA, 20),
};
const int N_KERNEL32 = sizeof API_KERNEL32 / sizeof API_KERNEL32[0];

// ---------------------------------------------------------------- process setup
static const struct { const char *dll; const struct api *t; const int *n; } MODS[] = {
    { "KERNEL32.DLL", API_KERNEL32, &N_KERNEL32 }, { "USER32.DLL", API_USER32, &N_USER32 },
    { "GDI32.DLL", API_GDI32, &N_GDI32 }, { "WINMM.DLL", API_WINMM, &N_WINMM }, { "*", API_MISC, &N_MISC },
};
void w32_init(int argc, char **argv) {
    (void)argc; (void)argv;
    trace_quiet = getenv("PERSONA_TRACE_QUIET");
    heap_init();
    thr_init();
    nhost = 1;     // keep magic index 0 unused
    int missing = 0;
    for (int k = 0; k < NIMPORTS; k++) {
        const struct api *found = 0;
        for (size_t m = 0; m < sizeof MODS / sizeof MODS[0] && !found; m++) {
            if (strcasecmp(MODS[m].dll, IMPORTS[k].dll) && strcmp(MODS[m].dll, "*")) continue;
            for (int j = 0; j < *MODS[m].n; j++) if (!strcmp(MODS[m].t[j].name, IMPORTS[k].name)) { found = &MODS[m].t[j]; break; }
        }
        uint32_t magic;
        if (found) magic = w32_register(found->name, found->fn, found->argbytes);
        else { magic = w32_register(IMPORTS[k].name, api_missing, 0); missing++; LOG("import %s!%s not implemented\n", IMPORTS[k].dll, IMPORTS[k].name); }
        WR32(IMPORTS[k].iat, magic);
    }
    if (missing) LOG("%d imports unimplemented\n", missing);
    // resource directory of the image
    uint32_t pe = 0x400000u + RD32(0x400000u + 0x3c);
    rsrc_base = 0x400000u + RD32(pe + 0x18 + 0x60 + 2 * 8);
    // TIB: no SEH frames, stack bounds, self pointer
    WR32(TIB_ADDR + 0x00, 0xffffffffu); WR32(TIB_ADDR + 0x04, STACK_HI); WR32(TIB_ADDR + 0x08, STACK_LO);
    WR32(TIB_ADDR + 0x18, TIB_ADDR); WR32(TIB_ADDR + 0x24, 0x1001); WR32(TIB_ADDR + 0x30, TIB_ADDR + 0x800);
    FS_BASE = TIB_ADDR;
    gstr_put(HOSTSTR, "D:\\DC\\PS.EXE");
    gstr_put(HOSTSTR + 0x100, "PATH=C:\\WINDOWS"); WR8(HOSTSTR + 0x110, 0);
}
