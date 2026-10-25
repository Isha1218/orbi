"""Claude Code adapter: each transcript becomes a one-shot `claude -p` run on the user's own
Claude login, translated into agent.* events. Same results handling as the Hermes adapter."""
import asyncio
import json
from pathlib import Path

from . import mcp_server
from .hermes import Hermes

MODEL = "opus"
WORKSPACE = Path.home() / ".orbi" / "workspace"  # empty folder, so no project's files or rules get pulled in
MCP_CONFIG = {"mcpServers": {"orbi": {"type": "http", "url": f"http://{mcp_server.HOST}:{mcp_server.PORT}{mcp_server.PATH}"}}}
PROMPT = (
    "The user said this out loud to Orbi, their desk sphere. Show your answer with the orbi tools "
    "(face_show for glanceable results, screen_show for anything with words), not just text."
)
TOOLS = ["WebSearch", "WebFetch"]  # no shell, no file edits: you can't approve those by voice


def command(text):
    return [
        "claude", "-p", text,
        "--model", MODEL,
        "--output-format", "stream-json", "--verbose",
        "--mcp-config", json.dumps(MCP_CONFIG), "--strict-mcp-config",
        "--tools", *TOOLS,
        "--allowedTools", "mcp__orbi", *TOOLS,
        "--append-system-prompt", PROMPT,
    ]


class ClaudeCode(Hermes):
    name = "claude"

    async def run(self, agent_id, text):
        displays_before = self.displays
        self.daemon.emit({"type": "agent.started", "id": agent_id, "task": text})
        WORKSPACE.mkdir(parents=True, exist_ok=True)
        try:
            proc = await asyncio.create_subprocess_exec(
                *command(text),
                cwd=WORKSPACE,
                stdin=asyncio.subprocess.DEVNULL,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                limit=10 * 1024 * 1024,
            )
        except OSError as e:
            self.finish(agent_id, text, False, f"couldn't start claude: {e}", displays_before)
            return
        self.daemon.emit({"type": "agent.working", "id": agent_id})
        err_task = asyncio.ensure_future(proc.stderr.read())

        result = None
        async for line in proc.stdout:
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            if msg.get("type") == "assistant":
                for block in msg["message"].get("content", []):
                    if block.get("type") == "tool_use":
                        self.daemon.emit({"type": "agent.working", "id": agent_id, "note": block["name"]})
            elif msg.get("type") == "result":
                result = msg
        err = await err_task
        await proc.wait()

        if result is not None and not result.get("is_error"):
            self.finish(agent_id, text, True, result.get("result", "").strip(), displays_before)
            return
        if result is not None and result.get("result"):
            error = result["result"]
        else:
            error = err.decode().strip()
        if not error:
            error = f"claude exited with code {proc.returncode}"
        self.finish(agent_id, text, False, error, displays_before)
