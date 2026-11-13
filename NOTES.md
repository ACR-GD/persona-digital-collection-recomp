# Working notes

## The disc (`Megami Ibunroku Persona Digital Collection.iso`, ISO 9660 "psdc", 1998, ASCII / Digital Famitsu)
* `PELSONA.exe`: Delphi autorun menu (QuickTime installer / start / accessories). Not needed.
* `dc/PS.exe`: **Macromedia Director 5.0J projector** (MSVC, SmartHeap allocator, MacroMix mixer in two
  extra code sections). Its PE overlay ("PJ95") holds the projector resources (Lingo keyword list,
  Mac-style resources), `xobglu32.dll` (16-bit XObject glue, unused) and the start movie `start.dir`
  (sets the search path, then `go "top" of movie "opening"`).
* `dc/*.dxr`: protected Director 5 movies (little-endian RIFX "XFIR"/MV93), one per attraction, plus
  `chara.cxt` (shared cast: the 63 `click` sounds). `dc/mov/*.mov`: QuickTime movies (opening,
  karaoke, VTR collection).
* `acce/`: accessories (screensavers, calendar, wallpaper changer, WAV library). The movies stream
  their BGM from `acce/wav/bgm/*.wav` with Lingo `sound playFile`.
* `dc/Lingo.ini`, `dc/PS.INI` (absent: every setting is a default).

| movie | content | bitmaps / with text (OCR) |
|---|---|---|
| opening | title, opening movie | 7 / 3 |
| contents | St. Hermelin festival hub, attraction menu | 42 / 25 |
| tarot | Nanjo & Elly's tarot reading | 229 / 183 |
| wpmark | Mark's wallpapers | 51 / 37 |
| karaoke | Tadashi & Tamaki's karaoke (QuickTime) | 68 / 57 |
| info | principal's info room (CD data tree) | 70 / 43 |
| stress | Ms. Natsumi's stress check | 40 / 30 |
| pcheck | Igor's Persona check | 146 / 69 |
| leader | Philemon's leader test | 48 / 34 |
| puzzle | Maki's puzzle | 249 / 104 |
| magic | Reiji & Brown's magic quiz | 58 / 36 |
| hockey | Toro's turtle hockey | 85 / 36 |
| race | Ms. Saeko's floor-mopping race | 82 / 45 |
| shoot | vice principal's shooting gallery | 47 / 25 |
| photo, mdroom, devils, yamaoka, clothes | hidden contents | 142, 45, 401, 28, 369 |
| END | staff roll | 23 / 21 |

## Text
* There are **no text cast members** (STXT) and no Japanese in the Lingo: every line of Japanese is
  pre-rendered into 8-bit bitmaps (dialogue panels, quiz questions, buttons, labels).
* Bitmaps: `CASt` type 1, D5 specific data = pitch(+flags) / rect / reg point / depth / palette;
  pixels in the owned `BITD` chunk, PackBits-style RLE (a payload of exactly pitch*height is raw).
  8-bit pixels index the movie's `CLUT` (Mac order: 0 = white, 255 = black).
* Fonts used in the art: bold Mincho (dialogue), Gothic (buttons/labels).

## Translation pipeline
1. `tools/export_bitmaps.py data/dc work/png` -> PNGs + contact sheets; `tools/ocr_bitmaps.py` -> tesseract
   jpn transcription in `work/ocr/` (names often misread; every line is checked against the image).
2. `translation/en/<movie>.json`: per cast member, `ja` (transcript) + `en`, or explicit `ops`.
3. `tools/apply_translation.py data translation/en data_en`: erases the Japanese (bright low-saturation
   pixels minus long frame lines, 2-px halo, refilled from a per-row/per-dither-phase mode of the
   panel background), renders English (Georgia Bold for dialogue, Arial Bold for labels), maps the
   changed pixels back to the movie palette, and appends new `BITD` chunks to a copy of the movie
   (`tools/dxrpatch.py`: mmap entries are repointed; old data stays). Previews in `work/preview/`.
4. The runtime reads `data_en/` before `data/` (overlay), so `data/` stays a pristine copy of the CD.
* Hover sprites that sit on top of a panel (e.g. はい/いいえ) take their background from the erased
  panel (`bg_from`).

## Translation status
All movies are translated (translation/gen/<movie>.py -> translation/en/<movie>.json -> data_en/dc/<movie>.dxr).
Hidden movies can be tested without unlocking them: `tools/try_movie.sh MOVIE SECS` boots straight into a movie.
Special techniques: karaoke lyrics restored from the backdrop and redrawn with an outline; MD disc labels as
rotated handwriting with ink-coloured erase (discs use background-transparent ink, so overlapping copies of a
label must not be duplicated); Yamaoka's LED panels rebuilt on their 8-px grid with crisp pixel text.

