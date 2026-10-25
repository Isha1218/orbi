import asyncio

import pytest
from mcp import Client
from PIL import Image

from orbi import mcp_server, plugins
from orbi.personality import Personality
from orbi.plugins import Plugins, define_plugin
from orbi.plugins_builtin import weather

from conftest import FakeDevice
from test_mcp import free_port, wait_for_port

GOOD = """
from orbi.plugins import define_plugin

def setup(ctx):
    ctx.tools.register("hello", lambda: "hi", description="Say hi")

plugin = define_plugin(name="good", setup=setup)
"""

BROKEN_SETUP = """
from orbi.plugins import define_plugin

def setup(ctx):
    ctx.tools.register("half", lambda: "never")
    raise RuntimeError("oops")

plugin = define_plugin(name="broken", setup=setup)
"""


@pytest.fixture
def loader(daemon):
    return Plugins(daemon, daemon.display)


def add(loader, name, setup):
    loader.add(define_plugin(name=name, setup=setup))


def test_loads_builtin_and_user_plugins(loader, tmp_path, capsys):
    (tmp_path / "good.py").write_text(GOOD)
    (tmp_path / "broken.py").write_text(BROKEN_SETUP)
    (tmp_path / "syntax.py").write_text("def nope(:\n")
    (tmp_path / "missing.py").write_text("x = 1\n")
    loader.load_all([plugins.BUILTIN_DIR, tmp_path])

    assert loader.loaded == ["email", "weather", "good"]
    names = [name for name, fn, description in loader.tools]
    assert names == ["email_show_email", "weather_get_weather", "good_hello"]
    out = capsys.readouterr().out
    assert "broken.py failed to load: RuntimeError('oops')" in out
    assert "syntax.py failed to load" in out
    assert "missing.py failed to load" in out
    assert "Plugins loaded: email, weather, good" in out


def test_default_folders_include_user_dir(loader, tmp_path, monkeypatch, capsys):
    (tmp_path / "good.py").write_text(GOOD)
    monkeypatch.setattr(plugins, "USER_DIR", tmp_path)
    loader.load_all()
    assert "good" in loader.loaded


async def test_doc_example(daemon, loader):
    def setup(ctx):
        ctx.tools.register("get_weather", lambda city: "sunny", description="Weather")
        ctx.on("agent.done", lambda e: ctx.face.react("pleased"))
        ctx.emit({"type": "plugin.weather.alert", "layer": "news", "scene": "storm"})

    add(loader, "weather", setup)
    assert loader.tools[0][0] == "weather_get_weather"
    assert daemon.shown["name"] == "storm"
    daemon.emit({"type": "agent.done", "id": "h1", "result": ""})
    assert daemon.state.face_content == {"t": "expression", "name": "pleased"}


async def test_handler_that_raises_doesnt_crash(daemon, loader, capsys):
    seen = []

    def setup(ctx):
        ctx.on("agent.done", lambda e: 1 / 0)
        ctx.on("agent.done", seen.append)

    add(loader, "flaky", setup)
    daemon.emit({"type": "agent.done", "id": "h1", "result": ""})
    assert len(seen) == 1
    assert "Plugin flaky: agent.done handler failed: ZeroDivisionError" in capsys.readouterr().out


async def test_plugin_tools_over_mcp(daemon, loader):
    loader.load_all([plugins.BUILTIN_DIR])

    def setup(ctx):
        async def roll(sides: int) -> str:
            return f"rolled a d{sides}"
        ctx.tools.register("roll", roll, description="Roll a die")

    add(loader, "dice", setup)
    port = free_port()
    server = mcp_server.make(daemon.display, port=port, tools=loader.tools)
    task = asyncio.ensure_future(server.serve())
    await wait_for_port(port)
    try:
        async with Client(f"http://127.0.0.1:{port}/mcp") as client:
            tools = await client.list_tools()
            names = sorted(t.name for t in tools.tools)
            assert names == ["dice_roll", "email_show_email", "face_react", "face_show",
                             "screen_show", "weather_get_weather"]
            result = await client.call_tool("dice_roll", {"sides": 20})
            assert result.content[0].text == "rolled a d20"
            args = {"sender": "Sarah J. <sarah@x.com>", "subject": "Re: Demo", "summary_markdown": "- Fri 2pm",
                    "label": "Demo → Fri"}
            result = await client.call_tool("email_show_email", args)
            assert "Shown on the face." in result.content[0].text
            assert daemon.state.face_content["initials"] == "SJ"
    finally:
        server.should_exit = True
        await task


