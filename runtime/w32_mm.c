// WINMM: waveOut on SDL audio streams, multimedia timers, MCI (logged stubs); plus the few
// MSVFW32 / COMDLG32 / SHELL32 / OLE32 imports.
#include "win32.h"
#include <SDL3/SDL.h>
#include <strings.h>

// ---------------------------------------------------------------- multimedia timers
// Callbacks are delivered on a guest thread from rt_irq(); a host thread only raises irq_pending.
struct mmtimer { int used; uint32_t delay, proc, user, flags, id; uint32_t next; };
static struct mmtimer T[16];
static SDL_Mutex *mu;
static int poll_on;
static void lock(void) { SDL_LockMutex(mu); }
static void unlock(void) { SDL_UnlockMutex(mu); }

// ---------------------------------------------------------------- waveOut
struct wq { uint32_t hdr; uint64_t end; };
struct wout {
    int used; SDL_AudioStream *st; uint32_t cb, inst, flags; int bpf;
    uint64_t put;                    // bytes handed to SDL so far
    struct wq q[256]; int qn;
    int paused; uint32_t vol;
};
static struct wout WO[4];
static SDL_AudioDeviceID dev;
// Bytes of this stream the device has consumed. SDL's resampler keeps a few frames of history that it
// only releases when more data arrives, so once the guest stops writing (end of a sound) a small tail
// stays queued for good; without this, the last buffer would never be reported done (WOM_DONE), and
// MacroMix would keep the sound channel busy (no BGM loop, buttons waiting on soundBusy forever).
static uint64_t wave_played(struct wout *w) {
    int queued = SDL_GetAudioStreamQueued(w->st);
    if (queued <= 64 * w->bpf) queued = 0;
    return w->put - (uint64_t)queued;
}
static int wave_pending(void) {
    for (int k = 0; k < 4; k++) if (WO[k].used && WO[k].qn && wave_played(&WO[k]) >= WO[k].q[0].end) return 1;
    return 0;
}
static int SDLCALL poll_thread(void *arg) {
    (void)arg;
    for (;;) {
        SDL_Delay(1);
        uint32_t now = w32_tick_ms();
        lock();
        for (int k = 0; k < 16; k++) if (T[k].used && (int32_t)(now - T[k].next) >= 0) irq_pending = 1;
        if (wave_pending()) irq_pending = 1;
        unlock();
    }
    return 0;
}
static void start_poll(void) { if (!poll_on) { poll_on = 1; SDL_DetachThread(SDL_CreateThread(poll_thread, "mmpoll", 0)); } }

static void wave_done(struct wout *w, int k, uint32_t hdr) {
    WR32(hdr + 16, (RD32(hdr + 16) | 1u) & ~0x10u);       // WHDR_DONE, not INQUEUE
    uint32_t type = w->flags & 0x70000;
    uint32_t hwo = 0x7100u + (uint32_t)k;
    if (type == 0x30000 && w->cb) { uint32_t a[5] = { hwo, 0x3BD /*WOM_DONE*/, w->inst, hdr, 0 }; guest_call(w->cb, 5, a); }
    else if (type == 0x10000 && w->cb) w32_post_message(w->cb, 0x3BD /*MM_WOM_DONE*/, hwo, hdr);
}
void w32_async_callbacks(void) {
    // finished wave buffers
    for (int k = 0; k < 4; k++) {
        struct wout *w = &WO[k];
        while (w->used && w->qn) {
            static int dbg = -1; if (dbg < 0) dbg = getenv("PERSONA_DEBUG_WAVE") != 0;
            if (dbg) { static uint32_t last; uint32_t now = w32_tick_ms(); if (now - last > 1000) { last = now;
                fprintf(stderr, "[%u] wave %d: put %llu queued %d qn %d next end %llu\n", now, k, (unsigned long long)w->put,
                        SDL_GetAudioStreamQueued(w->st), w->qn, (unsigned long long)w->q[0].end); } }
            if (wave_played(w) < w->q[0].end) break;
            uint32_t hdr = w->q[0].hdr;
            memmove(w->q, w->q + 1, sizeof w->q[0] * (size_t)(w->qn - 1)); w->qn--;
            wave_done(w, k, hdr);
        }
    }
    // timers
    uint32_t now = w32_tick_ms();
    for (int k = 0; k < 16; k++) {
        lock();
        int due = T[k].used && (int32_t)(now - T[k].next) >= 0;
        struct mmtimer t = T[k];
        if (due) {
            if (t.flags & 1) { T[k].next += t.delay ? t.delay : 1; if ((int32_t)(now - T[k].next) > 200) T[k].next = now + t.delay; }
            else T[k].used = 0;
        }
        unlock();
        if (!due) continue;
        if (t.flags & 0x10) continue;    // TIME_CALLBACK_EVENT_SET: not used here
        if (getenv("PERSONA_NO_MMTIMER")) continue;
        uint32_t a[5] = { t.id, 0, t.user, 0, 0 };
        guest_call(t.proc, 5, a);
    }
}
static uint32_t m_timeSetEvent(void) {
    lock();
    int k; for (k = 0; k < 16 && T[k].used; k++);
    if (k == 16) { unlock(); return 0; }
    T[k] = (struct mmtimer){ 1, ARG(0), ARG(2), ARG(3), ARG(4), (uint32_t)k + 1, w32_tick_ms() + ARG(0) };
    unlock();
    start_poll();
    LOG("timeSetEvent(%u ms, proc %08x, user %08x, flags %x) -> %d\n", ARG(0), ARG(2), ARG(3), ARG(4), k + 1);
    return (uint32_t)k + 1;
}
static uint32_t m_timeKillEvent(void) {
    uint32_t id = ARG(0);
    lock(); if (id >= 1 && id <= 16) T[id - 1].used = 0; unlock();
    return 0;
}
static uint32_t m_timeGetTime(void) { return w32_tick_ms(); }

