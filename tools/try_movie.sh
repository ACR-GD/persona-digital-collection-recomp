#!/bin/sh
# tools/try_movie.sh MOVIE SECS [env...]: boot straight into a translated movie (it stands in for opening.dxr
# in a throwaway overlay; the start movie jumps to label "top"). Screens in build/shots.
mv=$1; secs=$2; shift 2
rm -rf build/ov_try; mkdir -p build/ov_try/dc
cp -R data_en/dc/. build/ov_try/dc/
cp data_en/dc/$mv.dxr build/ov_try/dc/opening.dxr
tools/run_headless.sh "$secs" PERSONA_OVERLAY=build/ov_try SDL_AUDIO_DRIVER=dummy "$@"
