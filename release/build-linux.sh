#!/bin/sh
# Builds dist/PersonaDC-<version>-Linux-x64.tar.gz (PersonaDC + lib/libSDL3.so.0 + persona_en.patch)
# in Docker. Needs Docker, gen/ (make gen) and build/persona_en.patch (make patch).
set -eu
cd "$(dirname "$0")/.."
VERSION=$(cat VERSION)
mkdir -p dist
[ -f build/persona_en.patch ] || { echo "build/persona_en.patch missing (make patch)"; exit 1; }
docker build -q --platform linux/amd64 -t persona-linux -f release/Dockerfile.linux release >/dev/null
docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" -v "$PWD:/src" -w /src persona-linux sh -c "
    set -e
    make -j8 OPT=-O2 CC=clang BUILD=build/linux OUT=build/linux/PersonaDC \
        SDL_CFLAGS='-I/opt/sdl3/include' SDL_LIBS='-L/opt/sdl3/lib -lSDL3' \
        LDFLAGS='-Wl,-rpath,\\\$\$ORIGIN/lib'
    rm -rf dist/linux && mkdir -p dist/linux/PersonaDC/lib
    strip -o dist/linux/PersonaDC/PersonaDC build/linux/PersonaDC
    cp -L /opt/sdl3/lib/libSDL3.so.0 dist/linux/PersonaDC/lib/
    cp build/persona_en.patch dist/linux/PersonaDC/
    cp release/README-player.txt dist/linux/PersonaDC/README.txt
    tar -czf dist/PersonaDC-$VERSION-Linux-x64.tar.gz -C dist/linux PersonaDC
"
echo "dist/PersonaDC-$VERSION-Linux-x64.tar.gz"
