"""One HTTPS port for everything: serves the phone app and takes device connections on /ws."""
import json
import mimetypes
from http import HTTPStatus
from pathlib import Path

import qrcode
from websockets.asyncio.server import serve
from websockets.datastructures import Headers
from websockets.http11 import Response

from . import certs
from .daemon import Device

PORT = 8765
APP_DIR = Path(__file__).resolve().parents[2] / "phone"


def static_file(connection, request):
    if request.path.split("?")[0] == "/ws":
        return None  # carry on with the WebSocket handshake
    path = request.path.split("?")[0].lstrip("/") or "index.html"
    file = (APP_DIR / path).resolve()
    if not file.is_relative_to(APP_DIR) or not file.is_file():
        return connection.respond(HTTPStatus.NOT_FOUND, "not found\n")
    kind = mimetypes.guess_type(file.name)[0] or "application/octet-stream"
    if file.suffix == ".wasm":
        kind = "application/wasm"
    headers = Headers([("Content-Type", kind), ("Cache-Control", "no-store")])
    return Response(200, "OK", headers, file.read_bytes())


def show_pairing(url):
    qr = qrcode.QRCode(border=1)
    qr.add_data(url)
    qr.print_ascii(invert=True)
    print(f"Scan with your phone, or open {url}")
    print("Your browser will warn about the certificate once; that's expected (it's your own computer).", flush=True)


async def run(daemon):
    async def handle(ws):
        device = Device(ws.send)
        try:
            async for msg in ws:
                if isinstance(msg, bytes):
                    daemon.on_audio(device, msg)
                    continue
                cmd = json.loads(msg)
                if cmd.get("t") == "hello" and device.hello is None:
                    device.hello = cmd
                    await daemon.add_device(device)
        finally:
            daemon.remove_device(device)

    ip = certs.lan_ip()
    async with serve(handle, "0.0.0.0", PORT, ssl=certs.ssl_context(ip), process_request=static_file):
        show_pairing(f"https://{ip}:{PORT}")
        await daemon.run()
