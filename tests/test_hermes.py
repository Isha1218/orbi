import asyncio
import os
import sys

import pytest

from orbi import agents

from test_mcp import url  # noqa: F401  (fixture)

FAKE_HERMES = """#!{python}
import asyncio, os, sys, time
assert sys.argv[1:4] == ["chat", "-Q", "-q"], sys.argv
text = sys.argv[4]
if text == "weather":
    print("Sunny, 62°")
elif text == "explode":
    print("model error", file=sys.stderr)
    sys.exit(1)
elif text == "slow":
    time.sleep(0.3)
    print("slow done")
elif text == "show":
    from mcp import Client

    async def main():
        async with Client(os.environ["ORBI_TEST_MCP"]) as client:
            await client.call_tool("face_show", {{"scene": "sun", "label": "62°"}})

    asyncio.run(main())
    print("shown")
"""


@pytest.fixture(autouse=True)
def fake_hermes(tmp_path, monkeypatch):
    path = tmp_path / "hermes"
    path.write_text(FAKE_HERMES.format(python=sys.executable))
    path.chmod(0o755)
    monkeypatch.setenv("PATH", f"{tmp_path}{os.pathsep}{os.environ['PATH']}")
    monkeypatch.setattr(agents, "NEWS_SECONDS", 0.05)


def record(daemon):
    events = []
    for name in ["agent.started", "agent.working", "agent.done", "agent.failed"]:
        daemon.on(name, events.append)
    return events


async def finish(daemon):
    await asyncio.gather(*daemon.hermes.tasks)


async def test_success_shows_result_on_screen(daemon):
    events = record(daemon)
    daemon.emit({"type": "user.transcript", "text": "weather", "confidence": 0.9})
    await finish(daemon)
    assert [e["type"] for e in events] == ["agent.started", "agent.working", "agent.done"]
    assert events[0]["task"] == "weather"
    assert events[2]["result"] == "Sunny, 62°"
    assert daemon.panel.shown == [{"title": "weather", "content": "Sunny, 62°", "actions": []}]
    assert daemon.shown == "pleased"
    await asyncio.sleep(0.1)
    assert daemon.shown == "idle"


async def test_failure(daemon):
    events = record(daemon)
    daemon.emit({"type": "user.transcript", "text": "explode", "confidence": 0.9})
    await finish(daemon)
    assert events[-1] == {"type": "agent.failed", "id": events[0]["id"], "error": "model error"}
    assert daemon.shown == "worried"
    assert daemon.state.agents == []


async def test_hermes_missing(daemon, monkeypatch, tmp_path):
    empty = tmp_path / "empty"
    empty.mkdir()
    monkeypatch.setenv("PATH", str(empty))
    events = record(daemon)
    daemon.emit({"type": "user.transcript", "text": "weather", "confidence": 0.9})
    await finish(daemon)
    assert events[-1]["type"] == "agent.failed"
    assert "couldn't start hermes" in events[-1]["error"]


async def test_display_tool_called_means_no_screen_fallback(daemon, url, monkeypatch):  # noqa: F811
    monkeypatch.setenv("ORBI_TEST_MCP", url)
    events = record(daemon)
    daemon.emit({"type": "user.transcript", "text": "show", "confidence": 0.9})
    await finish(daemon)
    assert events[-1]["type"] == "agent.done"
    assert daemon.panel.shown == []
    assert daemon.state.face_content["name"] == "sun"


async def test_agents_run_at_once(daemon):
    daemon.emit({"type": "user.transcript", "text": "slow", "confidence": 0.9})
    daemon.emit({"type": "user.transcript", "text": "slow", "confidence": 0.9})
    await asyncio.sleep(0.1)
    assert [a.id for a in daemon.state.agents] == ["hermes-1", "hermes-2"]
    assert daemon.shown == "busy"
    await finish(daemon)
    assert daemon.state.agents == []
    assert len(daemon.state.unread) == 2
    assert len(daemon.panel.shown) == 2
