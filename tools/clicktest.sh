#!/bin/sh
# tools/clicktest.sh "x,y x,y ..." : one run per point (parallel); click the hub prompt at the point 3 times.
# Prints which points reached the attraction menu.
rm -rf build/ct; mkdir -p build/ct; i=0
for p in $1; do i=$((i+1)); x=${p%,*}; y=${p#*,}; d=build/ct/$i; mkdir -p $d; echo "$p" > $d/pt
  env PERSONA_SAVE=$d/save PERSONA_HEADLESS=1 PERSONA_OVERLAY= PERSONA_SHOT=$d/s%04d.ppm PERSONA_SHOT_MS=3000 $EXTRA \
      PERSONA_CLICKS="6:350:360,9:300:380,14:$x:$y:250,17:$x:$y:250,20:$x:$y:250" ./persona > $d/log 2>&1 &
  echo $! > $d/pid; done
sleep ${SECS:-25}; for d in build/ct/*/; do kill -9 $(cat $d/pid) 2>/dev/null; done; sleep 1
build/venv/bin/python -I -c "
import glob
from PIL import Image
for d in sorted(glob.glob('build/' + __import__('os').environ.get('CT','ct') + '/*/'), key=lambda s: int(s.split('/')[-2])):
    f = sorted(glob.glob(d + 's*.ppm'))[-1]
    im = Image.open(f).convert('RGB')
    # attraction menu: dark portrait pixel at (150,60) (the hub shows the white logo there)
    ok = sum(im.getpixel((150, 60))) < 100
    print(open(d + 'pt').read().strip(), 'MENU' if ok else '-')
"
