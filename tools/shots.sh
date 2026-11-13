#!/bin/sh
# tools/shots.sh T1 T2 ... : convert build/shots/sNNNN.ppm (tenths of a second) nearest to each time (s) into build/shot_T.png
for t in "$@"; do
  want=$(python3 -c "print(int(float('$t')*10))")
  f=$(ls build/shots/*.ppm | python3 -c "
import sys,os
w=$want; fs=[l.strip() for l in sys.stdin]
best=min(fs,key=lambda f:abs(int(os.path.basename(f)[1:5])-w)); print(best)")
  build/venv/bin/python -I -c "from PIL import Image; Image.open('$f').save('build/shot_$t.png')" && echo "build/shot_$t.png <- $f"
done