async def test_layers_follow_priority_order_and_time_out(daemon, loader):
    contexts = []
    add(loader, "p", contexts.append)
    ctx = contexts[0]

    ctx.emit({"type": "plugin.p.ci", "layer": "ambient", "expression": "busy", "seconds": 0.1})
    assert daemon.shown == {"t": "expression", "name": "busy"}

    daemon.emit({"type": "agent.started", "id": "h1", "task": "x"})
    daemon.emit({"type": "agent.done", "id": "h1", "result": ""})
    assert daemon.shown == "pleased"
    daemon.state.unread.clear()
    daemon.refresh()

    ctx.emit({"type": "plugin.p.alert", "layer": "news", "scene": "storm", "label": "Build ✗", "seconds": 0.1})
    assert daemon.shown == {"t": "scene", "name": "storm", "mood": "neutral", "label": "Build ✗", "duration_ms": 100}

    ctx.emit({"type": "plugin.p.now", "layer": "content", "card": {"icon": "bell"}, "seconds": 0.05})
    assert daemon.shown["icon"] == "bell"

    await asyncio.sleep(0.07)
    assert daemon.shown["name"] == "storm"
    await asyncio.sleep(0.1)
    assert daemon.state.unread == []
    assert daemon.state.ambient == []
    assert daemon.shown == "idle"


async def test_bad_layer_items_are_skipped(daemon, loader, capsys):
    contexts = []
    add(loader, "p", contexts.append)
    seen = []
    daemon.on("plugin.p.x", seen.append)
    contexts[0].emit({"type": "plugin.p.x", "layer": "news", "scene": "tornado"})
    contexts[0].emit({"type": "plugin.p.x", "layer": "attic", "scene": "rain"})
    contexts[0].emit({"type": "plugin.p.x", "layer": "ambient", "expression": "dancing"})
    assert daemon.state.unread == []
    assert daemon.state.ambient == []
    assert len(seen) == 3
    out = capsys.readouterr().out
    assert "unknown scene 'tornado'" in out
    assert "unknown layer 'attic'" in out


async def test_face_helpers_use_validation(daemon, loader, tmp_path):
    contexts = []
    add(loader, "p", contexts.append)
    ctx = contexts[0]

    result = await ctx.face.show(scene="rain", label="62° · rain 3pm")
    assert "screen" in result
    assert daemon.state.face_content is None
    assert daemon.panel.shown[-1]["title"] == "62° · rain 3pm"

    device = FakeDevice()
    daemon.devices.add(device)
    path = tmp_path / "logo.png"
    Image.new("RGB", (64, 64), (0, 0, 255)).save(path)
    assert await ctx.face.show(card={"avatar": str(path)}, label="Mine") == "Shown on the face."
    assert daemon.state.face_content["avatar"] is True

    assert "Unknown expression" in ctx.face.react("dancing")
    ctx.screen.show("hello", title="Hi")
    assert daemon.panel.shown[-1] == {"title": "Hi", "content": "hello", "actions": []}


def test_idle_behaviors():
    p = Personality()
    calls = []

    def broken(idle, hour):
        raise ValueError("nope")

    def curious(idle, hour):
        calls.append((idle, hour))
        if idle > 30:
            return "confused"
        return None

    p.behaviors = [("bad", broken), ("weird", lambda idle, hour: "dancing"), ("curious", curious)]
    assert p.idle_expression(now=1000, idle=10, hour=12) == "idle"
    assert p.idle_expression(now=1000, idle=40, hour=12) == "confused"
    assert calls == [(10, 12), (40, 12)]
    assert [name for name, fn in p.behaviors] == ["weird", "curious"]
    assert p.idle_expression(now=1000, idle=90, hour=2) == "sleepy"


