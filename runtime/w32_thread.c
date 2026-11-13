// Guest threads. Each guest thread runs on its own host thread, but only one of them executes
// guest code at a time: they share one fair lock (the "GIL") and the CPU state globals (registers,
// lazy flags, x87, shadow stack, FS base) are saved and restored when the lock changes hands.
// The lock is released only around blocking calls (Sleep, waits, an empty message queue), which is
// where the projector's own threads (file I/O worker, MacroMix service thread) give way anyway.
#include "win32.h"
#include <SDL3/SDL.h>

struct cpu {
    uint32_t R[8], DF, fl_op, fl_sz, fl_a, fl_b, fl_res, fl_c, fl_expl;
    double FPR[8]; uint32_t top, cw, cc;
    uint32_t *SH; int SHP; uint32_t stk_lo, stk_hi, fs;
};
struct gthread {
    int used, finished;
    uint32_t id, handle, start, param, exit_code;
    struct cpu cpu;
    SDL_Thread *host;
};
#define NT 32
static struct gthread T[NT];
static SDL_Mutex *gil_mu; static SDL_Condition *gil_cv;
static uint64_t ticket_next, ticket_serving;
static _Thread_local int my_tid;      // index into T of the calling host thread
int thr_current(void) { return my_tid; }
uint32_t thr_current_id(void) { return T[my_tid].id; }

static void save_cpu(struct cpu *c) {
    memcpy(c->R, R, sizeof R); c->DF = DF;
    c->fl_op = fl_op; c->fl_sz = fl_sz; c->fl_a = fl_a; c->fl_b = fl_b; c->fl_res = fl_res; c->fl_c = fl_c; c->fl_expl = fl_expl;
    memcpy(c->FPR, FPR, sizeof FPR); c->top = FPU_TOP; c->cw = FPU_CW; c->cc = FPU_CC;
    c->SH = SH; c->SHP = SHP; c->stk_lo = STK_LO; c->stk_hi = STK_HI; c->fs = FS_BASE;
}
static void load_cpu(const struct cpu *c) {
    memcpy(R, c->R, sizeof R); DF = c->DF;
    fl_op = c->fl_op; fl_sz = c->fl_sz; fl_a = c->fl_a; fl_b = c->fl_b; fl_res = c->fl_res; fl_c = c->fl_c; fl_expl = c->fl_expl;
    memcpy(FPR, c->FPR, sizeof FPR); FPU_TOP = c->top; FPU_CW = c->cw; FPU_CC = c->cc;
    SH = c->SH; SHP = c->SHP; STK_LO = c->stk_lo; STK_HI = c->stk_hi; FS_BASE = c->fs;
}

// ---------------------------------------------------------------- the lock (FIFO tickets)
static void gil_acquire(void) {
    SDL_LockMutex(gil_mu);
    uint64_t me = ticket_next++;
    while (ticket_serving != me) SDL_WaitCondition(gil_cv, gil_mu);
    SDL_UnlockMutex(gil_mu);
    load_cpu(&T[my_tid].cpu);
}
static void gil_release(void) {
    save_cpu(&T[my_tid].cpu);
    SDL_LockMutex(gil_mu);
    ticket_serving++;
    SDL_BroadcastCondition(gil_cv);
    SDL_UnlockMutex(gil_mu);
}
static int others_waiting(void) {
    SDL_LockMutex(gil_mu);
    int w = ticket_next != ticket_serving + 1;
    SDL_UnlockMutex(gil_mu);
    return w;
}
void thr_block_begin(void) { gil_release(); }
void thr_block_end(void) { gil_acquire(); }
void thr_sleep(uint32_t ms) { gil_release(); SDL_Delay(ms); gil_acquire(); }
// let other runnable guest threads in, without sleeping if nobody waits
void thr_yield(void) { if (others_waiting()) { gil_release(); gil_acquire(); } }

void thr_init(void) {
    gil_mu = SDL_CreateMutex(); gil_cv = SDL_CreateCondition();
    T[0].used = 1; T[0].id = 0x1001; T[0].handle = 0x2fff0000u;
    my_tid = 0;
    ticket_next = 1; ticket_serving = 0;      // the main thread holds the lock from the start
}

