// Game data installer used by the launcher.
// * Source: an ISO 9660 disc image (.iso 2048-byte sectors, .bin/.cue raw 2352-byte sectors) or a
//   folder holding the disc contents. Everything is copied into the data folder.
// * English: persona_en.patch (tools/make_patch.py) holds the byte runs of the translated movies that
//   differ from the disc, plus the subtitle files; it is applied into the overlay folder.
// * PS.exe is never compiled into the program: it is mapped from the player's copy at startup.
// All file access goes through SDL_IOStream / SDL filesystem calls (UTF-8 paths on every platform).
#include "installer.h"
#include "../gen/image_info.h"
#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(struct install_progress *p, const char *fmt, const char *arg) { snprintf(p->err, sizeof p->err, fmt, arg); }
static void status(struct install_progress *p, float frac, const char *msg) { p->frac = frac; snprintf(p->msg, sizeof p->msg, "%s", msg); }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int ends_with(const char *s, const char *ext) { size_t a = strlen(s), b = strlen(ext); return a >= b && !SDL_strcasecmp(s + a - b, ext); }

static uint32_t crc_tab[256];
static uint32_t crc32_update(uint32_t c, const uint8_t *b, size_t n) {
    if (!crc_tab[1]) for (uint32_t k = 0; k < 256; k++) { uint32_t x = k; for (int j = 0; j < 8; j++) x = x & 1 ? 0xedb88320u ^ x >> 1 : x >> 1; crc_tab[k] = x; }
    c = ~c;
    for (size_t k = 0; k < n; k++) c = crc_tab[(c ^ b[k]) & 255] ^ c >> 8;
    return ~c;
}
static int is_dir(const char *p) { SDL_PathInfo i; return SDL_GetPathInfo(p, &i) && i.type == SDL_PATHTYPE_DIRECTORY; }
static int is_file(const char *p) { SDL_PathInfo i; return SDL_GetPathInfo(p, &i) && i.type == SDL_PATHTYPE_FILE; }
static int64_t file_size(const char *p) { SDL_PathInfo i; return SDL_GetPathInfo(p, &i) && i.type == SDL_PATHTYPE_FILE ? (int64_t)i.size : -1; }
static void join(char *out, size_t n, const char *dir, const char *rel) {
    size_t l = strlen(dir);
    snprintf(out, n, "%s%s%s", dir, l && (dir[l - 1] == '/' || dir[l - 1] == '\\') ? "" : "/", rel);
}
static int mkdirs_for(const char *file) {      // create the parent folders of a file path
    char d[1200]; snprintf(d, sizeof d, "%s", file);
    char *s = strrchr(d, '/'), *b = strrchr(d, '\\'); if (b > s) s = b;
    if (!s) return 1;
    *s = 0;
    return is_dir(d) || SDL_CreateDirectory(d);
}
static uint8_t *read_all(const char *path, size_t *n) {
    size_t sz = 0; void *b = SDL_LoadFile(path, &sz);
    if (n) *n = sz;
    return b;
}
static int write_all(const char *path, const void *b, size_t n) {
    if (!mkdirs_for(path)) return 0;
    return SDL_SaveFile(path, b, n);
}

// ---------------------------------------------------------------- PS.exe -> guest memory
int install_map_exe(const char *exe, uint8_t *mem, uint32_t mem_size, char *err, size_t errn) {
    size_t sz; uint8_t *b = read_all(exe, &sz);
    if (!b) { snprintf(err, errn, "cannot read %s", exe); return 0; }
    if (sz != EXE_SIZE || crc32_update(0, b, sz) != EXE_CRC32) {
        snprintf(err, errn, "%s is not the PS.exe this build supports (size %zu, expected %d): "
                 "the game needs the original 1998 Japanese disc", exe, sz, EXE_SIZE);
        SDL_free(b); return 0;
    }
    uint32_t pe = le32(b + 0x3c);
    unsigned nsec = b[pe + 6] | b[pe + 7] << 8, opt = b[pe + 20] | b[pe + 21] << 8;
    uint32_t base = le32(b + pe + 24 + 28), size = le32(b + pe + 24 + 56), hdrs = le32(b + pe + 24 + 60);
    if (base != IMAGE_BASE || size != IMAGE_SIZE || (uint64_t)base + size > mem_size) { snprintf(err, errn, "unexpected PE layout in %s", exe); SDL_free(b); return 0; }
    uint8_t *m = mem + base;
    memset(m, 0, size);
    memcpy(m, b, hdrs < sz ? hdrs : sz);
    for (unsigned k = 0; k < nsec; k++) {
        const uint8_t *s = b + pe + 24 + opt + 40 * k;
        uint32_t va = le32(s + 12), vsz = le32(s + 8), rawsz = le32(s + 16), rawp = le32(s + 20);
        if (vsz && rawsz > vsz) rawsz = vsz;
        if (va >= size) continue;
        if (va + rawsz > size) rawsz = size - va;
        if (rawp + rawsz <= sz) memcpy(m + va, b + rawp, rawsz);
    }
    SDL_free(b);
    return 1;
}

