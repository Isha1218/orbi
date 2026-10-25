# Project: Mini Sphere

## Overview
An open-source desk companion modeled on the Las Vegas Sphere (https://github.com/Isha1218/mini-sphere): a minimal glowing face that follows your mouse and shows what your coding agents are doing (thinking, done, error). Software first, in a browser simulator; hardware (Waveshare ESP32-S3-LCD-1.85, no touch: ESP32-S3 + 360×360 round ST77916 LCD + onboard mic) next. Voice input planned (you talk to it; no speaker yet).

## Tech Stack
- **firmware/**: portable C99 renderer and protocol parser. Compiled to wasm (Emscripten) for the sim now, and for the ESP32 later. No platform deps, so it carries over to hardware.
- **daemon/**: Python (`websockets`, `pyobjc-framework-Quartz` for the cursor on macOS).
- **sim/**: one HTML page that runs the wasm renderer on a canvas.
- **hardware/**: CadQuery script for the 3D-printed parts (`pip install cadquery`).

## Architecture
```
Claude Code hooks ─► minisphere send <state> ─┐
cursor (30 Hz) ───────────────────────────┼─► daemon hub (ws://localhost:8765) ─► sim (wasm)   [later: USB serial ─► ESP32]
```
- Protocol: one JSON object per line. `{"t":"look","x":-1..1,"y":-1..1}`, `{"t":"state","v":"idle|thinking|done|error"}`.
- The sphere does all drawing; the computer only sends messages. Frames are never streamed.
- Hardware port = new display/serial glue around the same `eye.c` + `protocol.c`, plus a serial transport in the daemon.

## Key Files
- `firmware/eye.c`: the face. Glowing white eyes on a black glass ball that turn toward the cursor; idle = smiling eyes, thinking = uneven lids scanning, done = squeezed-shut ^ ^, error = slanted lids + shake. Expressions morph; blinks squash; glances around when the cursor is still
- `firmware/protocol.c`: line parser → `eye_look` / `eye_set_state`
- `sim/index.html`, `sim/build.sh`: browser simulator and wasm build
- `daemon/sphere/hub.py`: WebSocket relay, cursor tracking, serves the sim on :8000
- `daemon/sphere/cli.py`: `minisphere run`, `minisphere send <state>`
- `hardware/shell.py` → `bowl.stl/.step`, `ring.stl/.step`: 60mm ball = black printed bowl + bought solid clear acrylic 60mm dome glued on top (it acts as a lens: the screen looks ~1.5× bigger and fills the glass half); sits tilted in a small ring. Board outline/parts come from Waveshare's STEP model of the 1.85" board. Board drops into a pocket (screen flush with the rim) and rests on 3 posts under its M2 standoffs; USB-C plugs in sideways through a window below the rim; a 1.5mm hole lets sound reach the mic. Face must be rotated in firmware to match how the ball sits

## How to Run
```sh
brew install emscripten
python3 -m venv .venv && .venv/bin/pip install -e .
python hardware/shell.py     # regenerate printable parts (needs cadquery)
sim/build.sh                 # rebuild after editing firmware/
.venv/bin/minisphere run         # then open http://localhost:8000
.venv/bin/minisphere send done   # idle | thinking | done | error
```

## Status
- Stage 1 (done): sim + daemon + mouse tracking + `minisphere send`.
- Hardware: switched to the 1.85" board (48mm round, 8.5mm deep) in a 60mm ball (solid acrylic dome on top, like the commercial "ESP32 ball"). Shell CAD redone for it (fit-checked against the board model). Board not bought yet.
- Next: firmware for the board + serial transport in the daemon; voice (mic audio → computer for speech-to-text); later Claude Code hooks.