async def test_plugin_adds_idle_behavior(daemon, loader):
    add(loader, "p", lambda ctx: ctx.idle(lambda idle, hour: "pleased"))
    assert daemon.personality.behaviors[0][0] == "p"


FORECAST = {
    "current": {"time": "2026-10-25T13:15", "temperature_2m": 62.4, "weather_code": 3},
    "hourly": {
        "time": ["2026-10-25T12:00", "2026-10-25T13:00", "2026-10-25T14:00", "2026-10-25T15:00"],
        "weather_code": [61, 3, 3, 63],
        "precipitation_probability": [90, 10, 20, 80],
    },
    "daily": {"temperature_2m_max": [66.0], "temperature_2m_min": [55.2]},
}


def fake_fetch(answers):
    def fetch(url, params):
        return answers[url]
    return fetch


async def test_weather_rain_later(monkeypatch):
    answers = {
        weather.GEOCODE_URL: {"results": [{"name": "Seattle", "country": "United States",
                                           "latitude": 47.6, "longitude": -122.3}]},
        weather.FORECAST_URL: FORECAST,
    }
    monkeypatch.setattr(weather, "fetch_json", fake_fetch(answers))
    result = await weather.get_weather("Seattle")
    assert "Seattle, United States: 62°F (17°C), cloudy. Today 55–66°F." in result
    assert "Rain likely from 3pm." in result
    assert 'face_show(scene="rain", mood="grumpy", label="62° rain 3pm")' in result


def test_weather_clear_and_storm():
    forecast = {
        "current": {"time": "2026-10-25T09:00", "temperature_2m": 80, "weather_code": 0},
        "hourly": {"time": ["2026-10-25T09:00"], "weather_code": [0], "precipitation_probability": [0]},
        "daily": {"temperature_2m_max": [85], "temperature_2m_min": [70]},
    }
    assert 'scene="sun", mood="happy", label="80° clear"' in weather.summarize("Austin", forecast)
    forecast["current"]["weather_code"] = 95
    assert 'scene="storm", mood="grumpy", label="80° storm"' in weather.summarize("Austin", forecast)
    forecast["current"]["weather_code"] = 3
    assert 'scene="cloud", mood="neutral", label="80° cloudy"' in weather.summarize("Austin", forecast)
    forecast["current"]["weather_code"] = 45
    assert 'scene="cloud", mood="neutral", label="80° fog"' in weather.summarize("Austin", forecast)


async def test_weather_network_failure(monkeypatch):
    def offline(url, params):
        raise OSError("no network")

    monkeypatch.setattr(weather, "fetch_json", offline)
    assert await weather.get_weather("Paris") == "Couldn't get the weather for Paris: no network"
    monkeypatch.setattr(weather, "fetch_json", fake_fetch({weather.GEOCODE_URL: {}}))
    assert await weather.get_weather("Nowhere") == "Couldn't find a place called Nowhere."


async def test_email_card_and_screen(daemon, loader):
    loader.load_all([plugins.BUILTIN_DIR])
    show_email = [fn for name, fn, description in loader.tools if name == "email_show_email"][0]
    url = "https://mail.google.com/mail/u/0/#inbox/123"
    result = await show_email("Sarah Johnson <sarah@x.com>", "Re: Demo timing", "- Move demo to Friday 2pm",
                              label="Demo → Fri", url=url)
    assert result == "Shown on the face. The summary is on the screen."
    assert daemon.state.face_content == {"t": "card", "initials": "SJ", "label": "Demo → Fri", "duration_ms": 4000}
    assert daemon.panel.shown[-1] == {
        "title": "Re: Demo timing",
        "content": "**From:** Sarah Johnson <sarah@x.com>\n\n- Move demo to Friday 2pm",
        "actions": [{"label": "Open in Gmail", "url": url}],
    }

    await show_email("noreply@x.com", "Hi", "- hello", label="Hello")
    assert daemon.state.face_content["initials"] == "N"
    assert daemon.panel.shown[-1]["actions"] == []
