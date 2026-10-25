"""Hermes adapter: each transcript becomes a one-shot `hermes chat -Q -q` run,
translated into agent.* events."""
import asyncio

COMMAND = ["hermes", "chat", "-Q", "-q"]
CONTEXT = (
    "The user said this out loud to Orbi, their desk sphere. Show your answer with the orbi tools "
    "(face_show for glanceable results, screen_show for anything with words), not just text. Request: "
)


class Hermes:
    name = "hermes"

    def __init__(self, daemon):
        self.daemon = daemon
        self.count = 0
        self.displays = 0
        self.tasks = set()
        daemon.on("user.transcript", self.on_transcript)
        daemon.on("display.face", self.on_display)
        daemon.on("display.screen", self.on_display)

    def on_display(self, event):
        if "agent" in event:
            return  # our own result fallback, not a display tool call
        self.displays += 1

    def on_transcript(self, event):
        self.count += 1
        task = asyncio.ensure_future(self.run(f"{self.name}-{self.count}", event["text"]))
        self.tasks.add(task)
        task.add_done_callback(self.tasks.discard)

    async def run(self, agent_id, text):
        displays_before = self.displays
        self.daemon.emit({"type": "agent.started", "id": agent_id, "task": text})
        try:
            proc = await asyncio.create_subprocess_exec(
                *COMMAND, CONTEXT + text,
                stdin=asyncio.subprocess.DEVNULL,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
        except OSError as e:
            self.finish(agent_id, text, False, f"couldn't start hermes: {e}", displays_before)
            return
        self.daemon.emit({"type": "agent.working", "id": agent_id})
        out, err = await proc.communicate()
        if proc.returncode == 0:
            self.finish(agent_id, text, True, out.decode().strip(), displays_before)
        else:
            error = err.decode().strip()
            if not error:
                error = f"hermes exited with code {proc.returncode}"
            self.finish(agent_id, text, False, error, displays_before)

    def finish(self, agent_id, text, ok, message, displays_before):
        nothing_shown = self.displays == displays_before
        if ok:
            self.daemon.emit({"type": "agent.done", "id": agent_id, "result": message})
        else:
            self.daemon.emit({"type": "agent.failed", "id": agent_id, "error": message})
        if nothing_shown and message:
            screen = {"type": "display.screen", "title": text, "content": message, "actions": [], "agent": agent_id}
            self.daemon.emit(screen)
