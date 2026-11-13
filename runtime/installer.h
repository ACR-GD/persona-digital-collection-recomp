// Game data installer (runtime/installer.c): copies the disc from an image or a folder, applies the
// English patch, and maps the player's PS.exe into guest memory.
#pragma once
#include <stdint.h>
#include <stddef.h>

struct install_progress {
    volatile float frac;     // 0..1
    char msg[256];           // current step
    char err[512];           // set on failure
    char warn[256];          // set on success with caveats (e.g. no English patch)
    volatile int done, ok;
};
// src: a disc image (.iso, .bin, .cue) or a folder (the disc root, a copy of it, or its dc folder).
// The disc is copied into data_dir; when patch is readable, the English overlay is written to overlay_dir.
int install_game(const char *src, const char *data_dir, const char *overlay_dir, const char *patch, struct install_progress *p);
// data_dir holds a copy of the disc with the supported PS.exe
int install_is_complete(const char *data_dir);
// overlay_dir holds the English movies
int install_has_english(const char *overlay_dir);
// Apply the English patch alone (data_dir must be complete). Returns 1 on success.
int install_patch(const char *patch, const char *data_dir, const char *overlay_dir, char *err, size_t errn);
// The overlay was built from this patch file (patch.id matches); 1 when the patch cannot be read.
int install_patch_is_current(const char *patch, const char *overlay_dir);
// Map <data_dir>/dc/PS.exe at its image base in mem (checked against the build). Returns 1 on success.
int install_map_exe(const char *exe, uint8_t *mem, uint32_t mem_size, char *err, size_t errn);
