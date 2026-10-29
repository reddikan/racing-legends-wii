#!/bin/sh
# Builds and runs the native test harness (no Wii toolchain needed). Output goes to tools/out/.
#   sh tools/run_tests.sh shots [track]   screenshots of the menus and races
#   sh tools/run_tests.sh bots [track]    bot races at several upgrade levels
#   sh tools/run_tests.sh reel track      a preview video (needs ffmpeg)
set -e
cd "$(dirname "$0")/.."
mkdir -p tools/out/reel
cc -O2 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
   source/r3d.c source/ui.c source/track.c source/scenery.c source/car.c source/fx.c source/race.c \
   source/hud.c source/menu.c source/game.c source/audio.c tools/swr.c tools/test.c -lm -o tools/out/gtest
tools/out/gtest tools/out ${1:-shots} $2
python3 -c "
import glob, os
from PIL import Image
for p in glob.glob('tools/out/*.ppm'):
    Image.open(p).save(p[:-4] + '.png'); os.remove(p)
" 2>/dev/null || echo "(PNG export skipped: needs python3 + Pillow)"
if [ "$1" = "reel" ]; then
    ffmpeg -loglevel error -y -framerate 30 -i tools/out/reel/f%04d.ppm -i tools/out/reel/audio.wav -c:v libx264 -preset veryfast -pix_fmt yuv420p -crf 20 -c:a aac -b:a 160k -shortest tools/out/reel/preview.mp4
    rm -f tools/out/reel/*.ppm
    echo "preview: tools/out/reel/preview.mp4"
fi
