#!/bin/sh
# Builds dist/PersonaDC-<version>-Windows-x64.zip (PersonaDC.exe + SDL3.dll + persona_en.patch) by
# cross-compiling in Docker. Needs Docker, gen/ (make gen) and build/persona_en.patch (make patch).
set -eu
cd "$(dirname "$0")/.."
VERSION=$(cat VERSION)
SDL_VER=3.4.20
CACHE=release/cache
mkdir -p "$CACHE" dist
[ -f build/persona_en.patch ] || { echo "build/persona_en.patch missing (make patch)"; exit 1; }
if [ ! -d "$CACHE/SDL3-$SDL_VER" ]; then
    curl -fL -o "$CACHE/SDL3-devel-$SDL_VER-mingw.tar.gz" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL3-devel-$SDL_VER-mingw.tar.gz"
    tar -xzf "$CACHE/SDL3-devel-$SDL_VER-mingw.tar.gz" -C "$CACHE"
fi
docker build -q -t persona-mingw -f release/Dockerfile.windows release >/dev/null
SDL=/src/$CACHE/SDL3-$SDL_VER/x86_64-w64-mingw32
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/src" -w /src persona-mingw sh -c "
    set -e
    make -j8 OPT=-O2 CC=x86_64-w64-mingw32-gcc-posix BUILD=build/windows OUT=build/windows/PersonaDC.exe \
        SDL_CFLAGS='-I$SDL/include' \
        SDL_LIBS='-L$SDL/lib -lSDL3' PTHREAD='-Wl,-Bstatic -lpthread -Wl,-Bdynamic' \
        EXTRA_LDLIBS='-static-libgcc -mwindows -Wl,--stack,67108864 -Wl,-Bstatic'
    rm -rf dist/windows && mkdir -p dist/windows
    x86_64-w64-mingw32-strip -o dist/windows/PersonaDC.exe build/windows/PersonaDC.exe
    cp $SDL/bin/SDL3.dll build/persona_en.patch dist/windows/
    sed 's/\$/\r/' release/README-player.txt > dist/windows/README.txt
    rm -f dist/PersonaDC-$VERSION-Windows-x64.zip
    cd dist/windows && zip -q ../PersonaDC-$VERSION-Windows-x64.zip PersonaDC.exe SDL3.dll persona_en.patch README.txt
"
echo "dist/PersonaDC-$VERSION-Windows-x64.zip"