static uint32_t m_waveOutGetNumDevs(void) { return dev ? 1 : 0; }
static uint32_t m_waveOutGetDevCapsA(void) {
    uint32_t p = ARG(1); memset(GP(p), 0, ARG(2) < 52 ? ARG(2) : 52);
    WR16(p, 1); WR16(p + 2, 1); WR32(p + 4, 0x400); gstr_put(p + 8, "SDL Audio");
    WR32(p + 40, 0xfff); WR16(p + 44, 2); WR32(p + 48, 0x0c);   // all PCM formats, stereo, volume + lr volume
    return 0;
}
// waveOutOpen(phwo, dev, fmt, cb, inst, flags)
static uint32_t m_waveOutOpen(void) {
    uint32_t fmt = ARG(2), flags = ARG(5);
    int tag = RD16(fmt), ch = RD16(fmt + 2), rate = (int)RD32(fmt + 4), bits = RD16(fmt + 14);
    LOG("waveOutOpen tag %d %d ch %d Hz %d bits flags %x cb %08x\n", tag, ch, rate, bits, flags, ARG(3));
    if (tag != 1 || (bits != 8 && bits != 16) || ch < 1 || ch > 2) return 32;   // WAVERR_BADFORMAT
    if (!dev) return 2;                                                        // MMSYSERR_BADDEVICEID
    if (flags & 1) return 0;                                                   // WAVE_FORMAT_QUERY
    int k; for (k = 0; k < 4 && WO[k].used; k++);
    if (k == 4) return 4;                                                      // MMSYSERR_ALLOCATED
    struct wout *w = &WO[k];
    memset(w, 0, sizeof *w);
    SDL_AudioSpec src = { bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16LE, ch, rate };
    w->st = SDL_CreateAudioStream(&src, 0);
    if (!w->st || !SDL_BindAudioStream(dev, w->st)) { LOG("waveOutOpen: %s\n", SDL_GetError()); return 1; }
    w->used = 1; w->cb = ARG(3); w->inst = ARG(4); w->flags = flags; w->bpf = ch * bits / 8; w->vol = 0xffffffffu;
    if (ARG(0)) WR32(ARG(0), 0x7100u + (uint32_t)k);
    start_poll();
    uint32_t type = flags & 0x70000;
    if (type == 0x30000 && w->cb) { uint32_t a[5] = { 0x7100u + (uint32_t)k, 0x3BB /*WOM_OPEN*/, w->inst, 0, 0 }; guest_call(w->cb, 5, a); }
    else if (type == 0x10000 && w->cb) w32_post_message(w->cb, 0x3BB, 0x7100u + (uint32_t)k, 0);
    return 0;
}
static struct wout *wo(uint32_t h, int *k) { uint32_t i = h - 0x7100u; if (i < 4 && WO[i].used) { if (k) *k = (int)i; return &WO[i]; } return 0; }
static uint32_t m_waveOutPrepareHeader(void) { WR32(ARG(1) + 16, RD32(ARG(1) + 16) | 2u); return 0; }
static uint32_t m_waveOutUnprepareHeader(void) { WR32(ARG(1) + 16, RD32(ARG(1) + 16) & ~2u); return 0; }
static uint32_t m_waveOutWrite(void) {
    int k; struct wout *w = wo(ARG(0), &k); if (!w) return 5;
    uint32_t hdr = ARG(1), data = RD32(hdr), n = RD32(hdr + 4);
    WR32(hdr + 16, (RD32(hdr + 16) | 0x10u) & ~1u);
    if (w->qn == 256) { wave_done(w, k, hdr); return 0; }
    SDL_PutAudioStreamData(w->st, GP(data), (int)n);
    static FILE *dump; static int dump_init;
    if (!dump_init) { dump_init = 1; if (getenv("PERSONA_AUDIO_DUMP")) dump = fopen(getenv("PERSONA_AUDIO_DUMP"), "wb"); }
    if (dump) { fwrite(GP(data), 1, n, dump); fflush(dump); }
    if (getenv("PERSONA_DEBUG_WAVE")) fprintf(stderr, "[%u] waveOutWrite %u bytes, queued %d, pending %d\n", w32_tick_ms(), n, SDL_GetAudioStreamQueued(w->st), w->qn);
    w->put += n;
    if (getenv("PERSONA_WAVE_INSTANT")) { WR32(hdr + 16, (RD32(hdr + 16) | 1u) & ~0x10u); return 0; }
    w->q[w->qn++] = (struct wq){ hdr, w->put };
    return 0;
}
static uint32_t m_waveOutReset(void) {
    int k; struct wout *w = wo(ARG(0), &k); if (!w) return 5;
    SDL_ClearAudioStream(w->st);
    int n = w->qn; struct wq q[256]; memcpy(q, w->q, sizeof q[0] * (size_t)n);
    w->qn = 0; w->put = 0;
    for (int i = 0; i < n; i++) wave_done(w, k, q[i].hdr);
    return 0;
}
static uint32_t m_waveOutClose(void) {
    int k; struct wout *w = wo(ARG(0), &k); if (!w) return 5;
    if (w->qn) return 33;   // WAVERR_STILLPLAYING
    SDL_DestroyAudioStream(w->st); w->used = 0;
    return 0;
}
static uint32_t m_waveOutPause(void) { struct wout *w = wo(ARG(0), 0); if (w) { SDL_UnbindAudioStream(w->st); w->paused = 1; } return 0; }
static uint32_t m_waveOutRestart(void) { struct wout *w = wo(ARG(0), 0); if (w && w->paused) { SDL_BindAudioStream(dev, w->st); w->paused = 0; } return 0; }
static uint32_t m_waveOutSetVolume(void) {
    struct wout *w = wo(ARG(0), 0); uint32_t v = ARG(1);
    float l = (float)(v & 0xffff) / 65535.0f;
    if (w) { w->vol = v; SDL_SetAudioStreamGain(w->st, l); }
    return 0;
}
static uint32_t m_waveOutGetVolume(void) { struct wout *w = wo(ARG(0), 0); if (ARG(1)) WR32(ARG(1), w ? w->vol : 0xffffffffu); return 0; }

