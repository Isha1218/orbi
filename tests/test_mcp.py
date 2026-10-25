import asyncio
import socket

import pytest
from mcp import Client

from orbi import mcp_server


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


async def wait_for_port(port):
    for _ in range(100):
        try:
            reader, writer = await asyncio.open_connection("127.0.0.1", port)
        except OSError:
            await asyncio.sleep(0.05)
            continue
        writer.close()
        return
    raise TimeoutError(port)


@pytest.fixture
async def url(daemon):
    port = free_port()
    server = mcp_server.make(daemon.display, port=port)
    task = asyncio.ensure_future(server.serve())
    await wait_for_port(port)
    yield f"http://127.0.0.1:{port}/mcp"
    server.should_exit = True
    await task


async def test_round_trip(daemon, url):
    async with Client(url) as client:
        tools = await client.list_tools()
        names = sorted(t.name for t in tools.tools)
        assert names == ["face_react", "face_show", "screen_show"]
        face_show = [t for t in tools.tools if t.name == "face_show"][0]
        assert "12 characters" in face_show.description
        assert "rain" in face_show.description

        result = await client.call_tool("face_show", {"scene": "rain", "mood": "grumpy", "label": "62° rain"})
        assert result.content[0].text == "Shown on the face."
        assert daemon.state.face_content["name"] == "rain"

        result = await client.call_tool("face_show", {"scene": "rain", "label": "62° · rain 3pm"})
        assert "screen" in result.content[0].text
        assert daemon.panel.shown[-1]["title"] == "62° · rain 3pm"

        result = await client.call_tool("face_show", {"card": {"initials": "SJ"}, "label": "Demo → Fri"})
        assert daemon.state.face_content["initials"] == "SJ"

        actions = [{"label": "Open in Gmail", "url": "https://mail.google.com"}]
        await client.call_tool("screen_show", {"content": "- Friday 2pm", "title": "Re: Demo", "actions": actions})
        assert daemon.panel.shown[-1] == {"title": "Re: Demo", "content": "- Friday 2pm", "actions": actions}

        await client.call_tool("face_react", {"expression": "worried"})
        assert daemon.state.face_content == {"t": "expression", "name": "worried"}
