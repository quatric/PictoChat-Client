#!/usr/bin/env bash
# Pack PictoChat.elf into PictoChat.cia. Needs makerom and bannertool on the PATH.
# Usage: scripts/make_cia.sh [version]   (run from anywhere; output goes to 3ds/)
set -e
cd "$(dirname "$0")/.."
VERSION="${1:-$(cat VERSION 2>/dev/null || echo 1.0.0)}"
IFS=. read -r MAJOR MINOR MICRO <<<"$VERSION"
bannertool makebanner -i meta/banner.png -a meta/audio.wav -o PictoChat.bnr
bannertool makesmdh -s "PictoChat" -l "Chat on PictoChat Online (pict.chat)" -p "quatric" -i icon.png -o PictoChat.icn
makerom -f cia -o PictoChat.cia -elf PictoChat.elf -rsf meta/app.rsf -target t -exefslogo -desc app:4 \
    -icon PictoChat.icn -banner PictoChat.bnr -major "${MAJOR:-1}" -minor "${MINOR:-0}" -micro "${MICRO:-0}"
echo built PictoChat.cia
