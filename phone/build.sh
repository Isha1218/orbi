#!/bin/sh
# Compile the firmware to wasm for the phone app.
set -e
cd "$(dirname "$0")"
mkdir -p build
emcc ../firmware/eye.c ../firmware/protocol.c -O2 \
  -o build/sphere.js \
  -sMODULARIZE -sEXPORT_NAME=createSphere -sENVIRONMENT=web \
  -sEXPORTED_FUNCTIONS=_eye_init,_eye_update,_eye_render,_protocol_handle_line,_malloc \
  -sEXPORTED_RUNTIME_METHODS=ccall,HEAPU16
echo "built phone/build/sphere.js"
