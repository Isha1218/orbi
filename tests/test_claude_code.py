import asyncio
import json
import os
import sys

import pytest

from orbi import agents, claude_code
from orbi.claude_code import ClaudeCode

FAKE_CLAUDE = """#!{python}
import json, sys
args = sys.argv[1:]
assert args[0] == "-p", args
assert "--strict-mcp-config" in args and "opus" in args, args
assert "Bash" not in args, args
text = args[1]


def say(msg):
    print(json.dumps(msg), flush=True)


say({{"type": "system", "subtype": "init"}})
if text == "weather":
    say({{"type": "assistant", "message": {{"content": [{{"type": "tool_use", "name": "WebSearch", "input": {{}}}}]}}}})
    say({{"type": "result", "subtype": "success", "is_error": False, "result": "Sunny, 62°"}})
elif text == "refused":
    say({{"type": "result", "subtype": "success", "is_error": True, "result": "usage limit reached"}})
    sys.exit(1)
else:
    print("crashed", file=sys.stderr)
    sys.exit(2)
"""


@pytest.fixture
def claude(daemon, tmp_path, monkeypatch):
    path = tmp_path / "claude"
    path.write_text(FAKE_CLAUDE.format(python=sys.executable))
    path.chmod(0o755)
    monkeypatch.setenv("PATH", f"{tmp_path}{os.pathsep}{os.environ['PATH']}")
    monkeypatch.setattr(claude_code, "WORKSPACE", tmp_path / "workspace")
    monkeypatch.setattr(agents, "NEWS_SECONDS", 0.05)
    daemon.hermes.tasks.clear()
    daemon.handlers["user.transcript"] = []  # replace the Hermes adapter from the fixture
    return ClaudeCode(daemon)


def record(daemon):
    events = []
    for name in ["agent.started", "agent.working", "agent.done", "agent.failed"]:
        daemon.on(name, events.append)
    return events


async def test_success(daemon, claude):
    events = record(daemon)
    daemon.emit({"type": "user.transcript", "text": "weather", "confidence": 0.9})
    await asyncio.gather(*claude.tasks)
    assert [e["type"] for e in events] == ["agent.started", "agent.working", "agent.working", "agent.done"]
    assert events[2]["note"] == "WebSearch"
    assert events[0]["id"] == "claude-1"
    assert events[-1]["result"] == "Sunny, 62°"
    assert daemon.panel.shown == [{"title": "weather", "content": "Sunny, 62°", "actions": []}]
    assert daemon.shown == "pleased"


async def test_error_result(daemon, claude):
    events = record(daemon)
    daemon.emit({"type": "user.transcript", "text": "refused", "confidence": 0.9})
    await asyncio.gather(*claude.tasks)
    assert events[-1]["type"] == "agent.failed"
    assert events[-1]["error"] == "usage limit reached"
    assert daemon.shown == "worried"


async def test_crash(daemon, claude):
    events = record(daemon)
    daemon.emit({"type": "user.transcript", "text": "boom", "confidence": 0.9})
    await asyncio.gather(*claude.tasks)
    assert events[-1] == {"type": "agent.failed", "id": "claude-1", "error": "crashed"}


def test_command_has_no_shell_or_edits():
    args = claude_code.command("hi")
    assert json.loads(args[args.index("--mcp-config") + 1])["mcpServers"]["orbi"]["url"] == "http://127.0.0.1:8766/mcp"
    tools = args[args.index("--tools") + 1:args.index("--allowedTools")]
    assert tools == ["WebSearch", "WebFetch"]
