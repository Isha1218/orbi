# Writing an Orbi plugin

A plugin is one Python file. Orbi loads the built-in ones from `daemon/orbi/plugins_builtin/` and yours from `~/.orbi/plugins/*.py` when `orbi run` starts.

> **Plugins run inside the daemon with its full permissions.** They can read your files, use the network and run programs, just like the daemon. Only install plugins you trust. (A per-plugin permission manifest is a later improvement.)

```python
from orbi.plugins import define_plugin


async def get_weather(city: str) -> str:
    """Weather for a city."""
    return "62°F, rain at 3pm"


def setup(ctx):
    ctx.tools.register("get_weather", get_weather)
    ctx.on("agent.done", lambda e: ctx.face.react("pleased"))
    ctx.emit({"type": "plugin.weather.alert", "layer": "news", "scene": "storm"})


plugin = define_plugin(name="weather", setup=setup)
```

## What `ctx` gives you

| Call | What it does |
| --- | --- |
| `ctx.tools.register(name, fn, description=None)` | Adds an agent tool, shown over the sphere MCP server as `<plugin>_<name>` (e.g. `weather_get_weather`). `fn` can be sync or async; type hints become the tool's arguments; the description defaults to the docstring. |
| `ctx.on(event_type, fn)` | Calls `fn(event)` for each event of that type (`user.transcript`, `agent.done`, `display.face`, `plugin.*`, ...). Keep it quick and synchronous. |
| `ctx.emit(event)` | Puts an event on the bus. Add `"layer"` to also show something on the face (see below). |
| `ctx.face.show(scene=, card=, mood=, label=)` | Same as the `face_show` tool, including validation and the screen fallback. Returns a task; `await` it for the result message. `card={"avatar": path_or_url}` shows your own image. |
| `ctx.face.react(expression)` | Same as `face_react`. |
| `ctx.screen.show(content, title="", actions=None)` | Same as `screen_show` (markdown, buttons as `{"label", "url"}`). |
| `ctx.idle(fn)` | An idle behavior: `fn(idle_seconds, hour)` returns an expression name or `None`. Asked many times a second, only when nothing else is on, so keep it cheap. |

Names for scenes, icons, moods and expressions are in `daemon/orbi/library.py`. Labels are at most 12 characters.

## Layers

`ctx.emit({"type": "plugin.<name>.<what>", "layer": ..., "scene"/"card"/"expression": ..., "mood": ..., "label": ..., "seconds": ...})`

The face shows the highest layer: conversation > **content** > needs you > **news** > busy agents > **ambient** > idle. Everything times out: `seconds` defaults to 4 (content), 10 (news) or 60 (ambient). Items the face can't show are skipped with a log line.

## Errors

A plugin that fails to load, or whose handler or idle behavior raises, is logged in one line and the daemon carries on. A failing idle behavior is turned off.
