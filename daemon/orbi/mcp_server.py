"""The sphere's MCP server: gives any MCP-capable agent the face_show, screen_show
and face_react tools, over streamable HTTP on localhost."""
import contextlib

import uvicorn
from mcp.server.mcpserver import MCPServer

from . import library

HOST = "127.0.0.1"
PORT = 8766
PATH = "/mcp"

ROUTING = f"""Orbi is a small glowing face on the user's desk. Where to put your answer:
- Fits in one scene or card plus a label of at most {library.MAX_LABEL} characters -> face_show only.
- Needs words (sentences, lists, code, tables, images, links) -> screen_show, plus a short face_show summary when one exists.
- When in doubt -> both."""

FACE_SHOW = f"""Show a glanceable result on the face for a few seconds: weather, a status, a number, a sender.
The face can show exactly one built-in scene OR one simple card, plus an optional one-line label of at most {library.MAX_LABEL} characters. Nothing else.
- scene: one of {", ".join(library.SCENES)}. The eyes stay visible and react to it.
- card: an object with exactly one key: {{"icon": name}} with name one of {", ".join(library.ICONS)}; {{"initials": "SJ"}} (1-3 letters); or {{"avatar": image URL or local file path}}.
- mood: eye shape during a scene, one of {", ".join(library.MOODS)}.
- label: at most {library.MAX_LABEL} characters, one line, e.g. "Demo → Fri".
Anything else (longer text, unknown names) is sent to the screen instead.

{ROUTING}"""

SCREEN_SHOW = f"""Show rich output in a panel on the user's computer screen. Use it for anything with sentences, lists, code, tables, images or links.
- content: markdown.
- title: a short title.
- actions: optional buttons, a list of {{"label": ..., "url": ...}}.
The face glances at the screen so the user knows to look.

{ROUTING}"""

FACE_REACT = f"""Make the face show a pure expression for a moment, with no content (for example done = pleased, failed = worried, didn't understand = confused).
expression: one of {", ".join(library.EXPRESSIONS)}."""


def build(display, tools=()):
    server = MCPServer("orbi", instructions=ROUTING)

    async def face_show(
        scene: str | None = None,
        card: dict[str, str] | None = None,
        mood: str | None = None,
        label: str | None = None,
    ) -> str:
        return await display.face_show(scene, card, mood, label)

    def screen_show(content: str, title: str = "", actions: list[dict[str, str]] | None = None) -> str:
        return display.screen_show(content, title, actions)

    def face_react(expression: str) -> str:
        return display.face_react(expression)

    server.add_tool(face_show, description=FACE_SHOW)
    server.add_tool(screen_show, description=SCREEN_SHOW)
    server.add_tool(face_react, description=FACE_REACT)
    for name, fn, description in tools:
        try:
            server.add_tool(fn, name=name, description=description)
        except Exception as e:
            print(f"Plugin tool {name} couldn't be added: {e!r}", flush=True)
    return server


class Server(uvicorn.Server):
    def capture_signals(self):
        return contextlib.nullcontext()  # leave Ctrl-C to the daemon


def make(display, port=PORT, tools=()):
    """A uvicorn server for the tools; run it with `await server.serve()`, stop it with should_exit.
    tools: extra (name, fn, description) from plugins."""
    app = build(display, tools).streamable_http_app(streamable_http_path=PATH, host=HOST)
    config = uvicorn.Config(app, host=HOST, port=port, log_level="warning", timeout_graceful_shutdown=1)
    return Server(config)