// ---------------------------------------------------------------- MCI (not used for this title's media; logged)
static uint32_t m_mciSendCommandA(void) { LOG("mciSendCommand dev=%u msg=%x flags=%x\n", ARG(0), ARG(1), ARG(2)); return 275; }
static uint32_t m_mciSendStringA(void) {
    char cmd[512]; gstr_get(ARG(0), cmd, sizeof cmd);
    LOG("mciSendString \"%s\"\n", cmd);
    if (ARG(1) && ARG(2)) WR8(ARG(1), 0);
    return 275;
}
static uint32_t m_mciGetErrorStringA(void) { if (ARG(1) && ARG(2)) gstr_put(ARG(1), "MCI error"); return 1; }
static uint32_t m_DefDriverProc(void) { return 0; }

const struct api API_WINMM[] = {
    A(waveOutRestart, m_waveOutRestart, 4), A(waveOutGetDevCapsA, m_waveOutGetDevCapsA, 12), A(timeGetTime, m_timeGetTime, 0),
    A(DefDriverProc, m_DefDriverProc, 20), A(waveOutPrepareHeader, m_waveOutPrepareHeader, 12), A(waveOutOpen, m_waveOutOpen, 24),
    A(waveOutReset, m_waveOutReset, 4), A(waveOutUnprepareHeader, m_waveOutUnprepareHeader, 12), A(waveOutClose, m_waveOutClose, 4),
    A(timeKillEvent, m_timeKillEvent, 4), A(timeSetEvent, m_timeSetEvent, 20), A(waveOutPause, m_waveOutPause, 4),
    A(waveOutSetVolume, m_waveOutSetVolume, 8), A(waveOutWrite, m_waveOutWrite, 12), A(waveOutGetVolume, m_waveOutGetVolume, 8),
    A(mciGetErrorStringA, m_mciGetErrorStringA, 12), A(mciSendStringA, m_mciSendStringA, 16), A(mciSendCommandA, m_mciSendCommandA, 16),
    A(waveOutGetNumDevs, m_waveOutGetNumDevs, 0),
};
const int N_WINMM = sizeof API_WINMM / sizeof API_WINMM[0];

