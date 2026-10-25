"""What the agent's display tools do: validate face output, fall back to the screen,
and show results through the priority layers."""
import asyncio
import io
import urllib.request

from PIL import Image

from . import library

FACE_SECONDS = 4.0
REACT_SECONDS = 2.5
GLANCE_SECONDS = 1.2
GLANCE_GAZE = {"t": "gaze", "x": 0.8, "y": -0.8}  # up and toward the panel corner
AVATAR_SIZE = 120
CARD_KINDS = ["icon", "initials", "avatar"]
MAX_INITIALS = 3


def check_face(scene=None, card=None, mood=None, label=None):
    """Returns (command, None) if the face can show this, or (None, reason) if not."""
    if label is None:
        label = ""
    if len(label) > library.MAX_LABEL:
        return None, f"label is over {library.MAX_LABEL} characters"
    if "\n" in label:
        return None, "label must be one line"
    if mood is not None and mood not in library.MOODS:
        return None, f"unknown mood '{mood}'"
    if scene and card:
        return None, "give a scene or a card, not both"
    if scene:
        if scene not in library.SCENES:
            return None, f"unknown scene '{scene}'"
        cmd = {"t": "scene", "name": scene, "mood": mood or "neutral"}
        if label:
            cmd["label"] = label
        cmd["duration_ms"] = int(FACE_SECONDS * 1000)
        return cmd, None
    if card:
        if len(card) != 1:
            return None, "a card has exactly one of icon, initials or avatar"
        kind = list(card)[0]
        value = card[kind]
        if kind not in CARD_KINDS:
            return None, f"unknown card kind '{kind}'"
        if not isinstance(value, str) or not value:
            return None, f"card {kind} must be a non-empty string"
        if kind == "icon" and value not in library.ICONS:
            return None, f"unknown icon '{value}'"
        if kind == "initials" and len(value) > MAX_INITIALS:
            return None, f"initials are at most {MAX_INITIALS} characters"
        cmd = {"t": "card", kind: value, "label": label, "duration_ms": int(FACE_SECONDS * 1000)}
        return cmd, None
    return None, "nothing to show (give a scene or a card)"


def screen_fallback(scene=None, card=None, mood=None, label=None):
    """Markdown for the screen built from a face_show call the face can't show."""
    lines = []
    if label:
        lines.append(f"## {label}")
    if scene:
        lines.append(f"Scene: {scene}")
    if card:
        for kind, value in card.items():
            if kind == "avatar":
                lines.append(f"![avatar]({value})")
            else:
                lines.append(f"{kind.capitalize()}: {value}")
    if mood:
        lines.append(f"Mood: {mood}")
    return "\n\n".join(lines)


def load_avatar(source):
    """Image URL or local path -> 120x120 RGB565 little-endian bytes."""
    if source.startswith("http://") or source.startswith("https://"):
        with urllib.request.urlopen(source, timeout=10) as resp:
            data = resp.read()
        img = Image.open(io.BytesIO(data))
    else:
        img = Image.open(source)
    img = img.convert("RGB")
    side = min(img.width, img.height)
    left = (img.width - side) // 2
    top = (img.height - side) // 2
    img = img.crop((left, top, left + side, top + side))
    img = img.resize((AVATAR_SIZE, AVATAR_SIZE))
    rgb = img.tobytes()
    out = bytearray()
    for i in range(0, len(rgb), 3):
        r, g, b = rgb[i], rgb[i + 1], rgb[i + 2]
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out += value.to_bytes(2, "little")
    return bytes(out)


class Display:
    """The three display tools, plus the handlers that put their events on the face and screen."""

    def __init__(self, daemon):
        self.daemon = daemon
        daemon.on("display.face", self.on_face)
        daemon.on("display.screen", self.on_screen)

    # --- tools ---

    async def face_show(self, scene=None, card=None, mood=None, label=None):
        cmd, reason = check_face(scene, card, mood, label)
        if cmd and "avatar" in cmd:
            try:
                frame = await asyncio.to_thread(load_avatar, cmd["avatar"])
            except Exception as e:
                cmd = None
                reason = f"couldn't load the avatar image ({e})"
            else:
                await self.send_image(frame)
                cmd["avatar"] = True
        if cmd is None:
            content = screen_fallback(scene, card, mood, label)
            self.screen_show(content, title=label or "Orbi")
            return f"The face can't show that ({reason}), so it went to the screen instead."
        self.daemon.emit({"type": "display.face", "command": cmd})
        return "Shown on the face."

    def screen_show(self, content, title="", actions=None):
        self.daemon.emit({"type": "display.screen", "title": title, "content": content, "actions": actions or []})
        return "Shown on the screen."

    def face_react(self, expression):
        if expression not in library.EXPRESSIONS:
            allowed = ", ".join(library.EXPRESSIONS)
            return f"Unknown expression '{expression}'. Use one of: {allowed}."
        cmd = {"t": "expression", "name": expression}
        self.daemon.emit({"type": "display.face", "command": cmd, "seconds": REACT_SECONDS})
        return "Done."

    # --- showing ---

    async def send_image(self, frame):
        sends = [d.send(frame) for d in list(self.daemon.devices)]
        await asyncio.gather(*sends)

    def on_face(self, event):
        cmd = event["command"]
        seconds = event.get("seconds", FACE_SECONDS)
        self.daemon.state.face_content = cmd
        asyncio.get_running_loop().call_later(seconds, self.clear_face, cmd)

    def clear_face(self, cmd):
        if self.daemon.state.face_content is cmd:
            self.daemon.state.face_content = None
            self.daemon.refresh()

    def on_screen(self, event):
        if self.daemon.panel:
            self.daemon.panel.show(event["title"], event["content"], event["actions"])
        self.daemon.glance(GLANCE_GAZE, GLANCE_SECONDS)
