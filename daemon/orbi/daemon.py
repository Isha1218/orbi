"""The brain: turns everything that happens into events, keeps the world state,
and sends the sphere whatever the highest priority layer says."""
import asyncio
import json

from . import mouse, personality, voice
from .state import WorldState, current_layer

CONFUSED_SECONDS = 1.5


class Device:
    """One connected sphere. The transport (WebSocket now, USB serial later) only needs send()."""

    def __init__(self, send):
        self.send = send
        self.hello = None

    async def command(self, cmd):
        await self.send(json.dumps(cmd, ensure_ascii=False))


class Daemon:
    def __init__(self, panel=None):
        self.panel = panel
        self.state = WorldState()
        self.devices = set()
        self.handlers = {}
        self.shown = None
        self.last_gaze = None
        self.listening = False
        self.audio = bytearray()
        self.personality = personality.Personality()
        self.loop = None

    # --- events ---

    def on(self, event_type, fn):
        self.handlers.setdefault(event_type, []).append(fn)

    def emit(self, event):
        for fn in self.handlers.get(event["type"], []):
            fn(event)
        self.refresh()

    # --- devices ---

    async def add_device(self, device):
        self.devices.add(device)
        await device.command({"t": "listening", "on": self.listening})
        if self.last_gaze:
            await device.command(self.last_gaze)
        self.shown = None
        self.refresh()

    def remove_device(self, device):
        self.devices.discard(device)

    def broadcast(self, cmd):
        for d in list(self.devices):
            asyncio.ensure_future(d.command(cmd))

    def on_audio(self, device, frame):
        if self.listening:
            self.audio.extend(frame)

    # --- what the face shows ---

    def refresh(self):
        layer = current_layer(self.state)
        if layer == "idle":
            layer = self.personality.idle_expression()
        if layer == self.shown:
            return
        self.shown = layer
        if isinstance(layer, dict):
            self.broadcast(layer)
        else:
            self.broadcast({"t": "expression", "name": layer})

    # --- talking ---

    def talk_pressed(self):
        if self.listening:
            return
        self.listening = True
        self.audio = bytearray()
        self.broadcast({"t": "listening", "on": True})
        self.state.conversation = "listening"
        self.emit({"type": "user.wake"})

    def talk_released(self):
        if not self.listening:
            return
        self.listening = False
        self.broadcast({"t": "listening", "on": False})
        self.broadcast({"t": "blink"})
        self.state.conversation = "thinking"
        self.refresh()
        asyncio.ensure_future(self.transcribe(bytes(self.audio)))

    async def transcribe(self, pcm):
        text, confidence = await asyncio.to_thread(voice.transcribe, pcm)
        if not text:
            self.state.conversation = "confused"
            self.refresh()
            await asyncio.sleep(CONFUSED_SECONDS)
            self.state.conversation = None
            self.refresh()
            return
        if self.panel:
            self.panel.toast(text)
        self.state.conversation = None
        self.emit({"type": "user.transcript", "text": text, "confidence": confidence})

    # --- loops ---

    async def track_cursor(self, hz=30):
        while True:
            pos = mouse.cursor()
            if pos:
                cmd = {"t": "gaze", "x": round(pos[0], 3), "y": round(pos[1], 3)}
                if cmd != self.last_gaze:
                    self.last_gaze = cmd
                    self.broadcast(cmd)
            self.refresh()
            await asyncio.sleep(1 / hz)

    def call_soon(self, fn):
        """For other threads (hotkey listener, panel) to run something on the daemon's loop."""
        self.loop.call_soon_threadsafe(fn)

    async def run(self):
        self.loop = asyncio.get_running_loop()
        await self.track_cursor()
