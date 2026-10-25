import asyncio

from PIL import Image

from orbi import agents, display
from orbi.state import WorldState, Agent, News, current_layer

from conftest import FakeDevice


def test_priority_order():
    s = WorldState()
    assert current_layer(s) == "idle"
    s.agents.append(Agent("a", "task"))
    assert current_layer(s) == "busy"
    s.unread.append(News("b", False))
    assert current_layer(s) == "worried"
    s.agents.append(Agent("c", "task", status="needs_input"))
    assert current_layer(s) == "expectant"
    s.face_content = {"t": "scene", "name": "rain"}
    assert current_layer(s) == s.face_content
    s.conversation = "listening"
    assert current_layer(s) == "listening"


async def test_busy_then_pleased_then_idle(daemon, monkeypatch):
    monkeypatch.setattr(agents, "NEWS_SECONDS", 0.05)
    daemon.emit({"type": "agent.started", "id": "h1", "task": "weather"})
    daemon.emit({"type": "agent.working", "id": "h1"})
    assert daemon.shown == "busy"
    daemon.emit({"type": "agent.done", "id": "h1", "result": "sunny"})
    assert daemon.state.agents == []
    assert daemon.shown == "pleased"
    await asyncio.sleep(0.1)
    assert daemon.state.unread == []
    assert daemon.shown == "idle"


async def test_failed_is_worried(daemon, monkeypatch):
    monkeypatch.setattr(agents, "NEWS_SECONDS", 0.05)
    daemon.emit({"type": "agent.started", "id": "h1", "task": "x"})
    daemon.emit({"type": "agent.failed", "id": "h1", "error": "boom"})
    assert daemon.shown == "worried"
    await asyncio.sleep(0.1)
    assert daemon.shown == "idle"


async def test_needs_input_beats_news_and_work(daemon):
    daemon.emit({"type": "agent.started", "id": "h1", "task": "x"})
    daemon.emit({"type": "agent.started", "id": "h2", "task": "y"})
    daemon.emit({"type": "agent.done", "id": "h2", "result": ""})
    daemon.emit({"type": "agent.needs_input", "id": "h1", "question": "ok?"})
    assert daemon.shown == "expectant"


async def test_face_content_shows_then_clears(daemon, monkeypatch):
    monkeypatch.setattr(display, "FACE_SECONDS", 0.05)
    result = await daemon.display.face_show(scene="rain", mood="grumpy", label="62° rain")
    assert result == "Shown on the face."
    assert daemon.shown["name"] == "rain"
    await asyncio.sleep(0.1)
    assert daemon.state.face_content is None
    assert daemon.shown == "idle"


async def test_conversation_beats_content_beats_needs_input(daemon):
    daemon.emit({"type": "agent.started", "id": "h1", "task": "x"})
    daemon.emit({"type": "agent.needs_input", "id": "h1"})
    await daemon.display.face_show(card={"icon": "mail"}, label="Demo → Fri")
    assert daemon.shown["t"] == "card"
    daemon.state.conversation = "listening"
    daemon.refresh()
    assert daemon.shown == "listening"
    daemon.state.conversation = None
    daemon.refresh()
    assert daemon.shown["t"] == "card"
    daemon.state.face_content = None
    daemon.refresh()
    assert daemon.shown == "expectant"


async def test_bad_face_show_goes_to_screen(daemon):
    result = await daemon.display.face_show(scene="rain", mood="grumpy", label="62° · rain 3pm")
    assert "screen" in result
    assert daemon.state.face_content is None
    assert daemon.panel.shown[0]["title"] == "62° · rain 3pm"
    assert "Scene: rain" in daemon.panel.shown[0]["content"]


async def test_screen_show_opens_panel_and_glances(daemon):
    device = FakeDevice()
    daemon.devices.add(device)
    actions = [{"label": "Open in Gmail", "url": "https://mail.google.com"}]
    daemon.display.screen_show("**From:** Sarah", title="Re: Demo timing", actions=actions)
    assert daemon.panel.shown == [{"title": "Re: Demo timing", "content": "**From:** Sarah", "actions": actions}]
    await asyncio.sleep(0)
    assert display.GLANCE_GAZE in device.sent
    assert daemon.glance_until > 0


async def test_screen_show_without_panel(daemon):
    daemon.panel = None
    assert daemon.display.screen_show("hi") == "Shown on the screen."


async def test_face_react_is_a_temporary_expression(daemon, monkeypatch):
    monkeypatch.setattr(display, "REACT_SECONDS", 0.05)
    assert daemon.display.face_react("pleased") == "Done."
    assert daemon.shown == {"t": "expression", "name": "pleased"}
    await asyncio.sleep(0.1)
    assert daemon.shown == "idle"
    assert "Unknown expression" in daemon.display.face_react("dancing")


async def test_avatar_card(daemon, tmp_path):
    device = FakeDevice()
    daemon.devices.add(device)
    path = tmp_path / "face.png"
    Image.new("RGB", (200, 100), (255, 0, 0)).save(path)
    await daemon.display.face_show(card={"avatar": str(path)}, label="Sarah J.")
    await asyncio.sleep(0)
    frame = device.sent[0]
    assert len(frame) == 120 * 120 * 2
    assert frame[:2] == (0xF800).to_bytes(2, "little")
    assert device.sent[1] == {"t": "card", "avatar": True, "label": "Sarah J.", "duration_ms": 4000}


async def test_avatar_that_fails_to_load_goes_to_screen(daemon, tmp_path):
    result = await daemon.display.face_show(card={"avatar": str(tmp_path / "missing.png")}, label="Sarah J.")
    assert "screen" in result
    assert daemon.panel.shown