// ---------------------------------------------------------------- misc DLLs
static uint32_t x_zero(void) { return 0; }
static uint32_t x_fail(void) { return 0x80004005u; }
static uint32_t x_CoInitialize(void) { return 0; }
static uint32_t x_FindExecutableA(void) { return 31; }
const struct api API_MISC[] = {
    A(ICClose, x_zero, 4), A(ICGetDisplayFormat, x_zero, 24), { "#2", x_zero, 0 },
    A(CommDlgExtendedError, x_zero, 0), A(PrintDlgA, x_zero, 4), A(GetSaveFileNameA, x_zero, 4), A(GetOpenFileNameA, x_zero, 4),
    A(FindExecutableA, x_FindExecutableA, 12), A(CoInitialize, x_CoInitialize, 4), A(CoUninitialize, x_zero, 0), A(CoCreateInstance, x_fail, 20),
};
const int N_MISC = sizeof API_MISC / sizeof API_MISC[0];

// ---------------------------------------------------------------- device
void audio_init(void) {
    mu = SDL_CreateMutex();
    if (getenv("PERSONA_NOAUDIO") && atoi(getenv("PERSONA_NOAUDIO"))) return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { fprintf(stderr, "audio: %s\n", SDL_GetError()); return; }
    SDL_AudioSpec spec = { SDL_AUDIO_S16LE, 2, 44100 };
    dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!dev) fprintf(stderr, "audio device: %s\n", SDL_GetError());
}
SDL_AudioDeviceID audio_device(void) { return dev; }
void audio_shutdown(void) { if (dev) SDL_CloseAudioDevice(dev); dev = 0; }

// ---------------------------------------------------------------- debugging MacroMix
void dbg_mm_status(void) {
    static int on = -1; if (on < 0) on = getenv("PERSONA_DEBUG_MM") != 0;
    if (!on) return;
    uint32_t o = RD32(ESP + 4), h = RD32(ESP + 8);
    static uint32_t last, lasth; uint32_t now = w32_tick_ms(); if (now - last < 500 && h == lasth) return; last = now; lasth = h;
    int nslot = RD16(o + 0x77a), pend = 0;
    for (int b = 0; b < nslot; b++) for (int d = 0; d < 0x24; d++) {
        uint32_t base = o + 0x77c + (uint32_t)(b * 0x26e);   // stride computed like the guest: ((b + b*18) * 9 - b) = 170b... see below
        (void)base;
    }
    // slot stride: eax = b; ebp = 9b; eax = b + 2*9b = 19b; eax = 9*19b = 171b; eax -= b -> 170b bytes
    for (int b = 0; b < nslot; b++) for (int d = 0; d < 0x24; d++) if (RD32(o + 0x77c + 170u * b + 8 + 4u * d) == h) pend = 1;
    fprintf(stderr, "[%u] mm_status(handle %08x): nslot %d pending %d bufs:", now, h, nslot, pend);
    int nb = RD16(o + 0x2c); int nch = RD16(o + 0x77a);
    for (int k = 0; k < nb; k++) {
        uint32_t e = o + 0x30 + 116u * k;
        int hit = 0;
        for (int c = 0; c < nch; c++) if (RD32(e + 4u * c + 0x54) && RD32(e + 4u * c + 0x34) == h) hit = 1;
        fprintf(stderr, " [st %u fl %x hit %d]", RD32(e), RD32(e + 0x14), hit);
    }
    fprintf(stderr, "\n");
}
