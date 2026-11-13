# Megami Ibunroku Persona Digital Collection — English translation & native port

A static recompilation of the 1998 Windows CD-ROM *女神異聞録ペルソナ デジタルコレクション*
(ASCII / Digital Famitsu) for macOS, Linux and Windows, with an English translation of its
pre-rendered text. The collection is built with Macromedia Director 5.0J; this project recompiles
the Director projector (`PS.exe`) to C and runs it on a Win32 emulation layer over SDL3. The
game's own movies and scripts run unmodified; translated movies are read from an overlay folder.

No game data is included. You need your own copy of the disc image. The translation scripts hold
the English text and drawing instructions only; the Japanese source lines are left out.

## Build

Requirements: clang or gcc, make, SDL3, Python 3 (for the generator and translation tools), bsdtar.

```sh
python3 -m venv build/venv && build/venv/bin/pip install pefile capstone==5.0.7 pillow
make data ISO="Megami Ibunroku Persona Digital Collection.iso"   # extracts the disc into data/
make gen                                                         # recompiles data/dc/PS.exe into gen/
make -j                                                          # builds ./persona
for f in translation/gen/*.py; do build/venv/bin/python -I $f; done            # translation scripts -> translation/en
build/venv/bin/python -I tools/apply_translation.py data translation/en data_en   # English overlay
build/venv/bin/python -I tools/make_subs.py data translation/en/movies.json data_en  # movie subtitles (needs ffmpeg)
./persona
```

`data_en/` (translated movies) is used automatically when present; `PERSONA_OVERLAY=` disables it.

## Running

* Window: 640x480 scaled 2x (`PERSONA_SCALE=n`), Alt+Enter toggles fullscreen.
* Sound is on (`PERSONA_NOAUDIO=1` disables it). Aim with the butterfly: it is the game's cursor.
* `PERSONA_SAVE=dir` changes where files written by the game go (default `save/`).

## Status

* Engine: boots, runs the opening, the festival hub and all 12 attractions with sound (rendering,
  palette, cursors, Lingo, mouse, QuickTime movies).
* Translation: complete for all 20 movies (hub, 12 attractions, 5 hidden contents, staff roll; the opening
  needs none). 621 bitmaps redrawn, plus English subtitles over the Japanese text burned into two videos.
* QuickTime: emulated (Cinepak video + PCM audio); the opening, the karaoke and Yamaoka's VTRs play.

See NOTES.md for the technical details and the debugging tools.