// ---------------------------------------------------------------- disc images
struct disc { SDL_IOStream *f; int raw; long start; };
static void disc_close(struct disc *d) { if (d->f) SDL_CloseIO(d->f); memset(d, 0, sizeof *d); }
static int disc_open(struct disc *d, const char *path, char *err, size_t errn) {
    memset(d, 0, sizeof *d);
    char bin[1200] = "";
    if (ends_with(path, ".cue")) {
        size_t n; char *c = (char *)read_all(path, &n);
        if (!c) { snprintf(err, errn, "cannot open %s", path); return 0; }
        char dir[1200]; snprintf(dir, sizeof dir, "%s", path);
        char *sl = strrchr(dir, '/'), *bs = strrchr(dir, '\\'); if (bs > sl) sl = bs;
        if (sl) sl[1] = 0; else dir[0] = 0;
        d->raw = 1;
        for (char *line = strtok(c, "\r\n"); line; line = strtok(0, "\r\n")) {
            while (isspace((unsigned char)*line)) line++;
            if (!SDL_strncasecmp(line, "FILE", 4) && !bin[0]) {
                char *q1 = strchr(line, '"'), *q2 = q1 ? strchr(q1 + 1, '"') : 0;
                if (q1 && q2) { *q2 = 0; snprintf(bin, sizeof bin, "%s%s", dir, q1 + 1); }
                else { char name[512]; if (sscanf(line + 4, "%511s", name) == 1) snprintf(bin, sizeof bin, "%s%s", dir, name); }
            } else if (!SDL_strncasecmp(line, "TRACK", 5)) {
                if (SDL_strcasestr(line, "MODE1/2048")) d->raw = 0;
            }
        }
        SDL_free(c);
        if (!bin[0]) { snprintf(err, errn, "%s is not a usable cue sheet", path); return 0; }
    } else {
        snprintf(bin, sizeof bin, "%s", path);
        int64_t sz = file_size(path);
        d->raw = !ends_with(path, ".iso") && sz > 0 && sz % 2352 == 0 && sz % 2048 != 0;
        if (!ends_with(path, ".iso") && sz > 0 && sz % 2352 == 0 && sz % 2048 == 0) {
            // ambiguous size: look for the volume descriptor
            SDL_IOStream *f = SDL_IOFromFile(path, "rb"); uint8_t b[8] = { 0 };
            if (f) { SDL_SeekIO(f, 16 * 2352 + 16 + 1, SDL_IO_SEEK_SET); SDL_ReadIO(f, b, 5); SDL_CloseIO(f); }
            d->raw = !memcmp(b, "CD001", 5);
        }
    }
    d->f = SDL_IOFromFile(bin, "rb");
    if (!d->f) { snprintf(err, errn, "cannot open the disc image %s", bin); return 0; }
    return 1;
}
static int read_sectors(struct disc *d, uint32_t lba, uint32_t n, uint8_t *out) {
    if (!d->raw) {
        if (SDL_SeekIO(d->f, ((int64_t)d->start + lba) * 2048, SDL_IO_SEEK_SET) < 0) return 0;
        return SDL_ReadIO(d->f, out, (size_t)n * 2048) == (size_t)n * 2048;
    }
    for (uint32_t k = 0; k < n; k++) {
        if (SDL_SeekIO(d->f, ((int64_t)d->start + lba + k) * 2352 + 16, SDL_IO_SEEK_SET) < 0) return 0;
        if (SDL_ReadIO(d->f, out + 2048 * k, 2048) != 2048) return 0;
    }
    return 1;
}

