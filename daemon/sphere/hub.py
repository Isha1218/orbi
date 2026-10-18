"""The daemon: relays protocol messages to every connected sphere and streams the cursor.

Spheres (the browser sim today, USB serial later) and senders (`minisphere send`) all connect
to one WebSocket. Any message from one client goes to all the others.
"""
import asyncio
import functools
import http.server
import json
import threading
from pathlib import Path

from websockets import broadcast
from websockets.asyncio.server import serve

from . import mouse

WS_PORT = 8765
HTTP_PORT = 8000
SIM_DIR = Path(__file__).resolve().parents[2] / "sim"

clients = set()
last_look = None


async def handle(ws):
    clients.add(ws)
    if last_look:
        await ws.send(last_look)
    try:
        async for msg in ws:
            broadcast(clients - {ws}, msg)
    finally:
        clients.discard(ws)


async def track_mouse(hz=30):
    global last_look
    while True:
        pos = mouse.cursor()
        msg = pos and json.dumps({"t": "look", "x": round(pos[0], 3), "y": round(pos[1], 3)})
        if msg and msg != last_look:
            last_look = msg
            broadcast(clients, msg)
        await asyncio.sleep(1 / hz)


def serve_sim():
    class Handler(http.server.SimpleHTTPRequestHandler):
        extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm"}

        def end_headers(self):  # always serve the latest build, never a stale cached one
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

        def log_message(self, *args):
            pass

    httpd = http.server.ThreadingHTTPServer(("localhost", HTTP_PORT), functools.partial(Handler, directory=SIM_DIR))
    threading.Thread(target=httpd.serve_forever, daemon=True).start()


async def run():
    serve_sim()
    print(f"minisphere daemon running. Simulator: http://localhost:{HTTP_PORT}", flush=True)
    async with serve(handle, "localhost", WS_PORT):
        await track_mouse()
