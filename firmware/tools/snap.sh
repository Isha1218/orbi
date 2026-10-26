#!/bin/sh
# Build the native harness and run a script: firmware/tools/snap.sh script.txt [out_dir]
# Frames named in "snap" lines are written there as PPM and converted to PNG.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
script="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
out="${2:-.}"
mkdir -p "$out"
src="$here/.."
cc -std=c99 -O2 -Wall -Wextra -o "$out/snap" "$here/snap.c" "$src/eye.c" "$src/protocol.c" "$src/text.c" \
  "$src/sprites.c" "$src/scenes.c" "$src/weather.c" "$src/cards.c" -lm
cd "$out"
./snap < "$script"
for f in *.ppm; do
  [ -e "$f" ] || continue
  sips -s format png "$f" --out "${f%.ppm}.png" > /dev/null
  rm "$f"
done