## Translation notes
* Names: PSP-era (Revelations: Persona) names, no honorifics; staff names in Western order.
* Staff roll readings that are best guesses (kanji with several readings): 濱平博 Hiroshi Hamahira, 佐藤玲 Rei Sato,
  三上素能子 Sonoko Mikami, 佐藤倫子 Michiko Sato, 宮内真琴 Makoto Miyauchi, 伊藤浩世 Hiroyo Ito, 小村真由 Mayu Komura,
  草野剛 Tsuyoshi Kusano, 井手隆之 Takayuki Ide, 村田麟太郎 Rintaro Murata, 木野内正義 Masayoshi Kinouchi,
  石塚堅 Ken Ishizuka, 半場友恵 Tomoe Hanba, 倉田雅代 Masayo Kurata, 吉川虎範 Toranori Yoshikawa.
  反谷教頭 = Vice Principal Hanya (reading not checked against the PSP release).
* Photo backgrounds (staff roll) use the `vertical` erase: masked pixels copy the clean strip above the line.
* Mark's gallery (wpmark): INSTALL ran `open "<cd>\\acce\\Wallchg.exe"` (the disc's Windows wallpaper
  changer; WinExec is stubbed). The button is removed: its frame and shadow are refilled with the photo
  texture in the three gallery backgrounds, the label sprite (291) becomes the same patch, and the
  handler (Lscr #188) returns at once (`_disable` in the translation script, `Patcher.disable_handler`).
* Hotspots in tests: the hub QUIT button reacts around pointer (50,440), i.e. butterfly (66,456).

## Recompilation
* `tools/recomp.py` (from the Icarus project): recursive descent from the entry point, relocation-based
  function discovery, all executable sections, jump tables, `lea r,[tbl]; jmp r` stub tables
  (MacroMix), C per function; `tools/extra_entries.txt` for computed targets.
* Runtime (`runtime/`): flat 256 MB guest memory, image at 0x400000.
  * `w32_kernel.c`: files (D: = CD = `data/`, overlay `data_en/`, writes to `save/`), heap, VirtualAlloc
    (64 KB aligned reservations, VirtualQuery: SmartHeap depends on both), resources from the image,
    ini profiles, critical sections, semaphores.
  * `w32_thread.c`: guest threads on host threads behind a FIFO lock; CPU state swapped on hand-off;
    preemption at API calls / loop back-edges when another thread waits (Lingo busy loops).
  * `w32_user.c`: window tree, classes, message queue with WM_PAINT/WM_TIMER synthesis, focus/capture,
    cursors (drawn in software), scripted input for tests.
  * `w32_gdi.c`: 8-bit palettized screen, DDB/DIB surfaces in guest memory, ROP3 blits with
    colour-domain translation, StretchDIBits/SetDIBitsToDevice, regions (rect lists), palettes
    (identity realization, AnimatePalette), pens/brushes/shapes, basic text.
  * `w32_mm.c`: waveOut on SDL audio streams, multimedia timers.
    A buffer is done when the device has consumed it (bytes put minus `SDL_GetAudioStreamQueued`). SDL's
    resampler keeps a few frames (7 bytes at 22 kHz -> 44.1 kHz) until more data arrives, so a residue of
    up to 64 frames counts as played: otherwise MacroMix's last buffer of a sound never completed, the
    channel stayed busy forever, BGM never looped (`soundBusy(2)`) and buttons that wait for their click
    sound (`repeat while soundBusy(1)`) froze the game once the music had ended.
* Director 5/Win is built on Altura's Mac-to-Windows layer: Win32 input becomes Mac EventRecords
  (queue at 0x50f3b8, WaitNextEvent at 0x40db80).

## Verified
* Boot -> opening -> festival hub -> attraction menu; all 12 attractions load, render and start
  their games with sound on (quizzes, puzzle, hockey match, shooting gallery...); translated hub.
* Audio data path: MacroMix opens a 22 kHz 8-bit mono device and streams BGM in real time.

## Clicks and the butterfly cursor (solved: not a bug)
The frame scripts hide the system cursor (`cursor 200`) and draw the butterfly sprite at
`mouseH+16, mouseV+16`, so players aim with the butterfly while the real pointer sits 16 px up and
to the left; sprite hit tests use the real pointer. Scripted test clicks therefore name the
butterfly position, and the runtime subtracts 16 (`PERSONA_CLICK_RAW=1` disables this). The earlier
"clicks fail with audio" observation came from test clicks aimed at the text, outside the hotspot.
Sound is on by default (`PERSONA_NOAUDIO=1` turns it off); with audio, all attractions play.

## QuickTime
Emulated in runtime/w32_qt.c. Director loads `QTIM32.DLL` / `CMGR32.DLL` and calls `_EntryPoint` with a
selector in BX (99 thunks at 0x4f4092; CMGR32 thunks from 0x4f45ca, AX=1). It also reads QTIM32's file
version through VERSION.DLL and refuses QuickTime < 2.0, so VERSION.DLL is emulated too (reports 2.1.2).
Selectors identified from Director's call sites (`tools/qtdis.py ADDR` annotates them):
QT#57/58 init/exit, #11 EnterMovies, #1 ClearMoviesStickyError, #44 OpenMovieFile, #42 NewMovieFromFile,
#2 CloseMovieFile, #7 DisposeMovie, #15 GetMovieBox, #28 GetMovieTimeScale, #18 GetMovieDuration,
#54 GetMovieTime, #13 IsMovieDone, #49 SetMovieActive, #23 GetMovieVolume, #56 NewMovieController,
#94 GetMoviesError, #4 ConvertTimeScale; CM#0 MCActivate, CM#1 MCDoAction (8 play with Fixed rate,
12 go to time, 42 get play rate). Unimplemented selectors are logged with PERSONA_DEBUG_QT=1.
Movies (Cinepak + 8-bit PCM) are parsed directly from the .mov atoms; frames are decoded with the
Icarus Cinepak decoder and drawn in true colour over the presented screen at the controller's window;
audio goes to an SDL stream at normal speed. The movie clock is wall time from the play call.
Burned-in Japanese (phantom.mov narration, remix4.mov Zhuangzi quote) is covered at playback by English
subtitles pre-rendered by tools/make_subs.py from translation/en/movies.json (card areas and fade times
measured from the video) into data_en/dc/mov/<movie>.sub.
QT installer: data/qt/qt32.exe holds the real DLLs as SZDD blobs (tools/szdd.py extracts them) if ever needed.

