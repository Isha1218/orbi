"""A fake phone connects to the real server, talks, and the daemon hears it."""
import asyncio
import json
import socket
import ssl

from websockets.asyncio.client import connect

from orbi import certs, claude_code, hotkey, mouse, plugins, server
from orbi.daemon import Daemon

FRAME_BYTES = 3200  # 100 ms of audio


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


async def next_command(ws, timeout=10):
    """The next JSON command from the daemon, skipping gaze updates."""
    while True:
        msg = await asyncio.wait_for(ws.recv(), timeout)
        cmd = json.loads(msg)
        if cmd["t"] != "gaze":
            return cmd


async def connect_device(port):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    for attempt in range(50):
        try:
            return await connect(f"wss://127.0.0.1:{port}/ws", ssl=ctx)
        except OSError:
            await asyncio.sleep(0.1)
    raise RuntimeError("server never came up")


async def talk(daemon, port, pcm):
    transcripts = asyncio.get_running_loop().create_future()
    daemon.on("user.transcript", transcripts.set_result)
    server_task = asyncio.create_task(server.run(daemon))
    ws = await connect_device(port)
    try:
        await ws.send(json.dumps({"t": "hello", "v": 1, "firmware": "0.1", "display": 360, "caps": ["mic"]}))
        assert await next_command(ws) == {"t": "listening", "on": False}
        assert await next_command(ws) == {"t": "expression", "name": "idle"}

        daemon.talk_pressed()
        assert await next_command(ws) == {"t": "listening", "on": True}
        assert await next_command(ws) == {"t": "expression", "name": "listening"}

        for i in range(0, len(pcm), FRAME_BYTES):
            await ws.send(pcm[i:i + FRAME_BYTES])
        while len(daemon.audio) < len(pcm):
            await asyncio.sleep(0.01)

        daemon.talk_released()
        assert await next_command(ws) == {"t": "listening", "on": False}
        assert await next_command(ws) == {"t": "blink"}
        assert await next_command(ws) == {"t": "expression", "name": "thinking"}

        event = await asyncio.wait_for(transcripts, 120)
        assert await next_command(ws) == {"t": "expression", "name": "idle"}
        return event
    finally:
        await ws.close()
        server_task.cancel()
        try:
            await server_task
        except asyncio.CancelledError:
            pass


def test_push_to_talk_end_to_end(monkeypatch, tmp_path, weather_pcm):
    port = free_port()
    monkeypatch.setattr(server, "PORT", port)
    monkeypatch.setattr(certs, "CERT_DIR", tmp_path)
    monkeypatch.setattr(certs, "lan_ip", lambda: "127.0.0.1")
    monkeypatch.setattr(hotkey, "start", lambda daemon: None)
    monkeypatch.setattr(mouse, "cursor", lambda: None)
    monkeypatch.setattr(plugins, "USER_DIR", tmp_path / "plugins")
    monkeypatch.setattr(claude_code, "command", lambda text: ["true"])  # never start a real agent

    daemon = Daemon()
    monkeypatch.setattr(daemon.personality, "idle_expression", lambda: "idle")

    event = asyncio.run(talk(daemon, port, weather_pcm))
    assert "weather" in event["text"].lower()
    assert event["confidence"] > 0