// ---------------------------------------------------------------- file lists (disc or folder)
struct srcfile { char path[512]; uint32_t lba; uint64_t size; };
struct srclist { struct srcfile *f; int n, cap; };
static void list_add(struct srclist *l, const char *path, uint32_t lba, uint64_t size) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 128; l->f = realloc(l->f, sizeof *l->f * l->cap); }
    snprintf(l->f[l->n].path, sizeof l->f[0].path, "%s", path);
    l->f[l->n].lba = lba; l->f[l->n].size = size; l->n++;
}
static int iso_walk(struct disc *d, uint32_t lba, uint32_t size, const char *prefix, struct srclist *out, int depth) {
    if (depth > 8 || size > (4u << 20)) return 0;
    uint32_t ns = (size + 2047) / 2048;
    uint8_t *buf = malloc((size_t)ns * 2048);
    if (!read_sectors(d, lba, ns, buf)) { free(buf); return 0; }
    for (uint32_t p = 0; p < size;) {
        uint8_t len = buf[p];
        if (!len) { p = (p / 2048 + 1) * 2048; continue; }
        if (p + 33 > size) break;
        uint8_t nl = buf[p + 32]; const char *nm = (const char *)buf + p + 33;
        if (!(nl == 1 && (nm[0] == 0 || nm[0] == 1))) {
            char name[256]; int k = 0;
            for (; k < nl && k < 255 && nm[k] != ';'; k++) name[k] = nm[k];
            name[k] = 0; if (k && name[k - 1] == '.') name[k - 1] = 0;
            char path[512]; snprintf(path, sizeof path, "%s%s", prefix, name);
            uint32_t elba = le32(buf + p + 2), esz = le32(buf + p + 10);
            if (buf[p + 25] & 2) { char pre[520]; snprintf(pre, sizeof pre, "%s/", path); iso_walk(d, elba, esz, pre, out, depth + 1); }
            else list_add(out, path, elba, esz);
        }
        p += len;
    }
    free(buf);
    return 1;
}
struct walk { const char *root; struct srclist *out; int depth; };
static SDL_EnumerationResult SDLCALL folder_cb(void *ud, const char *dirname, const char *fname) {
    struct walk *w = ud;
    if (fname[0] == '.') return SDL_ENUM_CONTINUE;
    char full[1200]; snprintf(full, sizeof full, "%s%s", dirname, fname);
    SDL_PathInfo info; if (!SDL_GetPathInfo(full, &info)) return SDL_ENUM_CONTINUE;
    const char *rel = full + strlen(w->root);
    while (*rel == '/' || *rel == '\\') rel++;
    if (info.type == SDL_PATHTYPE_DIRECTORY && w->depth < 8) {
        w->depth++; SDL_EnumerateDirectory(full, folder_cb, w); w->depth--;
    } else if (info.type == SDL_PATHTYPE_FILE) {
        char r[512]; snprintf(r, sizeof r, "%s", rel);
        for (char *c = r; *c; c++) if (*c == '\\') *c = '/';
        list_add(w->out, r, 0, info.size);
    }
    return SDL_ENUM_CONTINUE;
}
static struct srcfile *find(struct srclist *l, const char *name) {
    for (int k = 0; k < l->n; k++) if (!SDL_strcasecmp(l->f[k].path, name)) return &l->f[k];
    return 0;
}

