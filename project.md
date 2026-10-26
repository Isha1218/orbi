# Project: Orbi

## Overview
Open-source desk companion modeled on the Las Vegas Sphere (https://github.com/Isha1218/orbi), built from the "Desk Sphere" design doc. A glowing orb with eyes that follows your cursor, listens when you hold a key, sends your request to an agent (Hermes), and shows the result on its face or in a panel on your screen. v1: a phone propped on the desk is the sphere. v2: Waveshare ESP32-S3-LCD-1.85 (360×360 round ST77916 LCD + mic, no touch) in a 60mm ball.

## Tech Stack
- **firmware/**: portable C99 face renderer + protocol parser. Compiled to wasm for the phone now, native for the ESP32 later. No platform deps, no malloc.
- **phone/**: PWA that runs the wasm firmware. Only device glue: WebSocket, mic → 16 kHz PCM, wake lock.
- **daemon/orbi/**: Python asyncio. `websockets` (HTTPS + WSS, self-signed cert), `faster-whisper` (local STT), `pynput` (hotkey), `pywebview` + `markdown` (panel), `mcp` (display tools), Pillow (avatars), Quartz (cursor, idle time).
- **hardware/**: CadQuery script for the 3D-printed parts.

## Architecture
```
phone (wasm firmware) ◄─ wss://<lan-ip>:8765/ws ─► daemon ─► claude -p (default) | hermes chat -Q -q
   audio PCM up, commands down                      │  ▲
                                                    │  └─ MCP http://127.0.0.1:8766/mcp (face_show, screen_show, face_react)
                                                    └─► screen panel (toast + markdown)
```
- Device protocol v1 is documented in `firmware/protocol.h` (JSON lines, `"t"` = command; binary frames for audio up and avatar images down). Built-in names live in `protocol.h` and `daemon/orbi/library.py`; keep them in sync.
- The sphere does all drawing; the daemon only sends commands. Same firmware on phone and ESP32, so v2 = new glue (LCD, I2S mic, USB serial) + a serial transport in the daemon.
- Daemon: events → `WorldState` → `current_layer` (conversation > content > needs you > news > busy > plugin ambient > idle; plugins can also add content and news) picks the one thing the face shows. Personality picks the idle expression.
- Plugins (built-in + `~/.orbi/plugins/*.py`) load at startup; their tools join the MCP server as `<plugin>_<tool>`.
- Push-to-talk: hold Right Option. News clears by timeout (10 s), no acknowledge input.

## Key Files
- `firmware/eye.c`: the face. 9 expressions, blinks, listening ring, labels (on a soft dark backing); composites scenes/cards per row band (`eye_render_rows`)
- `firmware/weather.c`: rain, storm, snow, sun, cloud: full-orb windows into the weather (no eyes), iris open/close
- `firmware/scenes.c`: scene dispatch + confetti, sparkles, zzz (eyes stay)
- `firmware/cards.c`: animated illustrated glance cards (12 icons, initials, avatar)
- `firmware/sprites.c`, `gfx.h`, `text.c`: row-binned sprite engine, shared math/SDF helpers, bitmap text
- `firmware/protocol.c` / `protocol.h`: protocol parser and spec
- `firmware/font.h`: bitmap font from `firmware/tools/make_font.py`
- `firmware/tools/snap.sh`: native harness → PNG frames; `bench N` prints ms/frame
- `phone/index.html`, `phone/build.sh`: phone app and wasm build
- `daemon/orbi/server.py`: HTTPS/WSS port, QR pairing, starts MCP + Hermes adapter
- `daemon/orbi/daemon.py`: event bus, devices, talking flow, cursor gaze, glances
- `daemon/orbi/state.py`: world state and priority layers
- `daemon/orbi/personality.py`: idle behavior (sleepy at night / when away, greets you back, yawns)
- `daemon/orbi/voice.py`, `hotkey.py`: faster-whisper STT, Right Option push-to-talk
- `daemon/orbi/display.py`, `mcp_server.py`: display tools, face validation → screen fallback, avatars
- `daemon/orbi/claude_code.py`: default harness. `claude -p` on the user's Claude login, Opus, stream-json → `agent.*` events; only WebSearch/WebFetch + Orbi MCP tools, runs in empty `~/.orbi/workspace`
- `daemon/orbi/hermes.py`, `agents.py`: Hermes adapter (`orbi run --harness hermes`) → `agent.*` events; agents + news state
- `daemon/orbi/panel.py` + `panel.html`: screen panel overlay
- `daemon/orbi/plugins.py`: plugin API (`define_plugin`, `ctx`), loader, plugin layers (content / news / ambient)
- `daemon/orbi/plugins_builtin/`: `weather` (Open-Meteo `weather_get_weather`), `email` (`email_show_email`: initials card + summary panel)
- `docs/plugins.md`: plugin author docs
- `hardware/shell.py` → `bowl`/`ring` STL+STEP: black printed bowl + 60mm solid acrylic dome; board in a pocket on 3 posts; USB-C side window; mic hole. Face must be rotated in firmware to match how the ball sits

## How to Run
```sh
brew install emscripten
python3 -m venv .venv && .venv/bin/pip install -e ".[test]"
phone/build.sh               # rebuild after editing firmware/
.venv/bin/orbi run           # scan the QR with your phone, accept the cert warning once, tap to wake
.venv/bin/pytest tests
firmware/tools/snap.sh       # render face PNGs
python tools/panel_demo.py   # preview the screen panel
```
- Your own plugins: drop a `.py` file in `~/.orbi/plugins/` (see `docs/plugins.md`).
- macOS permissions for your terminal: Input Monitoring + Accessibility (push-to-talk).
- Claude Code harness needs `claude` logged in (Pro works); uses your plan's usage limits.
- Hermes (optional): installed at `~/.hermes`; `~/.hermes/config.yaml` has `mcp_servers.orbi.url: http://127.0.0.1:8766/mcp`. Model: free Gemini `gemini-flash-lite-latest` (provider `gemini`, key in `~/.hermes/.env`); title generation off to save quota. Full Flash's free tier (20 req/day) is too small for Hermes (3–10 calls/turn).

## Status
- Phase 1 (phone sphere) + Phase 2 (agents) + Phase 3 (plugin API, weather + email plugins) built; 56 tests pass. Verified with real Claude Code (Opus 5.5): weather → plugin + face_show (~18 s). Verified with real Hermes: weather → face scene (~12 s), regex → screen panel + code card (~13 s). Not yet verified on a real phone (iOS mic over self-signed HTTPS is the risk).
- Graphics redo: weather fills the orb (new `cloud` scene; cloudy/fog → cloud), illustrated animated cards. Perf on M2: idle 1.0 ms, scenes/cards 1.2–3.8 ms/frame worst (storm).
- Next: Phase 4 ESP32 glue (expect roughly 3–10 fps on the S3 for rich scenes without further optimization).
- Open: label font lacks accented letters (é, ü, ñ); doc's "62° · rain 3pm" is 14 chars > 12 limit.
