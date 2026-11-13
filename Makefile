# Megami Ibunroku Persona Digital Collection: static recompilation of the Director 5.0J projector.
CC      ?= clang
OPT     ?= -O2
SDL_CFLAGS ?= $(shell pkg-config --cflags sdl3 2>/dev/null || echo -I/opt/homebrew/opt/sdl3/include)
SDL_LIBS   ?= $(shell pkg-config --libs sdl3 2>/dev/null || echo -L/opt/homebrew/opt/sdl3/lib -lSDL3)
CFLAGS  = $(OPT) -g -std=gnu11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -D_GNU_SOURCE -Wno-unused-label -Wno-unused-variable \
          -Wno-parentheses-equality -Wno-unused-value -Wno-misleading-indentation -Wno-unused-but-set-variable \
          -fno-strict-aliasing -fwrapv $(SDL_CFLAGS) $(EXTRA_CFLAGS)
LDLIBS  = $(SDL_LIBS) -lm -lpthread
ifeq ($(OS),Windows_NT)
LDLIBS += -Wl,--stack,67108864
endif

GEN_SRC = $(wildcard gen/*.c)
RT_SRC  = $(wildcard runtime/*.c)
BUILD   ?= build
OUT     ?= persona
OBJ     = $(patsubst %.c,$(BUILD)/%.o,$(GEN_SRC) $(RT_SRC))
PY      ?= build/venv/bin/python
ISO     ?= Megami Ibunroku Persona Digital Collection.iso

$(OUT): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDLIBS)

$(BUILD)/gen/%.o: gen/%.c runtime/rt.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/runtime/%.o: runtime/%.c runtime/rt.h runtime/win32.h runtime/gdi.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

gen: tools/recomp.py tools/pe.py
	$(PY) -I tools/recomp.py data/dc/PS.exe gen

# data/: the CD-ROM contents (read-only game data)
data:
	mkdir -p data && bsdtar -xf "$(ISO)" -C data && chmod -R u+w data

debug:
	$(MAKE) BUILD=build/dbg OUT=build/dbg/persona_dbg OPT=-O1 EXTRA_CFLAGS=-DRT_DEBUG
.PHONY: debug

clean:
	rm -rf $(BUILD)/gen $(BUILD)/runtime $(OUT)
.PHONY: gen clean data
