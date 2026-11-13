#!/bin/sh
# usage: tools/run_headless.sh SECONDS [extra env...]  -> build/run.log, build/shots/sNNNN.ppm (tenths of a second)
secs=${1:-15}; shift
rm -rf build/shots build/testsave; mkdir -p build/shots
env PERSONA_SAVE=build/testsave PERSONA_HEADLESS=1 PERSONA_SHOT=build/shots/s%04d.ppm "$@" ./persona > build/run.log 2>&1 &
pid=$!
( sleep "$secs"; kill -9 $pid 2>/dev/null ) &
wait $pid 2>/dev/null
echo "exit $?"
