// UTF-8 file paths on Windows: the C runtime's fopen/stat/opendir take the ANSI code page, so a data
// folder under a non-ASCII user name would not open. These wrappers convert to UTF-16 (runtime/u8path.c).
// Elsewhere paths are already UTF-8 and nothing changes.
#pragma once
#ifdef _WIN32
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
typedef struct u8_DIR u8_DIR;
struct u8_dirent { char d_name[780]; };
FILE *u8_fopen(const char *path, const char *mode);
int u8_stat(const char *path, struct stat *st);
int u8_remove(const char *path);
int u8_mkdir(const char *path);
int u8_rename(const char *a, const char *b);
u8_DIR *u8_opendir(const char *path);
struct u8_dirent *u8_readdir(u8_DIR *d);
int u8_closedir(u8_DIR *d);
#define fopen(p, m) u8_fopen(p, m)
#define stat(p, s) u8_stat(p, s)
#define remove(p) u8_remove(p)
#define rename(a, b) u8_rename(a, b)
#define DIR u8_DIR
#define dirent u8_dirent
#define opendir(p) u8_opendir(p)
#define readdir(d) u8_readdir(d)
#define closedir(d) u8_closedir(d)
#endif
