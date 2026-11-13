#!/bin/sh
# tools/smoke.sh: enter each of the 12 attractions from the hub (in parallel) and screenshot it.
# Output: build/smoke/<n>/run.log and build/smoke/<n>/sNNNN.ppm ; summary of FATALs on stdout.
secs=${SECS:-40}
mkdir -p build/smoke
i=0
for y in 65 160 255; do for x in 145 265 385 505; do
  i=$((i+1)); d=build/smoke/$i; rm -rf $d; mkdir -p $d
  env PERSONA_SAVE=$d/save PERSONA_HEADLESS=1 SDL_AUDIO_DRIVER=dummy PERSONA_SHOT=$d/s%04d.ppm PERSONA_SHOT_MS=1000 \
      PERSONA_CLICKS="6:350:360,9:300:380,14:135:402:300,19:$x:$y:200,${EXTRA_CLICKS:-}" ./persona > $d/run.log 2>&1 &
  echo $! > $d/pid
done; done
sleep $secs
for d in build/smoke/*/; do kill -9 $(cat $d/pid) 2>/dev/null; done
wait 2>/dev/null; sleep 1
for d in build/smoke/*/; do printf "%s: " $d; grep -m1 -E "FATAL|unimplemented host" $d/run.log || echo ok; done