// ---------------------------------------------------------------- creation / exit
#define THREAD_STACK (512u << 10)
static int SDLCALL thread_main(void *arg) {
    int k = (int)(intptr_t)arg;
    my_tid = k;
    gil_acquire();
    uint32_t a[1] = { T[k].param };
    LOG("thread %x start %08x(%08x)\n", T[k].id, T[k].start, T[k].param);
    T[k].exit_code = guest_call(T[k].start, 1, a);
    T[k].finished = 1;
    LOG("thread %x finished\n", T[k].id);
    gil_release();
    return 0;
}
uint32_t thr_create(uint32_t start, uint32_t param, uint32_t *tid) {
    int k; for (k = 1; k < NT && T[k].used; k++);
    if (k == NT) return 0;
    struct gthread *t = &T[k];
    memset(t, 0, sizeof *t);
    t->used = 1; t->id = 0x1001 + k; t->handle = 0x2fff0000u + k; t->start = start; t->param = param;
    uint32_t stk = gmem_alloc(THREAD_STACK);
    uint32_t tib = gmem_alloc(0x1000);
    WR32(tib + 0x00, 0xffffffffu); WR32(tib + 0x04, stk + THREAD_STACK); WR32(tib + 0x08, stk);
    WR32(tib + 0x18, tib); WR32(tib + 0x24, t->id);
    struct cpu *c = &t->cpu;
    memset(c, 0, sizeof *c);
    c->R[4] = stk + THREAD_STACK - 64; c->fl_op = F_LOGIC; c->fl_sz = 32; c->cw = 0x37f;
    c->SH = calloc(65536, 4); c->SHP = 1; c->stk_lo = stk; c->stk_hi = stk + THREAD_STACK; c->fs = tib;
    if (tid) *tid = t->id;
    SDL_PropertiesID pr = SDL_CreateProperties();
    SDL_SetPointerProperty(pr, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)thread_main);
    SDL_SetStringProperty(pr, SDL_PROP_THREAD_CREATE_NAME_STRING, "guest");
    SDL_SetPointerProperty(pr, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, (void *)(intptr_t)k);
    SDL_SetNumberProperty(pr, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 16 << 20);
    thr_start_preemption();
    t->host = SDL_CreateThreadWithProperties(pr);
    SDL_DestroyProperties(pr);
    SDL_DetachThread(t->host);
    return t->handle;
}
void thr_exit(uint32_t code) {
    int k = my_tid;
    if (k == 0) { w32_shutdown(); exit((int)code); }
    T[k].exit_code = code; T[k].finished = 1;
    LOG("ExitThread %x\n", T[k].id);
    gil_release();
    // the host thread parks forever: its C stack still holds the guest call frames
    for (;;) SDL_Delay(1000000);
}
int thr_is_thread_handle(uint32_t h) { return h > 0x2fff0000u && h < 0x2fff0000u + NT && T[h - 0x2fff0000u].used; }
int thr_finished(uint32_t h) { return T[h - 0x2fff0000u].finished; }
void thr_set_main_cpu_from_globals(void) { save_cpu(&T[0].cpu); }

// ---------------------------------------------------------------- preemption
// A guest thread that never blocks (e.g. Lingo `repeat while soundBusy(1)`) would starve the
// others. A monitor raises irq_pending when someone has waited for the lock for a while; the
// holder then yields at its next safe point (API call or loop back-edge, see rt_irq).
static volatile uint64_t wait_since;
void thr_maybe_yield(void) {
    static _Thread_local uint64_t last;
    uint64_t now = SDL_GetTicksNS();
    if (now - last < 2000000ull) return;
    last = now;
    if (others_waiting()) { gil_release(); gil_acquire(); }
}
static int SDLCALL preempt_monitor(void *arg) {
    (void)arg;
    for (;;) {
        SDL_Delay(2);
        if (others_waiting()) irq_pending = 1;
    }
    return 0;
}
void thr_start_preemption(void) {
    static int on; if (on) return; on = 1;
    SDL_DetachThread(SDL_CreateThread(preempt_monitor, "preempt", 0));
}