// ---------------------------------------------------------------- English patch
struct patchbuf { const uint8_t *p, *end; };
static int pb_need(struct patchbuf *b, size_t n) { return (size_t)(b->end - b->p) >= n; }
static uint32_t pb32(struct patchbuf *b) { uint32_t v = le32(b->p); b->p += 4; return v; }
static int apply_patch(const char *patch, const char *data_dir, const char *overlay_dir, struct install_progress *pr, float f0, float f1, char *err, size_t errn) {
    size_t n; uint8_t *pt = read_all(patch, &n);
    if (!pt) { snprintf(err, errn, "cannot read the English patch %s", patch); return 0; }
    struct patchbuf b = { pt, pt + n };
    int ok = 0;
    if (!pb_need(&b, 12) || memcmp(b.p, "PDCPAT1", 8)) { snprintf(err, errn, "%s is not a patch file", patch); goto out; }
    b.p += 8;
    uint32_t nf = pb32(&b);
    for (uint32_t k = 0; k < nf; k++) {
        if (!pb_need(&b, 2)) goto bad;
        unsigned pl = b.p[0] | b.p[1] << 8; b.p += 2;
        if (!pb_need(&b, pl + 20) || pl >= 400) goto bad;
        char rel[400]; memcpy(rel, b.p, pl); rel[pl] = 0; b.p += pl;
        if (strstr(rel, "..")) goto bad;
        uint32_t ssz = pb32(&b), scrc = pb32(&b), dsz = pb32(&b), dcrc = pb32(&b), nr = pb32(&b);
        if (pr) { char m[500]; snprintf(m, sizeof m, "Applying the English patch: %s", rel); status(pr, f0 + (f1 - f0) * k / nf, m); }
        uint8_t *dst = calloc(1, dsz ? dsz : 1);
        if (ssz) {
            char sp[1200]; join(sp, sizeof sp, data_dir, rel);
            size_t sn; uint8_t *src = read_all(sp, &sn);
            if (!src || sn != ssz || crc32_update(0, src, sn) != scrc) {
                snprintf(err, errn, "%s differs from the disc the patch was made for", rel);
                SDL_free(src); free(dst); goto out;
            }
            memcpy(dst, src, ssz < dsz ? ssz : dsz);
            SDL_free(src);
        }
        for (uint32_t r = 0; r < nr; r++) {
            if (!pb_need(&b, 8)) { free(dst); goto bad; }
            uint32_t off = pb32(&b), len = pb32(&b);
            if (!pb_need(&b, len) || (uint64_t)off + len > dsz) { free(dst); goto bad; }
            memcpy(dst + off, b.p, len); b.p += len;
        }
        if (crc32_update(0, dst, dsz) != dcrc) { snprintf(err, errn, "patched %s does not match", rel); free(dst); goto out; }
        char dp[1200]; join(dp, sizeof dp, overlay_dir, rel);
        if (!write_all(dp, dst, dsz)) { snprintf(err, errn, "cannot write %s", dp); free(dst); goto out; }
        free(dst);
    }
    ok = 1; goto out;
bad:
    snprintf(err, errn, "the English patch is damaged");
out:
    SDL_free(pt);
    return ok;
}
int install_patch(const char *patch, const char *data_dir, const char *overlay_dir, char *err, size_t errn) {
    return apply_patch(patch, data_dir, overlay_dir, 0, 0, 1, err, errn);
}

// ---------------------------------------------------------------- install
int install_is_complete(const char *data_dir) {
    char p[1200];
    join(p, sizeof p, data_dir, "dc/PS.exe"); if (file_size(p) != EXE_SIZE) return 0;
    join(p, sizeof p, data_dir, "dc/contents.dxr"); if (!is_file(p)) return 0;
    join(p, sizeof p, data_dir, "dc/END.dxr"); return is_file(p);
}
int install_has_english(const char *overlay_dir) {
    char p[1200]; join(p, sizeof p, overlay_dir, "dc/contents.dxr"); return is_file(p);
}

static int copy_files(struct disc *d, const char *root, struct srclist *l, const char *data_dir, struct install_progress *p) {
    uint64_t total = 0, done = 0;
    for (int k = 0; k < l->n; k++) total += l->f[k].size;
    size_t cap = 1 << 20; uint8_t *buf = malloc(cap);
    for (int k = 0; k < l->n; k++) {
        struct srcfile *f = &l->f[k];
        char m[600]; snprintf(m, sizeof m, "Copying %s", f->path);
        status(p, 0.9f * (float)done / (float)(total ? total : 1), m);
        char dp[1200]; join(dp, sizeof dp, data_dir, f->path);
        if (!mkdirs_for(dp)) { fail(p, "cannot create the folder for %s", dp); free(buf); return 0; }
        SDL_IOStream *o = SDL_IOFromFile(dp, "wb");
        if (!o) { fail(p, "cannot write %s", dp); free(buf); return 0; }
        SDL_IOStream *in = 0;
        if (!d) { char sp[1200]; join(sp, sizeof sp, root, f->path); in = SDL_IOFromFile(sp, "rb"); if (!in) { SDL_CloseIO(o); fail(p, "cannot read %s", sp); free(buf); return 0; } }
        uint64_t left = f->size; uint32_t lba = f->lba; int ok = 1;
        while (left && ok) {
            size_t chunk = left < cap ? (size_t)left : cap;
            if (d) {
                uint32_t ns = (uint32_t)((chunk + 2047) / 2048);
                ok = read_sectors(d, lba, ns, buf); lba += ns;
            } else ok = SDL_ReadIO(in, buf, chunk) == chunk;
            if (ok) ok = SDL_WriteIO(o, buf, chunk) == chunk;
            left -= chunk; done += chunk;
            if (!(lba & 255)) status(p, 0.9f * (float)done / (float)(total ? total : 1), m);
        }
        if (in) SDL_CloseIO(in);
        if (!SDL_CloseIO(o)) ok = 0;
        if (!ok) { fail(p, "copy failed: %s (disc image damaged or disk full?)", f->path); free(buf); return 0; }
    }
    free(buf);
    return 1;
}

