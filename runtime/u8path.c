// UTF-8 path wrappers for Windows (see u8path.h).
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sys/stat.h>
#include <dirent.h>
typedef struct u8_DIR u8_DIR;
struct u8_dirent { char d_name[780]; };
struct u8_DIR { _WDIR *d; struct u8_dirent e; };
static wchar_t *wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, 0, 0);
    wchar_t *w = malloc(sizeof(wchar_t) * (n > 0 ? n : 1));
    if (n <= 0 || !MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n)) w[0] = 0;
    return w;
}
FILE *u8_fopen(const char *path, const char *mode) {
    wchar_t *p = wide(path), *m = wide(mode); FILE *f = _wfopen(p, m); free(p); free(m); return f;
}
int u8_stat(const char *path, struct stat *st) {
    wchar_t *p = wide(path); struct _stat64 s; int r = _wstat64(p, &s); free(p);
    if (!r) { memset(st, 0, sizeof *st); st->st_mode = s.st_mode; st->st_size = (off_t)s.st_size; st->st_mtime = (time_t)s.st_mtime; }
    return r;
}
int u8_remove(const char *path) { wchar_t *p = wide(path); int r = _wremove(p); free(p); return r; }
int u8_mkdir(const char *path) { wchar_t *p = wide(path); int r = _wmkdir(p); free(p); return r; }
int u8_rename(const char *a, const char *b) {
    wchar_t *x = wide(a), *y = wide(b);
    int r = MoveFileExW(x, y, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;   // POSIX rename replaces the target
    free(x); free(y); return r;
}
u8_DIR *u8_opendir(const char *path) {
    wchar_t *p = wide(path); _WDIR *d = _wopendir(p); free(p);
    if (!d) return 0;
    u8_DIR *r = calloc(1, sizeof *r); r->d = d; return r;
}
struct u8_dirent *u8_readdir(u8_DIR *d) {
    struct _wdirent *e = _wreaddir(d->d);
    if (!e) return 0;
    if (!WideCharToMultiByte(CP_UTF8, 0, e->d_name, -1, d->e.d_name, sizeof d->e.d_name, 0, 0)) d->e.d_name[0] = 0;
    return &d->e;
}
int u8_closedir(u8_DIR *d) { int r = _wclosedir(d->d); free(d); return r; }
#endif
