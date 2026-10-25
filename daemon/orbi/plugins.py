"""The plugin API: plugins add agent tools, react to events, put things on the
priority layers, and add idle behaviors. They never talk to the device directly."""
import asyncio
import importlib.util
from dataclasses import dataclass
from pathlib import Path

from . import agents, display, library
from .state import News

BUILTIN_DIR = Path(__file__).with_name("plugins_builtin")
USER_DIR = Path.home() / ".orbi" / "plugins"
LAYERS = ["content", "news", "ambient"]
AMBIENT_SECONDS = 60.0


@dataclass
class Plugin:
    name: str
    setup: object


def define_plugin(name, setup):
    return Plugin(name, setup)


class Tools:
    """Agent tools, shown to the agent as <plugin>_<name> next to face_show and friends."""

    def __init__(self, plugin_name):
        self.plugin_name = plugin_name
        self.registered = []

    def register(self, name, fn, description=None):
        if description is None:
            description = fn.__doc__ or ""
        self.registered.append((f"{self.plugin_name}_{name}", fn, description))


class Face:
    def __init__(self, display):
        self.display = display

    def show(self, scene=None, card=None, mood=None, label=None):
        """Same validation as the face_show tool. Returns a task; await it for the result."""
        return asyncio.ensure_future(self.display.face_show(scene, card, mood, label))

    def react(self, expression):
        return self.display.face_react(expression)


class Screen:
    def __init__(self, display):
        self.display = display

    def show(self, content, title="", actions=None):
        return self.display.screen_show(content, title, actions)


class Context:
    """What a plugin's setup(ctx) gets."""

    def __init__(self, plugins, name):
        self.plugins = plugins
        self.name = name
        self.tools = Tools(name)
        self.face = Face(plugins.display)
        self.screen = Screen(plugins.display)
        self.handlers = []
        self.behaviors = []

    def on(self, event_type, fn):
        self.handlers.append((event_type, fn))

    def emit(self, event):
        """Puts an event on the bus. With a "layer", its scene, card or expression joins that priority layer."""
        if "layer" in event:
            self.plugins.place(self.name, event)
        self.plugins.daemon.emit(event)

    def idle(self, fn):
        """fn(idle_seconds, hour) returns an expression name, or None to pass."""
        self.behaviors.append(fn)


def layer_command(event):
    """Returns (command, None) for the face, or (None, reason)."""
    if "expression" in event:
        name = event["expression"]
        if name not in library.EXPRESSIONS:
            return None, f"unknown expression '{name}'"
        return {"t": "expression", "name": name}, None
    cmd, reason = display.check_face(event.get("scene"), event.get("card"), event.get("mood"), event.get("label"))
    if cmd and "avatar" in cmd:
        return None, "avatars only work with ctx.face.show"
    return cmd, reason


class Plugins:
    def __init__(self, daemon, display):
        self.daemon = daemon
        self.display = display
        self.tools = []
        self.loaded = []

    # --- loading ---

    def load_all(self, folders=None):
        if folders is None:
            folders = [BUILTIN_DIR, USER_DIR]
        for folder in folders:
            for path in sorted(Path(folder).glob("*.py")):
                if not path.name.startswith("_"):
                    self.load_file(path)
        names = ", ".join(self.loaded)
        if not names:
            names = "none"
        print(f"Plugins loaded: {names}", flush=True)

    def load_file(self, path):
        try:
            spec = importlib.util.spec_from_file_location(f"orbi_plugin_{path.stem}", path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            self.add(module.plugin)
        except Exception as e:
            print(f"Plugin {path.name} failed to load: {e!r}", flush=True)

    def add(self, plugin):
        ctx = Context(self, plugin.name)
        plugin.setup(ctx)
        # Only a setup that finished gets its tools, handlers and behaviors
        self.tools.extend(ctx.tools.registered)
        for event_type, fn in ctx.handlers:
            self.daemon.on(event_type, self.guard(plugin.name, fn))
        for fn in ctx.behaviors:
            self.daemon.personality.behaviors.append((plugin.name, fn))
        self.loaded.append(plugin.name)

    def guard(self, name, fn):
        def handler(event):
            try:
                fn(event)
            except Exception as e:
                print(f"Plugin {name}: {event['type']} handler failed: {e!r}", flush=True)
        return handler

    # --- priority layers ---

    def place(self, name, event):
        layer = event["layer"]
        if layer not in LAYERS:
            print(f"Plugin {name}: unknown layer '{layer}'", flush=True)
            return
        cmd, reason = layer_command(event)
        if cmd is None:
            print(f"Plugin {name}: can't show {event['type']} ({reason})", flush=True)
            return
        seconds = event.get("seconds")
        if seconds is None:
            seconds = self.default_seconds(layer)
        if "duration_ms" in cmd:
            cmd["duration_ms"] = int(seconds * 1000)
        loop = asyncio.get_running_loop()
        state = self.daemon.state
        if layer == "content":
            state.face_content = cmd
            loop.call_later(seconds, self.display.clear_face, cmd)
        elif layer == "news":
            news = News(name, True, cmd)
            state.unread.append(news)
            loop.call_later(seconds, self.clear, state.unread, news)
        else:
            state.ambient.append(cmd)
            loop.call_later(seconds, self.clear, state.ambient, cmd)

    def default_seconds(self, layer):
        if layer == "content":
            return display.FACE_SECONDS
        if layer == "news":
            return agents.NEWS_SECONDS
        return AMBIENT_SECONDS

    def clear(self, items, item):
        if item in items:
            items.remove(item)
            self.daemon.refresh()
