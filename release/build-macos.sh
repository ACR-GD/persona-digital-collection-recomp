#!/bin/sh
# Builds dist/PersonaDC-<version>-macOS.zip: a universal (Apple silicon + Intel) PersonaDC.app with
# SDL3.framework and persona_en.patch inside. Needs gen/ (make gen) and build/persona_en.patch (make patch).
set -eu
cd "$(dirname "$0")/.."
VERSION=$(cat VERSION)
SDL_VER=3.4.20
CACHE=release/cache
APP=dist/macos/PersonaDC.app
mkdir -p "$CACHE" dist
[ -f build/persona_en.patch ] || { echo "build/persona_en.patch missing (make patch)"; exit 1; }

# SDL3.framework (universal) from the official SDL release
if [ ! -d "$CACHE/SDL3.xcframework" ]; then
    curl -fL -o "$CACHE/SDL3-$SDL_VER.dmg" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL3-$SDL_VER.dmg"
    MNT=$(mktemp -d)
    hdiutil attach -nobrowse -readonly -mountpoint "$MNT" "$CACHE/SDL3-$SDL_VER.dmg" >/dev/null
    cp -R "$MNT/SDL3.xcframework" "$CACHE/"
    hdiutil detach "$MNT" >/dev/null
fi
FW="$CACHE/SDL3.xcframework/macos-arm64_x86_64"

make -j8 OPT=-O2 BUILD=build/macos OUT=build/macos/persona \
    ARCH_FLAGS="-arch arm64 -arch x86_64 -mmacosx-version-min=11.0" \
    SDL_CFLAGS="-F$FW" SDL_LIBS="-F$FW -framework SDL3 -Wl,-rpath,@executable_path/../Frameworks"

rm -rf dist/macos && mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
cp build/macos/persona "$APP/Contents/MacOS/PersonaDC"
strip -S "$APP/Contents/MacOS/PersonaDC"
cp -R "$FW/SDL3.framework" "$APP/Contents/Frameworks/"
cp build/persona_en.patch "$APP/Contents/Resources/"   # SDL_GetBasePath() is Contents/Resources in a bundle
[ -f release/icon.icns ] && cp release/icon.icns "$APP/Contents/Resources/"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>PersonaDC</string>
  <key>CFBundleDisplayName</key><string>Persona Digital Collection</string>
  <key>CFBundleIdentifier</key><string>io.github.acr-gd.persona-digital-collection-recomp</string>
  <key>CFBundleExecutable</key><string>PersonaDC</string>
  <key>CFBundleIconFile</key><string>icon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
codesign --force --deep --sign - "$APP"     # ad-hoc signature (required on Apple silicon)
cp release/README-player.txt "dist/macos/README.txt"
rm -f "dist/PersonaDC-$VERSION-macOS.zip"
(cd dist/macos && zip -qry "../PersonaDC-$VERSION-macOS.zip" PersonaDC.app README.txt)
echo "dist/PersonaDC-$VERSION-macOS.zip"
