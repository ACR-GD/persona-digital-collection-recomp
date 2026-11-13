// Win32 high-level emulation shared declarations.
#pragma once
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------- guest memory layout
#define TIB_ADDR    0x00010000u    // main thread information block (FS base)
#define HOSTSTR     0x00011000u    // command line, environment, module name
#define STACK_LO    0x00100000u
#define STACK_HI    0x003F0000u
#define HEAP_LO     0x01000000u
#define HEAP_HI     0x0C000000u
#define VA_LO       0x0C000000u    // VirtualAlloc reservations, 64 KB granularity
#define VA_HI       MEM_SIZE

uint32_t gmem_alloc(uint32_t size);          // zeroed, 16-byte aligned, 0 on failure
void gmem_free(uint32_t addr);
uint32_t gmem_size(uint32_t addr);
uint32_t gmem_realloc(uint32_t addr, uint32_t size);
static inline void *GP(uint32_t a) { return MEM + (a & MEM_MASK); }
void gstr_get(uint32_t a, char *out, int n);
void gstr_put(uint32_t a, const char *s);

// ---------------------------------------------------------------- host functions
// Host implementations of imported functions. They read their arguments with ARG(n) (the return
// address is at ESP), return the EAX value; argbytes says how much the callee pops (stdcall),
// -1 marks cdecl.
#define ARG(n) RD32(ESP + 4 + 4 * (n))
typedef uint32_t (*apifn)(void);
struct api { const char *name; apifn fn; int argbytes; };
uint32_t w32_register(const char *name, apifn fn, int argbytes);   // returns magic address
void w32_call_host(uint32_t magic);
uint32_t w32_make_vtable(const char *iface, const struct api *methods, int n);
uint32_t w32_new_object(uint32_t vtable, uint32_t size);

extern const struct api API_KERNEL32[], API_USER32[], API_GDI32[], API_WINMM[], API_MISC[];
extern const int N_KERNEL32, N_USER32, N_GDI32, N_WINMM, N_MISC;

// ---------------------------------------------------------------- threads (w32_thread.c)
void thr_init(void);
uint32_t thr_create(uint32_t start, uint32_t param, uint32_t *tid);
_Noreturn void thr_exit(uint32_t code);
void thr_block_begin(void);   // release the guest lock around a host wait
void thr_block_end(void);
void thr_sleep(uint32_t ms);
void thr_yield(void);
void thr_maybe_yield(void);
void thr_start_preemption(void);
int thr_current(void);
uint32_t thr_current_id(void);
int thr_is_thread_handle(uint32_t h);
int thr_finished(uint32_t h);

// ---------------------------------------------------------------- subsystems
void w32_init(int argc, char **argv);
void w32_shutdown(void);
void w32_async_callbacks(void);      // deliver due multimedia timer / wave callbacks (from rt_irq)
void w32_pump_events(void);          // SDL events -> window messages / input state
uint32_t w32_tick_ms(void);          // GetTickCount/timeGetTime clock
extern int w32_log_api;              // PERSONA_TRACE=1: log every host call
void w32_post_message(uint32_t hwnd, uint32_t m, uint32_t w, uint32_t l);
uint32_t w32_send_message(uint32_t hwnd, uint32_t m, uint32_t w, uint32_t l);

// resources of the main module (w32_kernel.c)
uint32_t res_find(uint32_t type, uint32_t name, uint32_t *size);   // type/name: id or guest string ptr

// paths: guest "D:\..." (the CD) / "C:\..." / relative -> host files
int host_path(const char *guest, char *out, size_t n, int for_write);
extern char game_dir[1024], save_dir[1024], overlay_dir[1024];
extern char cur_dir[260];

// ---------------------------------------------------------------- display (w32_gdi.c / host.c)
#define SCREEN_W 640
#define SCREEN_H 480
extern uint8_t screen_px[SCREEN_W * SCREEN_H];   // 8-bit system palette indices
extern uint32_t sys_pal[256];                     // 0x00RRGGBB, the hardware palette
extern volatile int screen_dirty;
void gdi_init(void);
void video_init(void);
void video_present_screen(void);     // converts screen_px with sys_pal and shows it
void video_maybe_present(void);      // presents if dirty and the frame interval has passed
void video_shutdown(void);
void video_set_cursor_visible(int vis);
void video_set_cursor(uint32_t hcursor);
extern int mouse_x, mouse_y;
// window geometry for GDI (w32_user.c)
int user_window_origin(uint32_t hwnd, int client, int *x, int *y, int *w, int *h);

// ---------------------------------------------------------------- audio (host.c / w32_mm.c)
void audio_init(void);
void audio_shutdown(void);

// portability (macOS / Linux / Windows-MinGW)
void host_sleep_ms(uint32_t ms);
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) u8_mkdir(p)
#define FSEEK64 _fseeki64
#define FTELL64 _ftelli64
#else
#define MKDIR(p) mkdir((p), 0755)
#define FSEEK64 fseeko
#define FTELL64 ftello
#endif

#define LOG(...) do { if (rt_verbose) fprintf(stderr, __VA_ARGS__); } while (0)
extern int rt_verbose;
#define A(n, f, b) { #n, f, b }