## Launcher and installed layout
* `runtime/launcher.c` (SDL3 renderer, Crimson Pro UI font, OFL) + `runtime/installer.c`: ISO 9660 reader
  (2048/2352-byte sectors, cue sheets; the disc has mixed-case ISO names with Apple extensions and no
  Joliet) or a folder walk (disc root or its `dc` folder), copied into `<pref>/data`.
* `persona_en.patch` (`tools/make_patch.py`): per overlay file, size + CRC-32 of the original and of the
  result, and the differing byte runs (dxrpatch only appends BITD chunks and rewrites mmap entries,
  so a movie patch is the new chunks plus a few hundred bytes). 12.5 MB for the 20 movies + 2 .sub.
* `<data>_en/patch.id` (size + CRC-32 of the patch it was built from): when a program update ships a
  different patch, it is applied again at startup (installed layout only).
* `PS.exe` is mapped at startup (`install_map_exe`, checked against EXE_SIZE / EXE_CRC32 from
  `gen/image_info.h`); nothing from the disc is compiled into the binary. `PERSONA_IMAGE=gen/image.bin`
  still works for experiments.
* Windows: `runtime/u8path.h` routes fopen/stat/opendir/mkdir/rename through UTF-16 so a user folder with
  non-ASCII characters works; path variables are read with SDL_getenv (UTF-8). The cross build links
  winpthread statically (`-Wl,-Bstatic` last) and needs the 64 MB stack.
* Test hooks: `PERSONA_LAUNCHER_SHOT=file.bmp` + `PERSONA_LAUNCHER_AUTO=play|quit|install`
  (`PERSONA_SOURCE=` the disc), `SDL_VIDEO_DRIVER=offscreen` for containers.

## Debugging
* `tools/run_headless.sh SECS VAR=...` (screens in `build/shots/sNNNN.ppm`, tenths of a second),
  `tools/shots.sh T...`, `tools/smoke.sh` (enters each attraction), `tools/lingo_dis.py movie.dxr`
  (Lingo bytecode with names and literals), `tools/score_dump.py`, `tools/disasm.py`.
* Env: `PERSONA_TRACE=1|2` (API log, 2 adds return values) + `PERSONA_TRACE_QUIET=a,b`,
  `PERSONA_VERBOSE`, `PERSONA_CLICKS="t:x:y[:ms],..."`, `PERSONA_KEYS`, `PERSONA_WATCH=lo-hi`
  (log guest calls in a range with return values), `PERSONA_DUMP=addr:n`, `PERSONA_DEBUG_MSG`,
  `PERSONA_HIDE=substr` (pretend files are missing). `make debug` builds a profiling binary
  (`PERSONA_PROF_AT=secs`, `PERSONA_PROF_LO/HI`, `PERSONA_PROF_ALL`).