int install_game(const char *src, const char *data_dir, const char *overlay_dir, const char *patch, struct install_progress *p) {
    struct disc d = { 0 }; struct srclist l = { 0 }; char root[1200] = "";
    int ok = 0, image = !is_dir(src);
    status(p, 0, "Reading the disc...");
    if (image) {
        if (!disc_open(&d, src, p->err, sizeof p->err)) goto done;
        uint8_t pvd[2048];
        if (!read_sectors(&d, 16, 1, pvd) || memcmp(pvd + 1, "CD001", 5)) { fail(p, "%s is not an ISO 9660 disc image", src); goto done; }
        iso_walk(&d, le32(pvd + 156 + 2), le32(pvd + 156 + 10), "", &l, 0);
    } else {
        // the disc root, or its dc folder
        char t[1200];
        snprintf(root, sizeof root, "%s", src);
        join(t, sizeof t, root, "dc/PS.exe");
        if (!is_file(t)) {
            join(t, sizeof t, root, "PS.exe");
            char *s = strrchr(root, '/'), *b = strrchr(root, '\\'); if (b > s) s = b;
            size_t rl = strlen(root);
            if (rl && (root[rl - 1] == '/' || root[rl - 1] == '\\')) { root[rl - 1] = 0; s = strrchr(root, '/'); b = strrchr(root, '\\'); if (b > s) s = b; }
            if (is_file(t) && s && !SDL_strcasecmp(s + 1, "dc")) *s = 0;
            else { fail(p, "%s does not hold the Persona Digital Collection (no dc/PS.exe in it)", src); goto done; }
        }
        struct walk w = { root, &l, 0 };
        SDL_EnumerateDirectory(root, folder_cb, &w);
    }
    struct srcfile *exe = find(&l, "dc/PS.exe");
    if (!exe || !find(&l, "dc/contents.dxr")) { fail(p, "%s does not hold the Persona Digital Collection (no dc/PS.exe on it)", src); goto done; }
    if (exe->size != EXE_SIZE) {
        snprintf(p->err, sizeof p->err, "This disc has a different PS.exe (size %llu, expected %d): the game needs the original 1998 Japanese disc.",
                 (unsigned long long)exe->size, EXE_SIZE);
        goto done;
    }
    if (!copy_files(image ? &d : 0, root, &l, data_dir, p)) goto done;
    {   // verify the program before going further
        char ep[1200]; join(ep, sizeof ep, data_dir, "dc/PS.exe");
        size_t n; uint8_t *b = read_all(ep, &n);
        int good = b && n == EXE_SIZE && crc32_update(0, b, n) == EXE_CRC32;
        SDL_free(b);
        if (!good) { fail(p, "%s", "This disc has a different version of PS.exe than the one this build supports."); goto done; }
    }
    if (patch && is_file(patch)) {
        char err[512] = "";
        if (!apply_patch(patch, data_dir, overlay_dir, p, 0.9f, 1.0f, err, sizeof err)) {
            snprintf(p->warn, sizeof p->warn, "Game installed, but the English patch failed: %s", err);
        }
    } else snprintf(p->warn, sizeof p->warn, "Game installed (Japanese only: persona_en.patch was not found next to the program).");
    status(p, 1, "Done.");
    ok = 1;
done:
    disc_close(&d); free(l.f);
    p->ok = ok; p->done = 1;
    return ok;
}
