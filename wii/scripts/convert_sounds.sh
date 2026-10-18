#!/bin/sh
# Turn pict.chat's MP3 sound effects (github.com/ayunami2000/ayunpictojava, src/main/resources/www/sounds)
# into the raw PCM the app plays: 16-bit big-endian stereo at 32 kHz.
# Usage: scripts/convert_sounds.sh path/to/sounds
set -e
for f in "$1"/*.mp3; do
    ffmpeg -v error -y -i "$f" -ar 32000 -ac 2 -f s16be "data/$(basename "${f%.mp3}").pcm"
done
