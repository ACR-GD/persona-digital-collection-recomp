#!/bin/sh
# tools/qa_contents.sh MOVIE WAIT [X:Y]: boot straight into MOVIE (translated), wait WAIT seconds (past the end of
# its first BGM track: pass track length + 20), click CONTENTS (butterfly position, default 586:466) and keep running 15 s.
# Writes build/qa/MOVIE/{log,s*.ppm}; prints whether the BGM kept playing and whether the hub came back.
mv=$1; wait=$2; pt=${3:-586:466}; tag=${TAG:-}
d=build/qa/$mv$tag; rm -rf "$d"; mkdir -p "$d/ov/dc" "$d/save"
for f in data_en/dc/*; do ln -s "$PWD/$f" "$d/ov/dc/"; done
rm "$d/ov/dc/opening.dxr"; ln -s "$PWD/data_en/dc/$mv.dxr" "$d/ov/dc/opening.dxr"
env SDL_AUDIO_DRIVER=dummy PERSONA_OVERLAY="$d/ov" PERSONA_SAVE="$d/save" PERSONA_HEADLESS=1 PERSONA_DEBUG_WAVE=1 \
    PERSONA_SHOT="$d/s%04d.ppm" PERSONA_SHOT_MS=5000 PERSONA_CLICKS="$wait:$pt" ./persona > "$d/log" 2>&1 &
pid=$!
sleep $((wait + 15)); kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
build/venv/bin/python -I - "$d" "$mv" "$wait" <<'EOF'
import sys, re, glob
from PIL import Image
d, mv, wait = sys.argv[1], sys.argv[2], int(sys.argv[3])
ticks = [int(m.group(1)) for m in re.finditer(r'^\[(\d+)\] waveOutWrite', open(d + '/log').read(), re.M)]
# the first track is WAIT-20 s long: audio must still be written well after it ended
span = (ticks[-1] - ticks[0]) / 1000 if ticks else 0
shots = sorted(glob.glob(d + '/s*.ppm'))
im = Image.open(shots[-1]).convert('RGB')
im.resize((320, 240)).save(d + '/final.png')
print('%-8s audio for %.0f s (track %d s): %s' % (mv, span, wait - 20, 'loops' if span > wait - 12 else 'STOPPED'))
EOF
